/* HAL 全局回调只能有一个强定义；在此按句柄分发，业务模块不抢占彼此回调。 */
#if defined(SBUS_UART_HOST_TEST)
#include "sbus_uart_test_port.h"
extern UART_HandleTypeDef huart3;
#elif defined(RS485_PORT_HOST_TEST)
#include "rs485_port_test_port.h"
#else
#include "usart.h"
#endif
#include "sbus_uart.h"
#include "rs485_port.h"

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart == &huart1) sbus_uart_on_rx_event(size);
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) sbus_uart_on_error();
    else if (huart == &huart3) rs485_port_on_error();
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart3) rs485_port_on_rx();
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart3) rs485_port_on_tx();
}
