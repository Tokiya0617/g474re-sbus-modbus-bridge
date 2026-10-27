#include "sbus_receiver.h"
#include <string.h>

#define SBUS_RECEIVER_HEADER 0x0FU

void sbus_receiver_init(SbusReceiver *receiver)
{
    if (receiver == NULL)
    {
        return;
    }

    /* 清除旧字节便于调试；真正决定有效数据范围的是 length。 */
    memset(receiver->buffer, 0, sizeof(receiver->buffer));
    receiver->length = 0U;
}

bool sbus_receiver_push_byte(SbusReceiver *receiver, uint8_t byte, SbusFrame *out)
{
    /* 参数错误时不消费输入，也不修改已有接收状态和输出。 */
    if ((receiver == NULL) || (out == NULL))
    {
        return false;
    }

    /* 正常调用之间 length 一定小于帧长。防御异常计数，避免写越界。
     * 这不能替代初始化，也不能修复其他内存破坏。
     */
    if (receiver->length >= SBUS_FRAME_SIZE)
    {
        sbus_receiver_init(receiver);
    }

    /* 尚未开始收帧时，只接受候选帧头，其余字节直接跳过。 */
    if ((receiver->length == 0U) && (byte != SBUS_RECEIVER_HEADER))
    {
        return false;
    }

    /* 收帧过程中即使遇到 0x0F 也照常保存：它可能是通道数据。 */
    receiver->buffer[receiver->length] = byte;
    ++receiver->length;

    // 没收满 25 字节时，返回 false，重新进行下一个字节收取
    if (receiver->length < SBUS_FRAME_SIZE)
    {
        return false;
    }

    /* 收齐后再检查格式。decoder 失败时保证 out 不变。 */
    if (sbus_decode_frame(receiver->buffer, receiver->length, out))
    {
        receiver->length = 0U;
        return true;
    }

    /* 当前帧头已经被证明不合适，从下标 1 开始找下一个候选帧头。
     * 保留最早的候选及其后所有字节，避免丢掉已经收到的下一帧开头。
     * 若它仍是假帧头，补齐后会再次检查并继续向后寻找。
     */
    size_t next_header = 1U;
    while ((next_header < receiver->length) &&
           (receiver->buffer[next_header] != SBUS_RECEIVER_HEADER))
    {
        ++next_header;
    }

    if (next_header < receiver->length)
    {
        const size_t remaining = receiver->length - next_header;
        /* 源和目标范围可能重叠，必须使用 memmove，而不是 memcpy。 */
        memmove(receiver->buffer, &receiver->buffer[next_header], remaining);
        receiver->length = remaining;
    }
    else
    {
        receiver->length = 0U;
    }

    return false;
}

