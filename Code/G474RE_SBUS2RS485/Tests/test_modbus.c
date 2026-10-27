#include "modbus_crc.h"
#include "modbus_rtu.h"
#include "bridge_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Tests require assert()."
#endif

static size_t with_crc(uint8_t *bytes, size_t length)
{
    const uint16_t crc = modbus_crc16(bytes, length);
    bytes[length] = (uint8_t)crc;
    bytes[length + 1U] = (uint8_t)(crc >> 8U);
    return length + 2U;
}
static uint16_t reference_crc(const uint8_t *bytes, size_t length)
{
    /* 独立 MSB 多项式实现：每个输入字节/最终输出反转位序。 */
    uint16_t value = UINT16_MAX;
    for (size_t i = 0U; i < length; ++i)
    {
        unsigned reflected = 0U;
        for (unsigned b = 0U; b < 8U; ++b) reflected |= ((bytes[i] >> b) & 1U) << (7U - b);
        value ^= (uint16_t)(reflected << 8U);
        for (unsigned b = 0U; b < 8U; ++b)
            value = (uint16_t)((value << 1U) ^ ((value & 0x8000U) != 0U ? 0x8005U : 0U));
    }
    uint16_t result = 0U;
    for (unsigned b = 0U; b < 16U; ++b) result |= (uint16_t)(((value >> b) & 1U) << (15U - b));
    return result;
}
static void test_crc(void)
{
    const uint8_t text[] = "123456789";
    assert(modbus_crc16(text, 9U) == 0x4B37U);
    uint8_t request[8] = {1U, 3U, 0U, 0U, 0U, 10U, 0xC5U, 0xCDU};
    assert(modbus_crc_valid(request, sizeof(request)));
    for (unsigned bit = 0U; bit < 64U; ++bit)
    {
        request[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
        assert(!modbus_crc_valid(request, sizeof(request)));
        request[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
    }
    assert(modbus_crc16(NULL, 0U) == UINT16_MAX);
    assert(!modbus_crc_valid(NULL, 8U));
    uint8_t bytes[256];
    uint32_t random = 1U;
    for (size_t n = 0U; n <= sizeof(bytes); ++n)
    {
        for (size_t i = 0U; i < n; ++i)
        {
            random = random * 1664525U + 1013904223U;
            bytes[i] = (uint8_t)(random >> 24U);
        }
        assert(modbus_crc16(bytes, n) == reference_crc(bytes, n));
    }
}
static void test_map(void)
{
    RcSnapshot snapshot = {0};
    RcStatus status;
    RegisterDiagnostics diagnostics = {0};
    uint16_t registers[BRIDGE_REGISTER_COUNT];
    assert(rc_status_evaluate(&snapshot, 0U, 0U, &status));
    assert(register_map_build(&snapshot, &status, REGISTER_HIGH_WORD_FIRST, &diagnostics, registers));
    assert(registers[2] == (RC_REG_NO_FRAME | RC_REG_UNCONFIGURED));
    assert(registers[3] == UINT16_MAX && registers[27] == 0U);
    snapshot.has_frame = snapshot.transport_ok = true;
    snapshot.sequence = 0x12345678U;
    snapshot.received_ms = UINT32_MAX - 10U;
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) snapshot.frame.channels[i] = (uint16_t)(i * 127U);
    diagnostics.valid_sbus_frames = 0x12345678U;
    diagnostics.rejected_candidates = 0xAABBCCDDU;
    diagnostics.modbus_requests = 0xFFFF0001U;
    diagnostics.firmware_version = 0x0102U;
    assert(rc_status_evaluate(&snapshot, 9U, 100U, &status));
    assert(register_map_build(&snapshot, &status, REGISTER_HIGH_WORD_FIRST, &diagnostics, registers));
    assert(registers[0] == 1U && registers[1] == 0x5678U && registers[2] == RC_REG_USABLE);
    assert(registers[3] == 20U && registers[20] == 0x1234U && registers[21] == 0x5678U);
    assert(registers[22] == 0xAABBU && registers[23] == 0xCCDDU);
    assert(registers[24] == 0xFFFFU && registers[25] == 1U && registers[26] == 0x0102U);
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i) assert(registers[4U + i] == snapshot.frame.channels[i]);
    assert(register_map_build(&snapshot, &status, REGISTER_LOW_WORD_FIRST, &diagnostics, registers));
    assert(registers[20] == 0x5678U && registers[21] == 0x1234U);
    snapshot.transport_ok = false;
    snapshot.frame.failsafe = snapshot.frame.frame_lost = true;
    diagnostics.comm_fault = true;
    assert(rc_status_evaluate(&snapshot, 100000U, 100U, &status));
    assert(register_map_build(&snapshot, &status, REGISTER_HIGH_WORD_FIRST, &diagnostics, registers));
    assert(registers[2] == (RC_REG_FAILSAFE | RC_REG_FRAME_LOST | RC_REG_TIMEOUT |
                            RC_REG_TRANSPORT_FAULT | RC_REG_COMM_FAULT));
    assert(registers[3] == UINT16_MAX && registers[5] == 127U); /* 不伪造安全值。 */
    uint16_t before[BRIDGE_REGISTER_COUNT];
    memcpy(before, registers, sizeof(before));
    assert(!register_map_build(&snapshot, &status, REGISTER_WORD_ORDER_UNSET, &diagnostics, registers));
    assert(memcmp(before, registers, sizeof(before)) == 0);
    assert(!register_map_build(NULL, &status, REGISTER_HIGH_WORD_FIRST, &diagnostics, registers));
    assert(!register_map_build(&snapshot, NULL, REGISTER_HIGH_WORD_FIRST, &diagnostics, registers));
    assert(!register_map_build(&snapshot, &status, REGISTER_HIGH_WORD_FIRST, NULL, registers));
    assert(!register_map_build(&snapshot, &status, REGISTER_HIGH_WORD_FIRST, &diagnostics, NULL));
}
static void test_slave(void)
{
    ModbusSlave slave;
    uint16_t registers[BRIDGE_REGISTER_COUNT];
    for (size_t i = 0U; i < BRIDGE_REGISTER_COUNT; ++i) registers[i] = (uint16_t)(0x1200U + i);
    uint8_t request[256] = {1U, 3U}, response[258];
    assert(!modbus_slave_init(&slave, 0U, 0U));
    assert(!modbus_slave_init(&slave, 248U, 0U));
    assert(!modbus_slave_init(&slave, 1U, 65509U));
    assert(modbus_slave_init(&slave, 1U, 100U));
    for (uint16_t offset = 0U; offset < BRIDGE_REGISTER_COUNT; ++offset)
    {
        for (uint16_t quantity = 1U; quantity <= BRIDGE_REGISTER_COUNT - offset; ++quantity)
        {
            request[2] = 0U; request[3] = (uint8_t)(100U + offset);
            request[4] = 0U; request[5] = (uint8_t)quantity;
            with_crc(request, 6U);
            memset(response, 0xA5, sizeof(response));
            const size_t length = modbus_slave_handle(&slave, request, 8U, registers, response, 256U);
            assert(length == 5U + (size_t)quantity * 2U && response[length] == 0xA5U);
            assert(modbus_crc_valid(response, length));
            assert(response[0] == 1U && response[1] == 3U && response[2] == quantity * 2U);
            for (uint16_t i = 0U; i < quantity; ++i)
                assert((((uint16_t)response[3U + i * 2U] << 8U) | response[4U + i * 2U]) == registers[offset + i]);
        }
    }
    const uint16_t quantities[] = {0U, 126U, UINT16_MAX};
    for (size_t i = 0U; i < 3U; ++i)
    {
        request[4] = (uint8_t)(quantities[i] >> 8U); request[5] = (uint8_t)quantities[i];
        with_crc(request, 6U);
        assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 5U);
        assert(response[1] == 0x83U && response[2] == 3U && modbus_crc_valid(response, 5U));
    }
    request[4] = 0U; request[5] = 1U; request[3] = 99U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 5U && response[2] == 2U);
    request[3] = 128U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 5U && response[2] == 2U);
    request[1] = 6U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 5U && response[2] == 1U);
    request[0] = 0U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 0U);
    request[0] = 2U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 0U);
    request[0] = 1U; request[1] = 0x83U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 0U);
    request[1] = 3U; request[3] = 100U; with_crc(request, 6U);
    memset(response, 0xA5, sizeof(response));
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 6U) == 0U);
    for (size_t i = 0U; i < sizeof(response); ++i) assert(response[i] == 0xA5U);
    request[7] ^= 1U;
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 0U);
    request[7] ^= 1U;
    assert(modbus_slave_handle(&slave, request, with_crc(request, 5U), registers, response, 256U) == 5U && response[2] == 3U);
    assert(modbus_slave_handle(NULL, request, 8U, registers, response, 256U) == 0U);
    assert(modbus_slave_handle(&slave, NULL, 8U, registers, response, 256U) == 0U);
    assert(modbus_slave_init(&slave, 247U, 65508U));
    request[0] = 247U; request[1] = 3U; request[2] = 255U; request[3] = 255U;
    request[4] = 0U; request[5] = 1U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 7U);
    request[5] = 2U; with_crc(request, 6U);
    assert(modbus_slave_handle(&slave, request, 8U, registers, response, 256U) == 5U && response[2] == 2U);
}
static void test_rtu(void)
{
    ModbusRtu rtu;
    uint8_t out[256]; size_t length;
    assert(modbus_rtu_init(&rtu, 9600U, 0U));
    assert(rtu.timing.character_us == 1146U && rtu.timing.t1_5_us == 1719U && rtu.timing.t3_5_us == 4011U);
    assert(modbus_rtu_init(&rtu, 115200U, 0U));
    assert(rtu.timing.character_us == 96U && rtu.timing.t1_5_us == 750U && rtu.timing.t3_5_us == 1750U);
    const uint32_t first = 2000U;
    for (unsigned i = 0U; i < 8U; ++i) modbus_rtu_push(&rtu, (uint8_t)i, first + i * 96U);
    assert(!modbus_rtu_poll(&rtu, first + 7U * 96U + 1749U, true, out, sizeof(out), &length));
    assert(!modbus_rtu_poll(&rtu, first + 7U * 96U + 1750U, false, out, sizeof(out), &length));
    assert(modbus_rtu_poll(&rtu, first + 7U * 96U + 1750U, true, out, sizeof(out), &length));
    assert(length == 8U && out[7] == 7U);
    assert(!modbus_rtu_poll(&rtu, 10000U, true, out, sizeof(out), &length));
    modbus_rtu_push(&rtu, 1U, 10000U);
    modbus_rtu_push(&rtu, 2U, 10000U + 96U + 750U); /* 边界仍可接受。 */
    modbus_rtu_push(&rtu, 3U, 10000U + 2U * 96U + 1501U); /* >t1.5 丢整帧。 */
    assert(rtu.gap_errors == 1U && rtu.discarding);
    assert(!modbus_rtu_poll(&rtu, 20000U, true, out, sizeof(out), &length));
    for (unsigned i = 0U; i < 257U; ++i) modbus_rtu_push(&rtu, 0U, 30000U + i * 96U);
    assert(rtu.overflows == 1U);
    assert(!modbus_rtu_poll(&rtu, 60000U, true, out, sizeof(out), &length));
    for (unsigned i = 0U; i < 8U; ++i) modbus_rtu_push(&rtu, 1U, 70000U + i * 96U);
    modbus_rtu_push(&rtu, 2U, 80000U);
    assert(rtu.skipped_frames == 1U && rtu.length == 1U && rtu.bytes[0] == 2U);
    assert(modbus_rtu_init(&rtu, 115200U, UINT32_MAX - 5000U));
    for (unsigned i = 0U; i < 4U; ++i) modbus_rtu_push(&rtu, (uint8_t)i, UINT32_MAX - 200U + i * 96U);
    assert(modbus_rtu_poll(&rtu, 2000U, true, out, sizeof(out), &length) && length == 4U);
    assert(!modbus_rtu_init(&rtu, 0U, 0U));
}
static void test_adversarial_inputs(void)
{
    ModbusSlave slave;
    uint16_t registers[BRIDGE_REGISTER_COUNT] = {0};
    uint8_t request[256], guarded[258];
    uint32_t random = 0x12345678U;
    assert(modbus_slave_init(&slave, 7U, 65508U));
    for (unsigned trial = 0U; trial < 10000U; ++trial)
    {
        random = random * 1664525U + 1013904223U;
        const size_t length = (size_t)(random % 257U);
        const size_t capacity = (size_t)((random >> 9U) % 257U);
        for (size_t i = 0U; i < length; ++i)
        {
            random = random * 1664525U + 1013904223U;
            request[i] = (uint8_t)(random >> 24U);
        }
        if (length >= 4U && (trial & 1U) == 0U)
        {
            request[0] = 7U;
            if ((trial & 2U) == 0U) request[1] = 3U;
            with_crc(request, length - 2U);
        }
        memset(guarded, 0xA5, sizeof(guarded));
        const size_t reply = modbus_slave_handle(&slave, request, length, registers, &guarded[1], capacity);
        assert(reply <= capacity && guarded[0] == 0xA5U);
        for (size_t i = capacity + 1U; i < sizeof(guarded); ++i) assert(guarded[i] == 0xA5U);
        if (reply != 0U) assert(modbus_crc_valid(&guarded[1], reply));
    }
}
static void test_configuration_gate(void)
{
    const BridgeConfig valid = {
        .enabled = true, .approved = true, .slave_address = 1U, .register_base = 0U,
        .word_order = REGISTER_HIGH_WORD_FIRST, .baud_rate = 115200U,
        .parity = BRIDGE_PARITY_EVEN, .stop_bits = 1U, .de_active_high = true,
        .de_setup_us = 10U, .tx_timeout_ms = 100U, .sbus_timeout_ms = 100U,
        .plc_timeout_ms = 1000U
    };
    assert(bridge_config_valid(&valid));
    BridgeConfig candidate;
#define REJECT_FIELD(field, value) do { candidate = valid; candidate.field = (value); \
                                       assert(!bridge_config_valid(&candidate)); } while (0)
    REJECT_FIELD(enabled, false);
    REJECT_FIELD(approved, false);
    REJECT_FIELD(slave_address, 0U);
    REJECT_FIELD(slave_address, 248U);
    REJECT_FIELD(register_base, 65509U);
    REJECT_FIELD(word_order, REGISTER_WORD_ORDER_UNSET);
    REJECT_FIELD(baud_rate, 0U);
    REJECT_FIELD(baud_rate, 1000001U);
    REJECT_FIELD(parity, BRIDGE_PARITY_UNSET);
    REJECT_FIELD(stop_bits, 2U);
    REJECT_FIELD(de_setup_us, 1000001U);
    REJECT_FIELD(tx_timeout_ms, 6U);
    REJECT_FIELD(tx_timeout_ms, 0U);
    REJECT_FIELD(sbus_timeout_ms, 0U);
    REJECT_FIELD(sbus_timeout_ms, UINT32_MAX);
    REJECT_FIELD(plc_timeout_ms, 0U);
#undef REJECT_FIELD
    candidate = valid; candidate.parity = BRIDGE_PARITY_ODD;
    assert(bridge_config_valid(&candidate));
    candidate.parity = BRIDGE_PARITY_NONE;
    assert(!bridge_config_valid(&candidate)); /* 不接受 8N1 冒充 11 位 RTU 字符。 */
    candidate.stop_bits = 2U; candidate.word_order = REGISTER_LOW_WORD_FIRST;
    assert(bridge_config_valid(&candidate));
    candidate.register_base = 65508U; assert(bridge_config_valid(&candidate));
}
int main(void)
{
    assert(!bridge_config_valid(bridge_config_get()));
    test_crc(); test_map(); test_slave(); test_rtu(); test_adversarial_inputs(); test_configuration_gate();
    puts("PASS: CRC reference/vectors, register map, all read spans, exceptions, broadcast, RTU gaps/wrap/bounds");
    return 0;
}
