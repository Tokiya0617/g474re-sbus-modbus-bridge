#ifndef MODBUS_CRC_H
#define MODBUS_CRC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* CRC-16/MODBUS: 初值 FFFF，反射多项式 A001，线上低字节先发。
 * length>0 时 data 必须可读；NULL 非空输入返回 0，不作为有效校验结果。 */
uint16_t modbus_crc16(const uint8_t *data, size_t length);
bool modbus_crc_valid(const uint8_t *adu, size_t length);
#endif
