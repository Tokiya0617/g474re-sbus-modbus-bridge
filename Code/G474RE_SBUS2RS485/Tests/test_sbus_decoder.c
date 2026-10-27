/* Host-only tests. Do not add this main() to the STM32 firmware target. */
#include "sbus_decoder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent bit-by-bit encoder; does not use the decoder's accumulator. */
/**
 * @brief 打包 SBUS 数据
 * 
 * @param channels[16]     输入的 16 个通道值
 * @param frame[25]        打包完成的 25 Bytes SBUS Frame 数据
 * 
 * @return static void     仅当前文件可用
 * 
 * @note 
 */
static void pack_frame(const uint16_t channels[16], uint8_t frame[25])
{
    memset(frame, 0, 25U);                  // 清零 frame
    frame[0] = 0x0FU;                       // 写入帧头

    for (size_t ch = 0; ch < 16U; ++ch)
    {
        for (size_t bit = 0; bit < 11U; ++bit)
        {
            const size_t offset = ch * 11U + bit;   // 当前通道在 176 bit 中的位置

            // 先检查第 ch 个通道里的第 bit 位是否为 1
            if ((channels[ch] & (1U << bit)) != 0U)
            {
                // offset / 8 + 1 表示应该放在哪个字节
                // offset % 8 表示应该放在当前字节的哪个 bit
                frame[1U + offset / 8U] |= (uint8_t)(1U << (offset % 8U));
            }
        }
    }
}

/**
 * @brief 打包、解码、比较
 * 
 * @param channels[16]
 * 
 * @return NULL，检查失败时由 assert() 报错
 * 
 * @note 参数里的数组并不是复制进来的；这里实际通过指针访问调用者的数组
 */
static void check_channels(const uint16_t channels[16])
{
    uint8_t frame[25];
    SbusFrame out;
    pack_frame(channels, frame);                // 将期望通道值编码成模拟帧
    
    // 调用解码函数，将结果写进 out，并检查返回值是不是 true
    assert(sbus_decode_frame(frame, sizeof(frame), &out));

    // 逐个比较，解码出来的 out.channels[ch] 是否等于原始的 channels[ch]
    for (size_t ch = 0; ch < 16U; ++ch)
        assert(out.channels[ch] == channels[ch]);

    // 这里生成的模拟帧没有设置异常标志，所以 failsafe 和 frame_lost 都应该为 false
    assert(!out.frame_lost && !out.failsafe);
}

/**
 * @brief SBUS 错误检测
 * 
 * @param frame 
 * @param length 
 * 
 * @return static void 
 * 
 * @note 
 */
static void check_rejected(const uint8_t *frame, size_t length)
{
    // 给输出结构放一份已有数据
    SbusFrame out = {0};
    unsigned char before[sizeof(out)];
    out.channels[0] = 1234U;
    out.frame_lost = true;
    out.failsafe = true;

    memcpy(before, &out, sizeof(out));                  // 备份内存
    assert(!sbus_decode_frame(frame, length, &out));    // 调用 decoder，! 取反返回 false
    assert(memcmp(before, &out, sizeof(out)) == 0);     // 检查 out 和 before 是否相同，相同就返回 0
}

int main(void)
{
    uint16_t channels[16] = {0};            // 期望通道值
    uint8_t frame[26] = {0};                // 准备交给解码器的字节数据
    SbusFrame out;                          // 接收解码结果

    check_channels(channels);               // 检查 16 全为 0 时，能不能正确解码

    // 检查 16 个通道全为最大值 2047 时，能否正确解码
    for (size_t ch = 0; ch < 16U; ++ch) channels[ch] = 2047U;
    check_channels(channels);

    // 检查各通道使用不同的值，能否正确解码
    for (size_t ch = 0; ch < 16U; ++ch) channels[ch] = (uint16_t)(ch * 127U);
    check_channels(channels);

    /* Exhaust every value of every channel; other channels remain zero. */
    // 每个通道分别遍历所有值进行测试，其他通道保持 0，共 32768 次
    for (size_t ch = 0; ch < 16U; ++ch)
    {
        memset(channels, 0, sizeof(channels));
        for (uint16_t value = 0; value < 2048U; ++value)
        {
            channels[ch] = value;
            check_channels(channels);
        }
    }

    /* Literal vector: CH1=1, CH2=2, CH16=1024. */
    // 固定字节测试，不依赖打包函数
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x0FU;
    frame[1] = 0x01U;
    frame[2] = 0x10U;
    frame[22] = 0x80U;

    assert(sbus_decode_frame(frame, 25U, &out));
    assert(out.channels[0] == 1U && out.channels[1] == 2U);
    for (size_t ch = 2; ch < 15U; ++ch) assert(out.channels[ch] == 0U);
    assert(out.channels[15] == 1024U);

    // 状态字节测试
    for (unsigned flags = 0; flags < 256U; ++flags)                 // 0~255
    {
        frame[23] = (uint8_t)flags;

        if (flags >= 16U) check_rejected(frame, 25U);               // 高四位要求为 0
        else
        {
            assert(sbus_decode_frame(frame, 25U, &out));
            assert(out.frame_lost == ((flags & 4U) != 0U));         // 0100，frame_lost mask
            assert(out.failsafe == ((flags & 8U) != 0U));           // 1000, failsafe mask
            assert(out.channels[15] == 1024U);
        }
    }

    // 长度、空指针、帧头和帧尾测试
    frame[23] = 0U;                                                 // 恢复正常状态字节

    // 检查 0~26 中，除了 25 以外的长度全部被拒绝
    for (size_t length = 0; length <= 26U; ++length)
        if (length != 25U) check_rejected(frame, length);
    
    // 检查极大长度也被拒绝
    check_rejected(frame, (size_t)-1);
    
    // 检查输入数据指针为空
    check_rejected(NULL, 25U);
    
    // 检查输出结构指针为空
    assert(!sbus_decode_frame(frame, 25U, NULL));

    // 错误帧头
    for (unsigned value = 0; value < 256U; ++value)
    {
        frame[0] = (uint8_t)value;
        if (value != 0x0FU) check_rejected(frame, 25U);
    }

    // 错误帧尾
    frame[0] = 0x0FU;
    for (unsigned value = 1; value < 256U; ++value)
    {
        frame[24] = (uint8_t)value;
        check_rejected(frame, 25U);
    }

    // 全部通过，才打印结果
    puts("PASS: 32768 channel cases, boundaries, flags, headers/footers, unchanged output");
    
    return 0;
}
