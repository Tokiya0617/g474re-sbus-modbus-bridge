#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H
#include "register_map.h"
#include <stddef.h>
#define MODBUS_ADU_MAX 256U

typedef struct
{
    uint32_t requests, exceptions, crc_errors, ignored_frames, internal_errors;
} ModbusSlaveStats;
typedef struct
{
    uint8_t address;
    uint16_t register_base;
    bool initialized;
    ModbusSlaveStats stats;
} ModbusSlave;

/* 唯一任务使用，无共享数据/HAL/动态内存。地址 1..247，base 为线上 0 基地址。
 * 只支持 03 读保持寄存器；写操作统一异常 01，无远程配置或车辆动作入口。
 * 广播、其他地址、坏 CRC、异常响应形式的功能码均不回复。 */
bool modbus_slave_init(ModbusSlave *slave, uint8_t address, uint16_t register_base);
/* 在收到完整 RTU ADU 后调用；registers 是为本次事务构造的一致寄存器副本。
 * 返回应答长度，0 表示不回复。response 容量不足/参数错误不写越界。
 * request、response 和 registers 不得重叠。返回应答不等于已在物理线上发送。 */
size_t modbus_slave_handle(ModbusSlave *slave, const uint8_t *request, size_t length,
                           const uint16_t registers[BRIDGE_REGISTER_COUNT],
                           uint8_t *response, size_t capacity);
#endif
