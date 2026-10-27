#include "modbus_crc.h"

uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0U) return 0U;
    uint16_t crc = UINT16_MAX;
    for (size_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0U; bit < 8U; ++bit)
        {
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) != 0U ? 0xA001U : 0U));
        }
    }
    return crc;
}

bool modbus_crc_valid(const uint8_t *adu, size_t length)
{
    return adu != NULL && length >= 4U && length <= 256U &&
           modbus_crc16(adu, length) == 0U;
}
