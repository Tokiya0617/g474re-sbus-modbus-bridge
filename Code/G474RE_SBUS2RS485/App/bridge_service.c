#include "bridge_service.h"
#include "modbus_rtu.h"
#include "modbus_crc.h"
#include <string.h>
#if defined(BRIDGE_SERVICE_HOST_TEST)
#include "bridge_service_test_port.h"
#else
#include "stm32g4xx.h"
#endif

static BridgeConfig s_config;
static ModbusSlave s_slave;
static ModbusRtu s_rtu;
static BridgeDiagnostics s_diag, s_published;
static uint8_t s_request[MODBUS_ADU_MAX], s_response[MODBUS_ADU_MAX];
static uint16_t s_registers[BRIDGE_REGISTER_COUNT];
static bool s_initialized, s_started, s_frame_seen, s_plc_expired;
static uint32_t s_started_ms, s_seen_fault, s_seen_tx, s_seen_restart, s_age_sample_ms;
static uint32_t s_frame_sequence, s_frame_time;
static uint64_t s_frame_age_ms;

static uint32_t lock(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq(); __DMB(); return saved;
}
static void unlock(uint32_t saved) { __DMB(); __set_PRIMASK(saved); }
static void publish_diagnostics(void)
{
    const uint32_t saved = lock();
    s_published = s_diag;
    unlock(saved);
}

static void sample_input(void)
{
    (void)rc_snapshot_read(&s_diag.snapshot);
    const uint32_t now_ms = rs485_port_now_ms(); /* 读取快照之后取同一 HAL 毫秒时基。 */
    (void)rc_status_evaluate(&s_diag.snapshot, now_ms, s_config.sbus_timeout_ms, &s_diag.input);
    if (s_diag.snapshot.has_frame)
    {
        if (!s_frame_seen || s_frame_sequence != s_diag.snapshot.sequence ||
            s_frame_time != s_diag.snapshot.received_ms)
        {
            s_frame_seen = true;
            s_frame_sequence = s_diag.snapshot.sequence;
            s_frame_time = s_diag.snapshot.received_ms;
            s_frame_age_ms = (uint32_t)(now_ms - s_frame_time);
        }
        else s_frame_age_ms += (uint32_t)(now_ms - s_age_sample_ms);
        /* 服务持续周期运行时扩展旧帧年龄，避免 49.7 天后旧快照因整圈回绕重新有效。
         * 相邻 process 间隔必须小于一个 HAL 毫秒计数周期，HAL 时基本身须健康。 */
        s_diag.input.age_ms = s_frame_age_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)s_frame_age_ms;
        if (s_diag.input.timeout_configured && s_frame_age_ms >= s_config.sbus_timeout_ms)
        {
            s_diag.input.timed_out = true;
            s_diag.input.usable = false;
        }
    }
    else s_frame_seen = false;
    s_age_sample_ms = now_ms;
    (void)sbus_uart_get_stats(&s_diag.sbus);
}

static void update_state(void)
{
    const uint32_t since_request = rs485_port_now_ms() -
        (s_diag.have_request ? s_diag.last_request_ms : s_started_ms);
    if (s_started && since_request >= s_config.plc_timeout_ms) s_plc_expired = true;
    s_diag.comm_fault = s_config.enabled && bridge_config_valid(&s_config) &&
                        (!s_started || !s_diag.port.running || s_diag.port.terminal_fault || s_plc_expired);
    if (!s_config.enabled) s_diag.state = BRIDGE_DISABLED;
    else if (!bridge_config_valid(&s_config)) s_diag.state = BRIDGE_CONFIG_FAULT;
    else if (!s_started || s_diag.comm_fault) s_diag.state = BRIDGE_COMM_FAULT;
    else if (!s_diag.input.has_frame) s_diag.state = BRIDGE_STARTUP;
    else if (s_diag.input.failsafe) s_diag.state = BRIDGE_SBUS_FAILSAFE;
    else if (s_diag.input.timed_out) s_diag.state = BRIDGE_SBUS_LOST;
    else if (!s_diag.input.usable) s_diag.state = BRIDGE_INPUT_FAULT;
    else s_diag.state = BRIDGE_ACTIVE;
}

