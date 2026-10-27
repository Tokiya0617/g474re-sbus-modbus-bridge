#include "rc_status.h"

#include <stddef.h>

bool rc_status_evaluate(const RcSnapshot *snapshot, uint32_t now_ms,
                        uint32_t timeout_ms, RcStatus *out)
{
    if (snapshot == NULL || out == NULL)
    {
        return false;
    }

    RcStatus status = {0};
    status.age_ms = UINT32_MAX;
    status.has_frame = snapshot->has_frame;
    status.timeout_configured = timeout_ms != 0U &&
                                timeout_ms <= (uint32_t)INT32_MAX;

    /* 未收到第一帧时，不把零初始化的通道或时间解释成新鲜遥控数据。 */
    if (status.has_frame)
    {
        status.age_ms = (uint32_t)(now_ms - snapshot->received_ms);
        status.transport_ok = snapshot->transport_ok;
        status.frame_lost = snapshot->frame.frame_lost;
        status.failsafe = snapshot->frame.failsafe;
        status.timed_out = status.timeout_configured &&
                           status.age_ms >= timeout_ms;

        /* 新帧可以恢复判断结果；UART 重启本身不会恢复 transport_ok。 */
        status.usable = status.transport_ok && !status.frame_lost &&
                        !status.failsafe && status.timeout_configured &&
                        !status.timed_out;
    }

    *out = status;
    return true;
}
