#ifndef __SBUS_DECODER_H__
#define __SBUS_DECODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SBUS_FRAME_SIZE       25U               // 完整帧长度 25 Bytes
#define SBUS_CHANNEL_COUNT    16U               // 通道数 16
#define SBUS_CHANNEL_BITS     11U               // 每个通道占用 11 bits

typedef struct 
{
  uint16_t channels[SBUS_CHANNEL_COUNT];

  bool frame_lost;    // 接收帧报告的丢帧状态
  bool failsafe;      // 接收帧报告的失控保护状态
} SbusFrame;

/* 单帧候选格式：25 字节，0x0F 帧头、0x00 帧尾、状态高四位为零。
 * TODO(待实测确认)：上述限制和状态位解释尚未经 GR01 实物验证。
 * data 必须指向 length 字节可读且调用期间不变的数据。
 * 成功写入 out；失败不修改 out。true 只表示格式符合候选实现，
 * 不保证数据可用于控制，也不提供 CRC、UART 错误或时效检查。
 */
bool sbus_decode_frame(const uint8_t *data, size_t length, SbusFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* __SBUS_DECODER_H__ */

