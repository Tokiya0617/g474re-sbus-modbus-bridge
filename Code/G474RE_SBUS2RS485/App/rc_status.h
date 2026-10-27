#ifndef RC_STATUS_H
#define RC_STATUS_H

#include "rc_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一份快照在指定时刻的判断结果；不是 Modbus 状态位布局。 */
typedef struct
{
    uint32_t age_ms;          /* 帧年龄，单位 ms；无帧时为 UINT32_MAX，不是 0。 */
    bool has_frame;           /* 是否已经发布过帧。 */
    bool transport_ok;        /* 有帧，且发布后没有检测到接收故障。 */
    bool frame_lost;          /* 有帧时原样报告 SBUS 丢帧标志。 */
    bool failsafe;            /* 有帧时原样报告 SBUS 失控保护标志。 */
    bool timeout_configured;  /* 调用者传入了允许范围内的超时阈值。 */
    bool timed_out;           /* 有帧且阈值已配置时，age_ms >= timeout_ms。 */
    bool usable;              /* 以上数据条件全部通过；不代表授权车辆动作。 */
} RcStatus;

/* 只计算，不读写共享快照、不访问 HAL/RTOS、不锁存状态，也不修改通道值。
 * snapshot 须为 rc_snapshot_read() 得到的稳定副本，调用期间不得并发修改。
 * now_ms 在读取副本之后取值，与 received_ms 使用同一 uint32_t 毫秒时基。
 * 无符号减法允许跨一次计数回绕，但真实帧年龄必须小于 2^32 ms；超过
 * 一个完整计数周期无法仅靠两个时间戳识别，系统须另行保证检查/历史管理。
 *
 * timeout_ms 必须由调用者依据获批配置提供：允许 1..INT32_MAX ms。
 * 0 或更大值表示配置不可用，timeout_configured/usable 均为 false；
 * 此时 timed_out 为 false（无法判断），不表示数据新鲜。不提供默认阈值。
 * TODO(待确认)：正式阈值须经 GR01 实测及系统安全评审，不使用测试值代替。
 *
 * usable 要求有帧、传输正常、未报告 frame_lost/failsafe、已配置且未超时。
 * 这是保守的数据筛选条件，不校验物理通道范围，不取代 PLC 安全联锁。
 * 无帧时 age_ms=UINT32_MAX，transport_ok/frame_lost/failsafe/timed_out 为 false。
 * true 仅表示结果已写入；任一指针为 NULL 返回 false，out 保持不变。
 * out 须为调用者独立可写空间，不与 snapshot 重叠或被并发访问。
 */
bool rc_status_evaluate(const RcSnapshot *snapshot, uint32_t now_ms,
                        uint32_t timeout_ms, RcStatus *out);

#ifdef __cplusplus
}
#endif

#endif /* RC_STATUS_H */
