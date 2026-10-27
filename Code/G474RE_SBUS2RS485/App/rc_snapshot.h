#ifndef RC_SNAPSHOT_H
#define RC_SNAPSHOT_H

#include "sbus_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
    SbusFrame frame;       /* 最新帧，包括丢帧/失控保护状态。 */
    uint32_t sequence;     /* 每发布一帧加 1，UINT32_MAX 后自然回绕到 0。 */
    uint32_t received_ms;  /* 接收时间，单位 ms，不是数据年龄。 */
    bool has_frame;        /* 发布过帧，不等于当前可安全用于控制。 */
    bool transport_ok;     /* 发布后未检测到接收故障；仍须检查状态位和年龄。 */
} RcSnapshot;

/* 启动阶段初始化全部字段，在生产者/消费者运行前调用。 */
void rc_snapshot_init(void);

/* 单一生产者按接收顺序发布已解码的帧；frame 在调用期间必须稳定，
 * 不得被 DMA 或其他上下文并发修改。本函数不重新检查协议/范围。
 * received_ms 与后续年龄计算使用同一毫秒时基，允许计数回绕。
 * true 表示复制成功；NULL 返回 false，保持快照不变。
 */
bool rc_snapshot_publish(const SbusFrame *frame, uint32_t received_ms);

/* 已知 UART 错误/字节丢失时立即失效；保留历史帧、序号及时间戳。
 * 不代表遥控器失联超时，也不决定车辆动作。下一次 publish 恢复此标志。
 */
void rc_snapshot_invalidate(void);

/* 复制一致快照到调用者独立空间；NULL 返回 false。
 * 尚无帧时仍返回 true，out->has_frame 为 false。
 * 调用者须检查协议状态和时效；本模块不决定超时阈值或车辆安全值。
 * 同一个 out 不得被其他任务/中断/DMA 并发访问。
 */
bool rc_snapshot_read(RcSnapshot *out);

/* 并发约束：用于单核 STM32G474 的特权任务和普通可屏蔽中断。
 * PRIMASK 短临界区保护全部共享访问，并恢复原中断状态。
 * 禁止从 NMI、HardFault 等故障处理程序访问；不保护 DMA/多核访问。
 * 无需用 volatile 代替同步。上板后仍需验证中断延迟。
 */

#ifdef __cplusplus
}
#endif

#endif /* RC_SNAPSHOT_H */

