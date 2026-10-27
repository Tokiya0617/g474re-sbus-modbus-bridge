#ifndef RS485_PORT_TEST_PORT_H
#define RS485_PORT_TEST_PORT_H
#if !defined(RS485_PORT_HOST_TEST) || defined(__arm__) || defined(__thumb__)
#error "Host-only RS485 test port."
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef struct { uint32_t ISR, CR3; } USART_TypeDef;
typedef struct
{
    USART_TypeDef *Instance;
    uint32_t RxState, gState, ErrorCode, FifoMode;
    struct { uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl; } Init;
} UART_HandleTypeDef;
typedef void *osThreadId_t;
typedef enum { osKernelReady, osKernelRunning } osKernelState_t;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern UART_HandleTypeDef huart1, huart3;
extern USART_TypeDef mock_usart3;
#define USART3 (&mock_usart3)
#define USART3_IRQn 3
#define HAL_UART_STATE_READY 1U
#define HAL_UART_STATE_BUSY_RX 2U
#define HAL_UART_STATE_BUSY_TX 3U
#define UART_MODE_TX_RX 3U
#define UART_WORDLENGTH_8B 8U
#define UART_WORDLENGTH_9B 9U
#define UART_PARITY_NONE 0U
#define UART_PARITY_EVEN 2U
#define UART_PARITY_ODD 3U
#define UART_STOPBITS_1 1U
#define UART_STOPBITS_2 2U
#define UART_HWCONTROL_NONE 0U
#define UART_FIFOMODE_DISABLE 0U
#define USART_CR3_DMAR 1U
#define USART_CR3_DMAT 2U
#define USART_ISR_PE 1U
#define USART_ISR_FE 2U
#define USART_ISR_NE 4U
#define USART_ISR_ORE 8U
#define USART_ISR_BUSY 16U
#define USART_ISR_RXNE_RXFNE 32U
#define UART_CLEAR_PEF USART_ISR_PE
#define UART_CLEAR_FEF USART_ISR_FE
#define UART_CLEAR_NEF USART_ISR_NE
#define UART_CLEAR_OREF USART_ISR_ORE
#define UART_RXDATA_FLUSH_REQUEST USART_ISR_RXNE_RXFNE
#define __HAL_UART_CLEAR_FLAG(h, f) ((h)->Instance->ISR &= ~(uint32_t)(f))
#define __HAL_UART_SEND_REQ(h, f) ((h)->Instance->ISR &= ~(uint32_t)(f))
#define RS485_DE_GPIO_Port ((void *)1)
#define RS485_DE_Pin 12U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
uint32_t HAL_GetTick(void);
uint32_t NVIC_GetPriority(int irq);
bool rs485_test_clock_start(void);
uint32_t rs485_test_now_us(void);
osThreadId_t osThreadGetId(void);
osKernelState_t osKernelGetState(void);
uint32_t osThreadFlagsSet(osThreadId_t id, uint32_t flags);
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, const uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *, uint16_t);
#endif
