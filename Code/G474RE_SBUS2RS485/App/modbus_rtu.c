#include "modbus_rtu.h"
#include <string.h>

bool modbus_rtu_timing(uint32_t baud, ModbusRtuTiming *out)
{
    if (out == NULL || baud < 1200U || baud > 1000000U) return false;
    ModbusRtuTiming timing;
    timing.character_us = (11000000UL + baud - 1U) / baud;
    timing.t1_5_us = baud > 19200U ? 750U : (16500000UL + baud - 1U) / baud;
    timing.t3_5_us = baud > 19200U ? 1750U : (38500000UL + baud - 1U) / baud;
    *out = timing;
    return true;
}

bool modbus_rtu_init(ModbusRtu *rtu, uint32_t baud, uint32_t now_us)
{
    if (rtu == NULL) return false;
    memset(rtu, 0, sizeof(*rtu));
    if (!modbus_rtu_timing(baud, &rtu->timing)) return false;
    rtu->initialized = true;
    modbus_rtu_discard(rtu, now_us);
    return true;
}

void modbus_rtu_discard(ModbusRtu *rtu, uint32_t now_us)
{
    if (rtu == NULL || !rtu->initialized) return;
    rtu->length = 0U;
    rtu->last_end_us = now_us;
    rtu->discarding = true; /* 错误/启动后须先看到完整静默，不拼接半帧。 */
}

void modbus_rtu_push(ModbusRtu *rtu, uint8_t byte, uint32_t end_us)
{
    if (rtu == NULL || !rtu->initialized) return;
    const uint32_t elapsed = end_us - rtu->last_end_us;
    const uint32_t silence = elapsed > rtu->timing.character_us ?
                             elapsed - rtu->timing.character_us : 0U;
    if (silence >= rtu->timing.t3_5_us)
    {
        /* 任务迟到：不排队旧请求的应答，以免撞上已开始的新事务。 */
        if (rtu->length != 0U && !rtu->discarding) ++rtu->skipped_frames;
        rtu->length = 0U;
        rtu->discarding = false;
    }
    else if (rtu->length != 0U && silence > rtu->timing.t1_5_us)
    {
        if (!rtu->discarding) ++rtu->gap_errors;
        rtu->discarding = true;
    }
    rtu->last_end_us = end_us;
    if (rtu->discarding) return;
    if (rtu->length >= sizeof(rtu->bytes))
    {
        ++rtu->overflows;
        rtu->discarding = true;
        return;
    }
    rtu->bytes[rtu->length++] = byte;
}

bool modbus_rtu_poll(ModbusRtu *rtu, uint32_t now_us, bool bus_idle,
                      uint8_t *out, size_t capacity, size_t *length)
{
    if (length == NULL) return false;
    *length = 0U;
    if (rtu == NULL || !rtu->initialized || out == NULL || !bus_idle ||
        (uint32_t)(now_us - rtu->last_end_us) < rtu->timing.t3_5_us) return false;
    const bool ready = !rtu->discarding && rtu->length >= 4U && rtu->length <= capacity;
    if (ready)
    {
        memcpy(out, rtu->bytes, rtu->length);
        *length = rtu->length;
    }
    rtu->length = 0U;
    rtu->discarding = false;
    return ready;
}
