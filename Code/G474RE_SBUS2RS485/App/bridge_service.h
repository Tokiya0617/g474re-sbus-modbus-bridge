#ifndef BRIDGE_SERVICE_H
#define BRIDGE_SERVICE_H
#include "rs485_port.h"
#include "modbus_slave.h"
#include "sbus_uart.h"

typedef enum
{
    BRIDGE_DISABLED, BRIDGE_STARTUP, BRIDGE_ACTIVE, BRIDGE_SBUS_LOST,
    BRIDGE_SBUS_FAILSAFE, BRIDGE_INPUT_FAULT, BRIDGE_COMM_FAULT, BRIDGE_CONFIG_FAULT
} BridgeState;
typedef struct
{
    BridgeState state;
    RcSnapshot snapshot;
    RcStatus input;
    SbusUartStats sbus;
    Rs485PortStats port;
    ModbusSlaveStats modbus;
    uint32_t rtu_gap_errors, rtu_overflows;
    uint32_t skipped_requests; /* 迟到而跳过的 RTU 候选，尚未保证 CRC/本站地址。 */
    uint32_t discarded_responses;
    uint32_t last_request_ms;
    bool have_request, comm_fault, enabled;
} BridgeDiagnostics;

/* 调度器启动后由唯一 Modbus 任务调用一次。禁用配置不触碰 USART3/DE，返回 DISABLED。
 * process 至少每 1 ms（当前任务等待上限）或线程标志到来时调用，不能永久等待通知。
 * 只有任务进行 RTU/CRC/映射/应答；无需新建任务、发送队列或动态内存。
 * 诊断只报告，不替代 PLC 安全动作；配置无效/端口启动失败不自动启用或无限重试。 */
BridgeState bridge_service_start(const BridgeConfig *config);
void bridge_service_process(void);
bool bridge_service_get_diagnostics(BridgeDiagnostics *out);
/* get_diagnostics 的短临界区仅保护单核特权任务/普通可屏蔽 ISR；禁止 NMI/HardFault。
 * out 必须为调用者独立可写副本，不与其他上下文并发使用。 */
#endif
