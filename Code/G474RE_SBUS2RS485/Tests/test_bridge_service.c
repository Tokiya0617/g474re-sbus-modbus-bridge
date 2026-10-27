#include "bridge_service.h"
#include "bridge_service_test_port.h"
#include "modbus_crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Tests require assert()."
#endif

static uint32_t s_mask, s_ms, s_us;
static RcSnapshot s_snapshot;
static Rs485PortStats s_port;
static Rs485Byte s_bytes[1024];
static size_t s_head, s_tail, s_reply_length;
static uint8_t s_reply[256];
static unsigned s_port_starts;
static bool s_start_ok = true, s_send_ok = true, s_mutate_after_read;
uint32_t __get_PRIMASK(void) { return s_mask; }
void __disable_irq(void) { s_mask = 1U; }
void __set_PRIMASK(uint32_t saved) { s_mask = saved; }
void __DMB(void) {}
uint32_t rs485_port_now_ms(void) { return s_ms; }
uint32_t rs485_port_now_us(void) { return s_us; }
bool rc_snapshot_read(RcSnapshot *out)
{
    assert(out != NULL); *out = s_snapshot;
    if (s_mutate_after_read)
    {
        s_mutate_after_read = false;
        ++s_snapshot.sequence;
        for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) s_snapshot.frame.channels[i] += 1000U;
    }
    return true;
}
bool sbus_uart_get_stats(SbusUartStats *out)
{
    assert(out != NULL); memset(out, 0, sizeof(*out));
    out->valid_frames = 0x12345678U; out->rejected_candidates = 5U; return true;
}
bool rs485_port_start(const BridgeConfig *config)
{
    assert(bridge_config_valid(config)); ++s_port_starts;
    s_port.running = s_start_ok; s_port.terminal_fault = !s_start_ok;
    s_port.rx_epoch_us = s_us; return s_start_ok;
}
void rs485_port_process(void) {}
bool rs485_port_pop(Rs485Byte *out)
{
    if (!s_port.running || s_port.transmitting || s_tail == s_head) return false;
    *out = s_bytes[s_tail++ % 1024U]; return true;
}
bool rs485_port_bus_idle(void)
{
    return s_port.running && !s_port.transmitting && s_head == s_tail;
}
bool rs485_port_send(const uint8_t *data, size_t length)
{
    if (!s_send_ok || !rs485_port_bus_idle()) return false;
    assert(length <= sizeof(s_reply));
    memcpy(s_reply, data, length); s_reply_length = length;
    s_port.transmitting = true; return true;
}
bool rs485_port_get_stats(Rs485PortStats *out) { *out = s_port; return true; }
static BridgeConfig config(void)
{
    const BridgeConfig value = {
        .enabled = true, .approved = true, .slave_address = 7U, .register_base = 100U,
        .word_order = REGISTER_HIGH_WORD_FIRST, .baud_rate = 115200U,
        .parity = BRIDGE_PARITY_EVEN, .stop_bits = 1U, .de_active_high = true,
        .de_setup_us = 10U, .tx_timeout_ms = 100U, .sbus_timeout_ms = 100U,
        .plc_timeout_ms = 1000U, .firmware_version = 0x0102U
    };
    return value;
}
static BridgeDiagnostics diagnostics(void)
{
    BridgeDiagnostics out; assert(bridge_service_get_diagnostics(&out)); return out;
}
static uint16_t reg(size_t offset)
{
    assert(s_reply_length >= 5U + (offset + 1U) * 2U);
    return (uint16_t)(((uint16_t)s_reply[3U + offset * 2U] << 8U) | s_reply[4U + offset * 2U]);
}
static void finish_tx(void)
{
    if (!s_port.transmitting) return;
    s_us += (uint32_t)s_reply_length * 96U;
    s_port.last_tx_end_us = s_us; ++s_port.tx_completed;
    s_port.transmitting = false;
}
static void enqueue(uint8_t address, uint8_t function, bool bad_crc)
{
    finish_tx();
    uint8_t request[8] = {address, function, 0U, 100U, 0U, 28U, 0U, 0U};
    const uint16_t crc = modbus_crc16(request, 6U);
    request[6] = (uint8_t)crc; request[7] = (uint8_t)(crc >> 8U);
    if (bad_crc) request[7] ^= 1U;
    s_us += 5000U; s_reply_length = 0U;
    for (size_t i = 0U; i < sizeof(request); ++i)
    {
        s_bytes[s_head % 1024U].byte = request[i];
        s_bytes[s_head++ % 1024U].end_us = s_us;
        s_us += 96U;
    }
    s_us += 1750U;
}
static void query(void)
{
    enqueue(7U, 3U, false); bridge_service_process();
    assert(s_reply_length == 61U && modbus_crc_valid(s_reply, s_reply_length));
}
static void publish_frame(void)
{
    s_snapshot.has_frame = s_snapshot.transport_ok = true;
    ++s_snapshot.sequence; s_snapshot.received_ms = s_ms;
    s_snapshot.frame.failsafe = s_snapshot.frame.frame_lost = false;
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) s_snapshot.frame.channels[i] = (uint16_t)(100U + i);
}

