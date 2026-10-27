#ifndef BRIDGE_CONFIG_H
#define BRIDGE_CONFIG_H
#include "register_map.h"
typedef enum { BRIDGE_PARITY_UNSET, BRIDGE_PARITY_EVEN,
               BRIDGE_PARITY_ODD, BRIDGE_PARITY_NONE } BridgeParity;
typedef struct
{
    bool enabled;
    bool approved; /* 人工确认接线/PLC/协议/阈值后才设 true，不是功能解锁口令。 */
    uint8_t slave_address;
    uint16_t register_base; /* Modbus PDU 的 0 基地址，不直接写 40001。 */
    RegisterWordOrder word_order;
    uint32_t baud_rate;
    BridgeParity parity;
    uint8_t stop_bits;
    bool de_active_high;
    uint32_t de_setup_us; /* 收发器使能后到开始发字节的最小等待。0 须经数据手册确认。 */
    uint32_t tx_timeout_ms; /* 包括 DE 准备/发送的总超时，须大于最大全帧发送时间。 */
    uint32_t sbus_timeout_ms; /* 获批失联阈值，不提供默认值。 */
    uint32_t plc_timeout_ms; /* 获批主站轮询失联诊断阈值，不决定车辆动作。 */
    uint16_t firmware_version;
} BridgeConfig;

const BridgeConfig *bridge_config_get(void);
bool bridge_config_valid(const BridgeConfig *config);
#endif
