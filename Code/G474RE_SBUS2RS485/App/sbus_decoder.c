#include "sbus_decoder.h"

/* TODO(待实测确认)：这是离线测试使用的普通 SBUS 候选格式，
 * 不是已经确认的 GR01 规格。暂不接收 SBUS2 等非 0x00 帧尾变体。
 * 布局：帧头 1 字节 + 通道数据 22 字节 + 状态 1 字节 + 帧尾 1 字节。
 * 格式参考：https://github.com/bolderflight/sbus （不代表 GR01 厂商资料）。
 */
#define SBUS_HEADER           0x0FU             // 帧头值
#define SBUS_FOOTER           0x00U             // 帧尾值
#define SBUS_FLAGS_INDEX      23U               // 状态字节下标为 23
#define SBUS_CHANNEL_MASK     0x07FFU           // 保留低 11 位
#define SBUS_FRAME_LOST_MASK  0x04U             // 0000 0100，检查 bit 2 用
#define SBUS_FAILSAFE_MASK    0x08U             // 0000 1000，检查 bit 3 用
#define SBUS_RESERVED_MASK    0xF0U             // 1111 0000，检查高 4 位用

/**
 * @brief 解码一帧完整数据。
 * 
 * @param data    输入数据，只读；应为 UART 接收后的字节，不是原始电平
 * @param length  输入字节数
 * @param out     调用者提供的结果存储空间
 * 
 * @return 若为 true：帧格式符合当前实现，结果已写入 out；
 *         若为 false：参数或帧格式无效，out 保持不变。
 * 
 * @note true 不代表遥控数据安全可使用，仍需检查状态和时效。
 *       本函数不检查 UART 奇偶校验/帧错误，也不提供 CRC 校验。
 *       调用期间，调用者必须保证输入完整且不会被 DMA 等并发改写。
 */
bool sbus_decode_frame(const uint8_t *data, size_t length, SbusFrame *out)
{
  // 先检查长度和指针，再访问数组，避免短帧造成越界。
  if ((data == NULL) || (out == NULL) || (length != SBUS_FRAME_SIZE))
  {
    return false;
  }

  const uint8_t flags = data[SBUS_FLAGS_INDEX];           // 取出状态字节 Byte 24

  if ((data[0] != SBUS_HEADER) ||                         // 帧头检查
      (data[SBUS_FRAME_SIZE - 1U] != SBUS_FOOTER) ||      // 帧尾检查
      ((flags & SBUS_RESERVED_MASK) != 0U))               // 状态字节高 4 位需要全为 0
  {
    return false;
  }

  /* 局部结果全部完成后再赋值，失败时保持 *out 不变。
  * 这不是跨任务原子发布；共享快照的同步由后续上层实现。
  */
  SbusFrame decoded = {0};                    // 暂存解码结果的本地结构体
  uint32_t bit_buffer = 0U;                   // 暂存还没有被读取的数据
  uint32_t bits_available = 0U;               // 暂时可用的位数
  size_t byte_index = 1U;                     // 接下来要读取哪个输入字节

  for (size_t channel = 0U; channel < SBUS_CHANNEL_COUNT; ++channel)      // 0~15
  {
    /* 通道按低位在前连续排列。攒够 11 位后取出一个通道。
    * 例如 CH1 用 byte[1] 的 8 位和 byte[2] 的低 3 位；
    * byte[2] 剩下的 5 位继续用于 CH2，不丢弃。
    */
    while (bits_available < SBUS_CHANNEL_BITS)                          // 0~10
    {
      /* 只读 byte[1..22]，不能把状态字节拼进通道数据。 */
      if (byte_index >= SBUS_FLAGS_INDEX)
      {
        return false;
      }

      bit_buffer |= (uint32_t)data[byte_index] << bits_available;       // 拼接数据
      ++byte_index;                                                     // 字节标 ++
      bits_available += 8U;                                             // 更新可用位数
    }

    // 将 bit_buffer 中暂存的数据取到 channels[channel] 中
    // & SBUS_CHANNEL_MASK 操作是保留低 11 位
    decoded.channels[channel] = (uint16_t)(bit_buffer & SBUS_CHANNEL_MASK);
    bit_buffer >>= SBUS_CHANNEL_BITS;         // 丢弃低 11 位，保留剩余数据
    bits_available -= SBUS_CHANNEL_BITS;      // 更新有效数据位数，减去 11 位
  }

  /* 位 0/1 是额外数字通道，本模块仅提供 CH1..CH16，所以忽略。
  * failsafe/丢帧是帧中的状态，不是帧结构损坏，应返回给调用者。
  */
  decoded.frame_lost = (flags & SBUS_FRAME_LOST_MASK) != 0U;
  decoded.failsafe = (flags & SBUS_FAILSAFE_MASK) != 0U;

  *out = decoded;
  return true;
}