int main(int argc, char **argv)
{
    BridgeConfig cfg = config();
    if (argc > 1 && strcmp(argv[1], "disabled") == 0)
    {
        assert(bridge_service_start(bridge_config_get()) == BRIDGE_DISABLED);
        bridge_service_process();
        assert(s_port_starts == 0U && !diagnostics().input.usable);
        puts("PASS: default disabled config never starts RS485"); return 0;
    }
    if (argc > 1 && strcmp(argv[1], "bad-config") == 0)
    {
        cfg.approved = false;
        assert(bridge_service_start(&cfg) == BRIDGE_CONFIG_FAULT);
        assert(s_port_starts == 0U && diagnostics().enabled);
        puts("PASS: unapproved enabled config remains configuration fault"); return 0;
    }
    if (argc > 1 && strcmp(argv[1], "start-failure") == 0)
    {
        s_start_ok = false;
        assert(bridge_service_start(&cfg) == BRIDGE_COMM_FAULT);
        bridge_service_process();
        assert(diagnostics().comm_fault && s_port_starts == 1U);
        puts("PASS: bridge reports port start failure without retry loop"); return 0;
    }
    assert(bridge_service_start(&cfg) == BRIDGE_STARTUP);
    query();
    assert((reg(2U) & RC_REG_NO_FRAME) != 0U && (reg(2U) & RC_REG_USABLE) == 0U);
    assert(reg(3U) == UINT16_MAX && diagnostics().have_request);
    publish_frame(); query();
    assert(reg(2U) == RC_REG_USABLE && reg(4U) == 100U && reg(19U) == 115U);
    assert(reg(20U) == 0x1234U && reg(21U) == 0x5678U);
    assert(reg(24U) == 0U && reg(25U) == 2U && reg(26U) == 0x0102U);
    assert(diagnostics().state == BRIDGE_ACTIVE);
    s_mutate_after_read = true; query();
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) assert(reg(4U + i) == 100U + i);
    query();
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) assert(reg(4U + i) == 1100U + i);
    finish_tx(); s_ms = 100U; bridge_service_process();
    assert(diagnostics().state == BRIDGE_SBUS_LOST && diagnostics().input.timed_out);
    query(); assert((reg(2U) & RC_REG_TIMEOUT) != 0U && (reg(2U) & RC_REG_USABLE) == 0U);
    publish_frame(); query(); assert(diagnostics().state == BRIDGE_ACTIVE);
    s_snapshot.frame.failsafe = true; query();
    assert(diagnostics().state == BRIDGE_SBUS_FAILSAFE && (reg(2U) & RC_REG_FAILSAFE) != 0U);
    s_snapshot.frame.failsafe = false; s_snapshot.transport_ok = false; query();
    assert(diagnostics().state == BRIDGE_INPUT_FAULT && (reg(2U) & RC_REG_TRANSPORT_FAULT) != 0U);
    publish_frame();
    enqueue(0U, 3U, false); bridge_service_process(); assert(s_reply_length == 0U);
    enqueue(8U, 3U, false); bridge_service_process(); assert(s_reply_length == 0U);
    enqueue(7U, 3U, true); bridge_service_process();
    assert(s_reply_length == 0U && diagnostics().modbus.crc_errors == 1U);
    enqueue(7U, 6U, false); bridge_service_process();
    assert(s_reply_length == 5U && s_reply[1] == 0x86U && s_reply[2] == 1U);
    assert(diagnostics().modbus.exceptions == 1U);
    finish_tx(); s_send_ok = false;
    enqueue(7U, 3U, false); bridge_service_process();
    assert(s_reply_length == 0U && diagnostics().discarded_responses == 1U);
    s_send_ok = true;
    ++s_port.fault_generation; ++s_port.restarts; s_port.rx_epoch_us = s_us;
    bridge_service_process(); query(); /* 丢弃旧半帧后恢复新请求。 */
    finish_tx(); s_ms += 1000U; bridge_service_process();
    assert(diagnostics().state == BRIDGE_COMM_FAULT && diagnostics().comm_fault);
    publish_frame(); query();
    assert(diagnostics().state == BRIDGE_ACTIVE && !diagnostics().comm_fault);
    finish_tx();
    /* 持续周期采样但没有新帧，旧时间戳绕完整一圈也不能重新 usable。 */
    s_ms += (uint32_t)INT32_MAX; bridge_service_process();
    s_ms += (uint32_t)INT32_MAX; bridge_service_process();
    s_ms += 2U; bridge_service_process();
    assert(diagnostics().input.age_ms == UINT32_MAX && diagnostics().input.timed_out);
    assert(!diagnostics().input.usable && diagnostics().comm_fault);
    query(); assert(reg(3U) == UINT16_MAX && (reg(2U) & RC_REG_TIMEOUT) != 0U);
    publish_frame(); query(); assert(diagnostics().input.usable);
    finish_tx(); s_port.running = false; s_port.terminal_fault = true;
    bridge_service_process(); assert(diagnostics().state == BRIDGE_COMM_FAULT);
    s_mask = 1U; (void)diagnostics(); assert(s_mask == 1U); s_mask = 0U;
    assert(!bridge_service_get_diagnostics(NULL));
    puts("PASS: bridge startup/active/lost/failsafe, fresh coherent replies, CRC/broadcast/errors, full tick wrap");
    return 0;
}
