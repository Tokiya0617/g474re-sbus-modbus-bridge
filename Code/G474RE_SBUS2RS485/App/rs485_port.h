#ifndef RS485_PORT_H
#define RS485_PORT_H
#include "bridge_config.h"
#include <stddef.h>
#define RS485_THREAD_FLAG (1UL << 1)
typedef struct { uint32_t end_us; uint8_t byte; } Rs485Byte;
typedef struct
{
    uint32_t rx_bytes, uart_errors, rx_overruns, tx_completed, tx_failures;
    uint32_t restarts, fault_generation, last_hal_error, last_tx_end_us, rx_epoch_us;
    bool running, terminal_fault, transmitting;
} Rs485PortStats;

/* 固定适配当前候选 USART3/PC10/PC11、PB12 DE；不重新配置 CubeMX 外设。
 * 8E1/8O1/8N2，接收采用单字节 IT+有界原始字节环形缓存（不是命令队列）。
 * DE 与 /RE 由获批接线保证：本实现发送时停止 MCU RX，不处理回显。
 * 只支持 TC 后立即释放 DE，若收发器要求额外保持须先修改并验证端口。
 * start/process/send/pop 仅唯一 Modbus 任务调用，get_stats 可供诊断任务读取。
 * 计时使用 DWT 周期累计转 us；时钟固定且至少每秒调用 process；禁止调试暂停
 * 后继续把时间当实时，未提供 STOP/动态时钟/多核/NMI 同步支持。 */
bool rs485_port_start(const BridgeConfig *config);
void rs485_port_process(void);
bool rs485_port_pop(Rs485Byte *out);
bool rs485_port_bus_idle(void);
bool rs485_port_send(const uint8_t *data, size_t length);
bool rs485_port_get_stats(Rs485PortStats *out);
uint32_t rs485_port_now_us(void);
uint32_t rs485_port_now_ms(void);

/* 以下只由统一 HAL 回调/USART3 IRQ 前置钩子调用，不在 ISR 解析协议。 */
void rs485_port_before_irq(void);
void rs485_port_on_rx(void);
void rs485_port_on_tx(void);
void rs485_port_on_error(void);
#endif
