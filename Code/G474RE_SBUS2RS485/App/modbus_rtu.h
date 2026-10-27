#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H
#include "modbus_slave.h"

typedef struct { uint32_t character_us, t1_5_us, t3_5_us; } ModbusRtuTiming;
typedef struct
{
    uint8_t bytes[MODBUS_ADU_MAX];
    size_t length;
    uint32_t last_end_us;
    ModbusRtuTiming timing;
    uint32_t gap_errors, overflows, skipped_frames;
    bool initialized, discarding;
} ModbusRtu;

/* 11 位字符格式（8E1/8O1/8N2）。低波特率按字符计算，>19200 用规范固定间隔。
 * end_us 是字节接收结束的观察时间；两次 end 的差需减去一个字符，才近似静默。
 * 时间戳非硬件捕获；实际 IRQ 抖动必须实测，不能声称精确检测所有边界。
 * 每个对象唯一任务操作，时间间隔小于 2^32 us；无 IDLE=帧边界假设。 */
bool modbus_rtu_timing(uint32_t baud, ModbusRtuTiming *out);
bool modbus_rtu_init(ModbusRtu *rtu, uint32_t baud, uint32_t now_us);
void modbus_rtu_discard(ModbusRtu *rtu, uint32_t now_us);
void modbus_rtu_push(ModbusRtu *rtu, uint8_t byte, uint32_t end_us);
/* 只在原始接收队列已排空、硬件没有正在接收字符时调用。
 * 有足够静默才复制帧；坏帧/容量不足丢弃，length 返回 0，不解析 CRC。 */
bool modbus_rtu_poll(ModbusRtu *rtu, uint32_t now_us, bool bus_idle,
                      uint8_t *out, size_t capacity, size_t *length);
#endif
