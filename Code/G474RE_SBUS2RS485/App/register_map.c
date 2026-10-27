#include "register_map.h"
#include <string.h>

static void split_counter(uint16_t *out, uint32_t value, RegisterWordOrder order)
{
    const uint16_t high = (uint16_t)(value >> 16U);
    const uint16_t low = (uint16_t)value;
    out[0] = order == REGISTER_HIGH_WORD_FIRST ? high : low;
    out[1] = order == REGISTER_HIGH_WORD_FIRST ? low : high;
}

bool register_map_build(const RcSnapshot *snapshot, const RcStatus *input_status,
                        RegisterWordOrder order,
                        const RegisterDiagnostics *diagnostics,
                        uint16_t out[BRIDGE_REGISTER_COUNT])
{
    if (snapshot == NULL || input_status == NULL || diagnostics == NULL || out == NULL ||
        (order != REGISTER_HIGH_WORD_FIRST && order != REGISTER_LOW_WORD_FIRST)) return false;
    const RcStatus status = *input_status;
    uint16_t registers[BRIDGE_REGISTER_COUNT] = {0};
    registers[0] = BRIDGE_PROTOCOL_VERSION;
    registers[1] = (uint16_t)snapshot->sequence;
    if (status.usable) registers[2] |= RC_REG_USABLE;
    if (status.frame_lost) registers[2] |= RC_REG_FRAME_LOST;
    if (status.failsafe) registers[2] |= RC_REG_FAILSAFE;
    if (status.timed_out) registers[2] |= RC_REG_TIMEOUT;
    if (status.has_frame && !status.transport_ok) registers[2] |= RC_REG_TRANSPORT_FAULT;
    if (!status.has_frame) registers[2] |= RC_REG_NO_FRAME;
    if (!status.timeout_configured) registers[2] |= RC_REG_UNCONFIGURED;
    if (diagnostics->comm_fault) registers[2] |= RC_REG_COMM_FAULT;
    registers[3] = status.age_ms > UINT16_MAX ? UINT16_MAX : (uint16_t)status.age_ms;
    for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; ++i)
        registers[4U + i] = snapshot->frame.channels[i];
    split_counter(&registers[20], diagnostics->valid_sbus_frames, order);
    split_counter(&registers[22], diagnostics->rejected_candidates, order);
    split_counter(&registers[24], diagnostics->modbus_requests, order);
    registers[26] = diagnostics->firmware_version;
    memcpy(out, registers, sizeof(registers));
    return true;
}