BridgeState bridge_service_start(const BridgeConfig *config)
{
    if (s_initialized) return s_diag.state;
    s_initialized = true;
    if (config != NULL) s_config = *config;
    s_diag.enabled = s_config.enabled;
    s_started_ms = rs485_port_now_ms();
    if (bridge_config_valid(&s_config) &&
        modbus_slave_init(&s_slave, s_config.slave_address, s_config.register_base) &&
        rs485_port_start(&s_config))
    {
        (void)rs485_port_get_stats(&s_diag.port);
        s_started = modbus_rtu_init(&s_rtu, s_config.baud_rate, s_diag.port.rx_epoch_us);
    }
    (void)rs485_port_get_stats(&s_diag.port);
    sample_input();
    update_state();
    publish_diagnostics();
    return s_diag.state;
}

void bridge_service_process(void)
{
    if (!s_initialized) return;
    if (s_started)
    {
        rs485_port_process();
        (void)rs485_port_get_stats(&s_diag.port);
        if (s_seen_fault != s_diag.port.fault_generation || s_seen_restart != s_diag.port.restarts)
        {
            s_seen_fault = s_diag.port.fault_generation;
            s_seen_restart = s_diag.port.restarts;
            modbus_rtu_discard(&s_rtu, s_diag.port.running ? s_diag.port.rx_epoch_us : rs485_port_now_us());
        }
        if (s_seen_tx != s_diag.port.tx_completed)
        {
            s_seen_tx = s_diag.port.tx_completed;
            modbus_rtu_discard(&s_rtu, s_diag.port.last_tx_end_us);
        }
        Rs485Byte byte;
        /* 有界处理：即使线路被噪声持续灌入，也不在一次调用中无限循环。 */
        for (size_t i = 0U; i < 512U && rs485_port_pop(&byte); ++i)
            modbus_rtu_push(&s_rtu, byte.byte, byte.end_us);

        size_t length = 0U;
        if (modbus_rtu_poll(&s_rtu, rs485_port_now_us(), rs485_port_bus_idle(),
                            s_request, sizeof(s_request), &length))
        {
            /* 每次请求重新读取最新快照，不把上一应答的通道留作下一事务数据。 */
            sample_input();
            const bool addressed = modbus_crc_valid(s_request, length) &&
                                   s_request[0] == s_config.slave_address &&
                                   (s_request[1] & 0x80U) == 0U;
            if (addressed)
            {
                s_diag.have_request = true;
                s_plc_expired = false;
                s_diag.last_request_ms = rs485_port_now_ms();
            }
            (void)rs485_port_get_stats(&s_diag.port);
            update_state();
            RegisterDiagnostics counters = {0};
            counters.valid_sbus_frames = s_diag.sbus.valid_frames;
            counters.rejected_candidates = s_diag.sbus.rejected_candidates;
            counters.modbus_requests = s_slave.stats.requests + (addressed ? 1U : 0U);
            counters.firmware_version = s_config.firmware_version;
            counters.comm_fault = s_diag.comm_fault;
            if (register_map_build(&s_diag.snapshot, &s_diag.input, s_config.word_order, &counters, s_registers))
            {
                const size_t response_length = modbus_slave_handle(&s_slave, s_request, length,
                    s_registers, s_response, sizeof(s_response));
                if (response_length != 0U && !rs485_port_send(s_response, response_length))
                    ++s_diag.discarded_responses;
            }
        }
        s_diag.modbus = s_slave.stats;
        s_diag.rtu_gap_errors = s_rtu.gap_errors;
        s_diag.rtu_overflows = s_rtu.overflows;
        s_diag.skipped_requests = s_rtu.skipped_frames;
        (void)rs485_port_get_stats(&s_diag.port);
    }
    sample_input(); /* 无 UART 事件也检查帧年龄，默认禁用时结果仍不可用。 */
    update_state();
    publish_diagnostics();
}

bool bridge_service_get_diagnostics(BridgeDiagnostics *out)
{
    if (out == NULL) return false;
    const uint32_t saved = lock();
    *out = s_published;
    unlock(saved);
    return true;
}
