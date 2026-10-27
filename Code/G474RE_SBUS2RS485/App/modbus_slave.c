#include "modbus_slave.h"
#include "modbus_crc.h"
#include <string.h>

bool modbus_slave_init(ModbusSlave *slave, uint8_t address, uint16_t register_base)
{
    if (slave == NULL) return false;
    memset(slave, 0, sizeof(*slave));
    if (address == 0U || address > 247U ||
        (uint32_t)register_base + BRIDGE_REGISTER_COUNT > 65536UL) return false;
    slave->address = address;
    slave->register_base = register_base;
    slave->initialized = true;
    return true;
}

static size_t finish(uint8_t *response, size_t length)
{
    const uint16_t crc = modbus_crc16(response, length);
    response[length] = (uint8_t)crc;
    response[length + 1U] = (uint8_t)(crc >> 8U);
    return length + 2U;
}

size_t modbus_slave_handle(ModbusSlave *slave, const uint8_t *request, size_t length,
                           const uint16_t registers[BRIDGE_REGISTER_COUNT],
                           uint8_t *response, size_t capacity)
{
    if (slave == NULL || !slave->initialized || request == NULL ||
        registers == NULL || response == NULL || length < 4U || length > MODBUS_ADU_MAX) return 0U;
    if (!modbus_crc_valid(request, length))
    {
        ++slave->stats.crc_errors;
        return 0U;
    }
    if (request[0] != slave->address || (request[1] & 0x80U) != 0U)
    {
        ++slave->stats.ignored_frames;
        return 0U;
    }
    ++slave->stats.requests;
    uint8_t exception = 0U;
    uint16_t offset = 0U, quantity = 0U;
    if (request[1] != 3U) exception = 1U;
    else if (length != 8U) exception = 3U;
    else
    {
        const uint16_t address = (uint16_t)(((uint16_t)request[2] << 8U) | request[3]);
        quantity = (uint16_t)(((uint16_t)request[4] << 8U) | request[5]);
        if (quantity == 0U || quantity > 125U) exception = 3U;
        else if (address < slave->register_base ||
                 (uint32_t)address + quantity > (uint32_t)slave->register_base + BRIDGE_REGISTER_COUNT)
            exception = 2U;
        else offset = (uint16_t)(address - slave->register_base);
    }
    const size_t needed = exception != 0U ? 5U : 5U + (size_t)quantity * 2U;
    if (capacity < needed)
    {
        ++slave->stats.internal_errors;
        return 0U;
    }
    response[0] = slave->address;
    if (exception != 0U)
    {
        ++slave->stats.exceptions;
        response[1] = (uint8_t)(request[1] | 0x80U);
        response[2] = exception;
        return finish(response, 3U);
    }
    response[1] = 3U;
    response[2] = (uint8_t)(quantity * 2U);
    for (uint16_t i = 0U; i < quantity; ++i)
    {
        const uint16_t value = registers[offset + i];
        response[3U + (size_t)i * 2U] = (uint8_t)(value >> 8U);
        response[4U + (size_t)i * 2U] = (uint8_t)value;
    }
    return finish(response, needed - 2U);
}
