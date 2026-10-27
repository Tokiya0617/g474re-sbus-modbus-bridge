#include "rs485_port.h"
#include "rs485_port_test_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Tests require assert()."
#endif
UART_HandleTypeDef huart1, huart3;
USART_TypeDef mock_usart3;
static uint32_t s_mask, s_now_us, s_now_ms, s_flags;
static uint8_t *s_rx_pointer;
static const uint8_t *s_tx_pointer;
static uint16_t s_tx_count;
static bool s_clock_ok = true, s_rx_ok = true, s_tx_ok = true, s_abort_ok = true;
static bool s_wrong_owner;
static GPIO_PinState s_de;
static unsigned s_sbus_errors, s_sbus_events, s_abort_count;
uint32_t __get_PRIMASK(void) { return s_mask; }
void __disable_irq(void) { s_mask = 1U; }
void __set_PRIMASK(uint32_t value) { s_mask = value; }
void __DMB(void) {}
uint32_t HAL_GetTick(void) { return s_now_ms; }
uint32_t NVIC_GetPriority(int irq) { (void)irq; return 6U; }
bool rs485_test_clock_start(void) { return s_clock_ok; }
uint32_t rs485_test_now_us(void) { return s_now_us; }
osThreadId_t osThreadGetId(void) { return (void *)(uintptr_t)(s_wrong_owner ? 2U : 1U); }
osKernelState_t osKernelGetState(void) { return osKernelRunning; }
uint32_t osThreadFlagsSet(osThreadId_t id, uint32_t flags)
{
    assert(id == (void *)1); s_flags |= flags; return s_flags;
}
void HAL_GPIO_WritePin(void *port, uint32_t pin, GPIO_PinState state)
{
    assert(port == RS485_DE_GPIO_Port && pin == RS485_DE_Pin); s_de = state;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    assert(h == &huart3 && n == 1U);
    if (!s_rx_ok) return HAL_ERROR;
    assert(h->RxState == HAL_UART_STATE_READY);
    s_rx_pointer = p; h->RxState = HAL_UART_STATE_BUSY_RX; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h, const uint8_t *p, uint16_t n)
{
    assert(h == &huart3 && n > 0U && s_de == GPIO_PIN_SET);
    if (!s_tx_ok) return HAL_ERROR;
    assert(h->gState == HAL_UART_STATE_READY);
    s_tx_pointer = p; s_tx_count = n; h->gState = HAL_UART_STATE_BUSY_TX; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
    assert(h == &huart3); ++s_abort_count;
    if (!s_abort_ok) return HAL_ERROR;
    h->RxState = HAL_UART_STATE_READY; s_rx_pointer = NULL;
    h->Instance->ISR &= ~(USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *h)
{
    assert(h == &huart3);
    if (!s_abort_ok) return HAL_ERROR;
    h->gState = HAL_UART_STATE_READY; return HAL_OK;
}
void sbus_uart_on_error(void) { ++s_sbus_errors; }
void sbus_uart_on_rx_event(uint16_t size) { (void)size; ++s_sbus_events; }

static BridgeConfig config(void)
{
    /* 仅主机模型参数，不作为 PLC/收发器配置建议。 */
    const BridgeConfig value = {
        .enabled = true, .approved = true, .slave_address = 1U, .register_base = 0U,
        .word_order = REGISTER_HIGH_WORD_FIRST, .baud_rate = 115200U,
        .parity = BRIDGE_PARITY_EVEN, .stop_bits = 1U, .de_active_high = true,
        .de_setup_us = 10U, .tx_timeout_ms = 100U, .sbus_timeout_ms = 100U,
        .plc_timeout_ms = 1000U, .firmware_version = 1U
    };
    return value;
}
static void prepare(void)
{
    huart3.Instance = USART3;
    huart3.RxState = huart3.gState = HAL_UART_STATE_READY;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.BaudRate = 115200U;
    huart3.Init.WordLength = UART_WORDLENGTH_9B;
    huart3.Init.Parity = UART_PARITY_EVEN;
    huart3.Init.StopBits = UART_STOPBITS_1;
}
static void receive(uint8_t byte)
{
    assert(s_rx_pointer != NULL);
    *s_rx_pointer = byte; huart3.RxState = HAL_UART_STATE_READY;
    HAL_UART_RxCpltCallback(&huart3);
}
static Rs485PortStats stats(void)
{
    Rs485PortStats value; assert(rs485_port_get_stats(&value)); return value;
}
int main(int argc, char **argv)
{
    prepare(); BridgeConfig cfg = config();
    assert(!rs485_port_start(bridge_config_get()));
    cfg.approved = false; assert(!rs485_port_start(&cfg)); cfg.approved = true;
    huart3.Init.Parity = UART_PARITY_NONE;
    assert(!rs485_port_start(&cfg)); huart3.Init.Parity = UART_PARITY_EVEN;
    if (argc > 1 && strcmp(argv[1], "clock-failure") == 0)
    {
        s_clock_ok = false;
        assert(!rs485_port_start(&cfg));
        assert(stats().terminal_fault && s_de == GPIO_PIN_RESET);
        puts("PASS: RS485 clock failure remains terminal with DE released"); return 0;
    }
    if (argc > 1 && strcmp(argv[1], "start-failure") == 0) s_rx_ok = false;
    const bool started = rs485_port_start(&cfg);
    if (!s_rx_ok)
    {
        assert(!started && stats().terminal_fault && !stats().running);
        puts("PASS: RS485 initial receive failure is terminal"); return 0;
    }
    assert(started && stats().running && s_de == GPIO_PIN_RESET);
    assert(!rs485_port_start(&cfg));
    UART_HandleTypeDef other = {0};
    HAL_UART_RxCpltCallback(&other); HAL_UART_TxCpltCallback(&other); HAL_UART_ErrorCallback(&other);
    HAL_UART_ErrorCallback(&huart1); HAL_UARTEx_RxEventCallback(&huart1, 128U);
    assert(s_sbus_errors == 1U && s_sbus_events == 1U && stats().uart_errors == 0U);
    s_now_us = 5000U; receive(0x12U);
    assert(!rs485_port_bus_idle() && (s_flags & RS485_THREAD_FLAG) != 0U);
    Rs485Byte byte; s_wrong_owner = true;
    assert(!rs485_port_pop(&byte)); s_wrong_owner = false;
    s_mask = 1U; assert(rs485_port_pop(&byte) && s_mask == 1U); s_mask = 0U;
    assert(byte.byte == 0x12U && byte.end_us == 5000U && rs485_port_bus_idle());
    assert(!rs485_port_pop(&byte) && !rs485_port_pop(NULL));
    mock_usart3.ISR = USART_ISR_BUSY; assert(!rs485_port_bus_idle()); mock_usart3.ISR = 0U;

    uint8_t tx[] = {1U, 3U, 2U, 0U, 1U, 0U, 0U};
    assert(rs485_port_send(tx, sizeof(tx)));
    assert(s_de == GPIO_PIN_SET && s_tx_pointer == NULL && stats().transmitting);
    assert(!rs485_port_send(tx, sizeof(tx)));
    tx[0] = 99U; /* 端口必须发送自己的稳定副本。 */
    s_now_us += 9U; rs485_port_process(); assert(s_tx_pointer == NULL);
    s_now_us += 1U; rs485_port_process();
    assert(s_tx_count == sizeof(tx) && s_tx_pointer[0] == 1U && s_de == GPIO_PIN_SET);
    huart3.gState = HAL_UART_STATE_READY; HAL_UART_TxCpltCallback(&huart3);
    assert(s_de == GPIO_PIN_RESET && stats().tx_completed == 1U && stats().running);
    assert(!stats().transmitting && s_rx_pointer != NULL);

    const uint32_t errors[] = {USART_ISR_PE, USART_ISR_FE, USART_ISR_NE, USART_ISR_ORE};
    for (size_t i = 0U; i < 4U; ++i)
    {
        mock_usart3.ISR = errors[i]; rs485_port_before_irq();
        assert(!stats().running);
        huart3.ErrorCode = errors[i]; HAL_UART_ErrorCallback(&huart3);
        rs485_port_process();
        assert(stats().running && stats().last_hal_error == errors[i]);
    }
    assert(stats().uart_errors == 4U && stats().fault_generation == 4U);
    for (unsigned i = 0U; i < 513U; ++i) { s_now_us += 96U; receive((uint8_t)i); }
    assert(stats().rx_overruns == 1U && !stats().running);
    rs485_port_process(); assert(stats().running && !rs485_port_pop(&byte));
    for (unsigned i = 0U; i < 1200U; ++i)
    {
        ++s_now_us; receive((uint8_t)i);
        assert(rs485_port_pop(&byte) && byte.byte == (uint8_t)i);
    }
    assert(!rs485_port_send(NULL, 7U) && !rs485_port_send(tx, 0U));
    assert(!rs485_port_send(tx, 257U));
    s_tx_ok = false; assert(rs485_port_send(tx, sizeof(tx)));
    s_now_us += 10U; rs485_port_process();
    assert(!stats().running && s_de == GPIO_PIN_RESET);
    s_tx_ok = true; rs485_port_process(); assert(stats().running);
    assert(rs485_port_send(tx, sizeof(tx))); s_now_us += 10U; rs485_port_process();
    s_now_ms += 100U; rs485_port_process();
    assert(stats().tx_failures == 2U && stats().running && s_de == GPIO_PIN_RESET);

    s_rx_ok = false; receive(0xAAU); assert(!stats().running);
    s_rx_ok = true; rs485_port_process(); assert(stats().running);
    s_abort_ok = false; huart3.ErrorCode = USART_ISR_ORE; HAL_UART_ErrorCallback(&huart3);
    rs485_port_process();
    assert(stats().terminal_fault && !stats().running && s_de == GPIO_PIN_RESET);
    const unsigned before = s_abort_count;
    rs485_port_process(); rs485_port_process(); assert(s_abort_count == before);
    assert(!rs485_port_get_stats(NULL));
    puts("PASS: RS485 dispatch, RX wrap/overflow, async TX/TC DE, timeout, errors, owner and terminal recovery");
    return 0;
}
