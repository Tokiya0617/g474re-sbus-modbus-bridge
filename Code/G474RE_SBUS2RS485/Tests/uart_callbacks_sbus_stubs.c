/* 仅 SBUS 单元测试：另一个端口不存在真实外设，不将此文件加入固件。 */
#include "sbus_uart_test_port.h"
UART_HandleTypeDef huart3;
void rs485_port_on_error(void) {}
void rs485_port_on_rx(void) {}
void rs485_port_on_tx(void) {}
