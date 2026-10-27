#ifndef SBUS_RECEIVER_H
#define SBUS_RECEIVER_H

#include "sbus_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一个字节流对应一个接收状态；必须先初始化，不能由多个上下文并发操作。
 * 成员由 receiver 模块维护，调用者不要自行修改。
 */
typedef struct
{
    uint8_t buffer[SBUS_FRAME_SIZE]; /* 尚在收集的候选帧，不是待发送帧队列。 */
    size_t length;                  /* 已保存的字节数；0 表示正在寻找帧头。 */
} SbusReceiver;

/**
 * @brief 清零帧
 * 
 * @param receiver SbusReceiver 结构体，包含 25 字节 SBUS 帧和 length
 * 
 * @return NULL
 * 
 * @note 数据流发生已知中断（例如 UART 错误、接收溢出）后由调用者重置。
 *       本模块不决定超时阈值，也不负责共享快照的失效处理
 */
void sbus_receiver_init(SbusReceiver *receiver);

/**
 * @brief 逐字节接收，并使用 decoder 模块检测，成功就保存，失败就重试
 * 
 * @param receiver 接收字节的 SbusReceiver 结构体
 * @param byte 要传入的字节
 * @param out 调用者提供的 SbusFrame 结构体，存放帧数据使用
 * 
 * @return bool 
 *         true：本次完成一帧拼接，并将结果解析后存入 out
 *         false：尚未收齐 25 Bytes 或帧检查不通过或参数为空，不修改 out
 * 
 * @note 任一指针为 NULL 时不消费字节、不修改 receiver。
 *       out 必须独立于 receiver 的存储区域。
 *       已初始化对象若 length 异常地达到或超过帧长，丢弃旧状态，
 *       再把本次 byte 作为可能的新帧头处理；这不替代首次初始化。
 *       true 仅表示通过 decoder 的候选格式检查，不代表可安全用于控制；
 *       调用者仍须检查 frame_lost、failsafe 和时效。无 CRC，不能排除假帧。
 */
bool sbus_receiver_push_byte(SbusReceiver *receiver, uint8_t byte, SbusFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* SBUS_RECEIVER_H */
