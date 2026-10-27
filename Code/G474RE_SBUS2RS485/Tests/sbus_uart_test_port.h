#ifndef SBUS_UART_TEST_PORT_H
#define SBUS_UART_TEST_PORT_H
#if !defined(SBUS_UART_HOST_TEST) || defined(__arm__) || defined(__thumb__)
#error "Host-only UART test port; never enable in firmware."
#endif
#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef enum { HAL_UART_RXEVENT_TC, HAL_UART_RXEVENT_HT, HAL_UART_RXEVENT_IDLE }
    HAL_UART_RxEventTypeTypeDef;
typedef void *osThreadId_t;
typedef enum { osKernelReady, osKernelRunning } osKernelState_t;

typedef struct { uint32_t ISR, CR3; } USART_TypeDef;
typedef struct { uint32_t CCR; } DMA_Channel_TypeDef;
typedef struct
{
    uint32_t Request, Direction, PeriphInc, MemInc;
    uint32_t PeriphDataAlignment, MemDataAlignment, Mode;
} DMA_InitTypeDef;
typedef struct
{
    DMA_Channel_TypeDef *Instance;
    void *Parent;
    uint32_t State;
    DMA_InitTypeDef Init;
} DMA_HandleTypeDef;
typedef struct
{
    USART_TypeDef *Instance;
    DMA_HandleTypeDef *hdmarx;
    uint32_t RxState, ErrorCode;
    struct { uint32_t Mode, WordLength, Parity; } Init;
    HAL_UART_RxEventTypeTypeDef RxEventType;
} UART_HandleTypeDef;

extern UART_HandleTypeDef huart1;
extern USART_TypeDef mock_usart1;
extern DMA_Channel_TypeDef mock_channel1;
extern uint32_t mock_dma_flags;
#define USART1 (&mock_usart1)
#define DMA1_Channel1 (&mock_channel1)
#define HAL_UART_STATE_READY 1U
#define HAL_UART_STATE_BUSY_RX 2U
#define HAL_DMA_STATE_READY 1U
#define HAL_DMA_STATE_BUSY 2U
#define UART_MODE_RX 1U
#define UART_WORDLENGTH_9B 9U
#define UART_PARITY_NONE 0U
#define DMA_REQUEST_USART1_RX 24U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_CIRCULAR 1U
#define USART_ISR_PE 1U
#define USART_ISR_FE 2U
#define USART_ISR_NE 4U
#define USART_ISR_ORE 8U
#define USART_CR3_DMAR 1U
#define DMA_CCR_EN 1U
#define MOCK_DMA_HT 1U
#define MOCK_DMA_TC 2U
#define MOCK_DMA_TE 4U
#define USART1_IRQn 1
#define DMA1_Channel1_IRQn 2
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) MOCK_DMA_HT
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) MOCK_DMA_TC
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) MOCK_DMA_TE
#define __HAL_DMA_GET_FLAG(h, flag) (mock_dma_flags & (flag))
#define __HAL_DMA_GET_COUNTER(h) mock_dma_counter()

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void __DMB(void);
uint32_t HAL_GetTick(void);
uint32_t NVIC_GetPriority(int irq);
uint32_t mock_dma_counter(void);
osThreadId_t osThreadGetId(void);
osKernelState_t osKernelGetState(void);
uint32_t osThreadFlagsSet(osThreadId_t id, uint32_t flags);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(const UART_HandleTypeDef *);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *, uint16_t);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
#endif
