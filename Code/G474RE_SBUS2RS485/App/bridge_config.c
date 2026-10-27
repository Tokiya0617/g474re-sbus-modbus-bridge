#include "bridge_config.h"
#include "modbus_rtu.h"

/* TODO(待确认)：PLC 端口/参数、从站地址、寄存器基址/字序、收发器 DE 和阈值。
 * 未确认的字段保持 0/UNSET，不把现有 CubeMX 候选值自动视为批准值。
 * enabled=false 时不启动 USART3 接收，不驱动 DE 发应答。 */
static const BridgeConfig s_config = {0};

const BridgeConfig *bridge_config_get(void) { return &s_config; }

bool bridge_config_valid(const BridgeConfig *config)
{
    if (config == NULL || !config->enabled || !config->approved ||
        config->slave_address == 0U || config->slave_address > 247U ||
        (uint32_t)config->register_base + BRIDGE_REGISTER_COUNT > 65536UL ||
        (config->word_order != REGISTER_HIGH_WORD_FIRST && config->word_order != REGISTER_LOW_WORD_FIRST) ||
        config->sbus_timeout_ms == 0U || config->sbus_timeout_ms > (uint32_t)INT32_MAX ||
        config->plc_timeout_ms == 0U || config->plc_timeout_ms > (uint32_t)INT32_MAX ||
        config->tx_timeout_ms == 0U || config->tx_timeout_ms > (uint32_t)INT32_MAX ||
        config->de_setup_us > 1000000U) return false;
    const bool parity = config->parity == BRIDGE_PARITY_EVEN || config->parity == BRIDGE_PARITY_ODD;
    if (!((parity && config->stop_bits == 1U) ||
          (config->parity == BRIDGE_PARITY_NONE && config->stop_bits == 2U))) return false;
    ModbusRtuTiming timing;
    if (!modbus_rtu_timing(config->baud_rate, &timing)) return false;
    /* 28 寄存器最大应答 61 字节；留余量由获批超时保证，不另造链路参数。 */
    const uint32_t minimum_ms = (timing.character_us * 61U + config->de_setup_us + 999U) / 1000U;
    return config->tx_timeout_ms > minimum_ms;
}
