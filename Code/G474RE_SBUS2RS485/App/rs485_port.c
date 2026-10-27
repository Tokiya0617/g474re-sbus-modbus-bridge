#include "rs485_port.h"
#include "modbus_rtu.h"
#include <string.h>
#if defined(RS485_PORT_HOST_TEST)
#include "rs485_port_test_port.h"
#else
#include "usart.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#endif

#define RX_CAPACITY 512U /* 只缓存原始字节及时间，满即整条接收流失效。 */
typedef enum { PORT_STOPPED, PORT_RX, PORT_DE_SETUP, PORT_TX, PORT_FAULT } PortPhase;
static Rs485Byte s_rx[RX_CAPACITY];
static uint8_t s_rx_byte, s_tx[MODBUS_ADU_MAX];
static uint32_t s_head, s_tail, s_tx_begin_ms, s_de_begin_us;
static size_t s_tx_length;
static BridgeConfig s_config;
static Rs485PortStats s_stats;
static PortPhase s_phase;
static osThreadId_t s_owner;
static bool s_owned, s_clock_started;
#if !defined(RS485_PORT_HOST_TEST)
static uint32_t s_last_cycle, s_cycle_remainder, s_us, s_cycles_per_us;
#endif

static uint32_t lock(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}
static void unlock(uint32_t saved) { __DMB(); __set_PRIMASK(saved); }
static void wake(void)
{
    if (s_owner != NULL) (void)osThreadFlagsSet(s_owner, RS485_THREAD_FLAG);
}
static void de(bool sending)
{
    const bool high = sending == s_config.de_active_high;
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static bool clock_start(void)
{
#if defined(RS485_PORT_HOST_TEST)
    return rs485_test_clock_start();
#else
    if (SystemCoreClock < 1000000U || SystemCoreClock % 1000000U != 0U ||
        (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U) return false;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    s_cycles_per_us = SystemCoreClock / 1000000U;
    s_last_cycle = DWT->CYCCNT;
    s_cycle_remainder = 0U;
    s_us = 0U;
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U;
#endif
}
static uint32_t clock_locked(void)
{
#if defined(RS485_PORT_HOST_TEST)
    return rs485_test_now_us();
#else
    const uint32_t cycle = DWT->CYCCNT;
    const uint32_t delta = cycle - s_last_cycle;
    s_last_cycle = cycle;
    /* 分开计算避免 delta+remainder 在周期计数将回绕时溢出。 */
    s_us += delta / s_cycles_per_us;
    s_cycle_remainder += delta % s_cycles_per_us;
    s_us += s_cycle_remainder / s_cycles_per_us;
    s_cycle_remainder %= s_cycles_per_us;
    return s_us;
#endif
}
uint32_t rs485_port_now_us(void)
{
    const uint32_t saved = lock();
    const uint32_t now = s_clock_started ? clock_locked() : 0U;
    unlock(saved);
    return now;
}
uint32_t rs485_port_now_ms(void) { return HAL_GetTick(); }

static void fault_locked(void)
{
    if (s_phase != PORT_FAULT)
    {
        ++s_stats.fault_generation;
        if (s_phase == PORT_TX || s_phase == PORT_DE_SETUP) ++s_stats.tx_failures;
    }
    s_phase = PORT_FAULT;
    s_stats.running = false;
    s_stats.transmitting = false;
    de(false); /* 错误时放弃损坏应答，优先释放总线，任务稍后清理 HAL。 */
}

static bool configuration_ready(const BridgeConfig *config)
{
    const uint32_t parity = config->parity == BRIDGE_PARITY_EVEN ? UART_PARITY_EVEN :
                            config->parity == BRIDGE_PARITY_ODD ? UART_PARITY_ODD : UART_PARITY_NONE;
    const uint32_t word = config->parity == BRIDGE_PARITY_NONE ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    const uint32_t stop = config->stop_bits == 1U ? UART_STOPBITS_1 : UART_STOPBITS_2;
    return huart3.Instance == USART3 && huart3.RxState == HAL_UART_STATE_READY &&
        huart3.gState == HAL_UART_STATE_READY && huart3.Init.Mode == UART_MODE_TX_RX &&
        huart3.Init.BaudRate == config->baud_rate && huart3.Init.WordLength == word &&
        huart3.Init.Parity == parity && huart3.Init.StopBits == stop &&
        huart3.Init.HwFlowCtl == UART_HWCONTROL_NONE &&
        huart3.FifoMode == UART_FIFOMODE_DISABLE &&
        (huart3.Instance->CR3 & (USART_CR3_DMAR | USART_CR3_DMAT)) == 0U &&
        NVIC_GetPriority(USART3_IRQn) >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
}

bool rs485_port_start(const BridgeConfig *config)
{
    if (s_owned || !bridge_config_valid(config) || osKernelGetState() != osKernelRunning ||
        osThreadGetId() == NULL || !configuration_ready(config)) return false;
    s_config = *config;
    s_owner = osThreadGetId();
    s_owned = true;
    de(false);
    s_clock_started = clock_start();
    if (!s_clock_started)
    {
        s_stats.terminal_fault = true;
        return false;
    }
    const uint32_t saved = lock();
    s_head = s_tail = 0U;
    s_phase = PORT_RX;
    s_stats.rx_epoch_us = clock_locked();
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
    __HAL_UART_SEND_REQ(&huart3, UART_RXDATA_FLUSH_REQUEST);
    const bool ok = HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1U) == HAL_OK;
    s_stats.running = ok;
    if (!ok) { fault_locked(); s_stats.terminal_fault = true; }
    unlock(saved);
    return ok;
}

void rs485_port_before_irq(void)
{
    if (!s_owned || s_stats.terminal_fault) return;
    const uint32_t saved = lock();
    if ((huart3.Instance->ISR & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) != 0U)
        fault_locked();
    unlock(saved);
}

void rs485_port_on_rx(void)
{
    if (!s_owned || s_stats.terminal_fault) return;
    const uint32_t saved = lock();
    if (s_phase == PORT_RX)
    {
        if ((uint32_t)(s_head - s_tail) >= RX_CAPACITY)
        {
            ++s_stats.rx_overruns;
            fault_locked();
        }
        else
        {
            s_rx[s_head % RX_CAPACITY].byte = s_rx_byte;
            s_rx[s_head % RX_CAPACITY].end_us = clock_locked();
            ++s_head;
            ++s_stats.rx_bytes;
            if (HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1U) != HAL_OK) fault_locked();
        }
    }
    unlock(saved);
    wake();
}

void rs485_port_on_tx(void)
{
    if (!s_owned || s_stats.terminal_fault) return;
    const uint32_t saved = lock();
    if (s_phase == PORT_TX)
    {
        /* HAL IT 的 TxCplt 来自 UART TC：移位寄存器最后一位已经发完。 */
        de(false);
        s_stats.last_tx_end_us = clock_locked();
        ++s_stats.tx_completed;
        s_stats.transmitting = false;
        s_phase = PORT_RX;
        s_stats.rx_epoch_us = s_stats.last_tx_end_us;
        __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
        __HAL_UART_SEND_REQ(&huart3, UART_RXDATA_FLUSH_REQUEST);
        if (HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1U) != HAL_OK) fault_locked();
    }
    unlock(saved);
    wake();
}

void rs485_port_on_error(void)
{
    if (!s_owned || s_stats.terminal_fault) return;
    const uint32_t saved = lock();
    ++s_stats.uart_errors;
    s_stats.last_hal_error = huart3.ErrorCode;
    fault_locked();
    unlock(saved);
    wake();
}

void rs485_port_process(void)
{
    if (!s_owned || osThreadGetId() != s_owner || s_stats.terminal_fault) return;
    uint32_t saved = lock();
    const uint32_t now_us = clock_locked();
    if ((s_phase == PORT_TX || s_phase == PORT_DE_SETUP) &&
        (uint32_t)(HAL_GetTick() - s_tx_begin_ms) >= s_config.tx_timeout_ms) fault_locked();
    const PortPhase phase = s_phase;
    const bool start_tx = phase == PORT_DE_SETUP &&
                         (uint32_t)(now_us - s_de_begin_us) >= s_config.de_setup_us;
    if (start_tx) s_phase = PORT_TX;
    unlock(saved);

    if (phase == PORT_FAULT)
    {
        /* IT 模式无 DMA 等待；仍在任务中清理，不在错误 ISR 阻塞。 */
        const HAL_StatusTypeDef rx = HAL_UART_AbortReceive(&huart3);
        const HAL_StatusTypeDef tx = HAL_UART_AbortTransmit(&huart3);
        saved = lock();
        s_tail = s_head; /* 已知数据流中断，不能拼接故障前的半帧。 */
        const bool ready = rx == HAL_OK && tx == HAL_OK &&
                           huart3.RxState == HAL_UART_STATE_READY && huart3.gState == HAL_UART_STATE_READY;
        s_phase = PORT_RX;
        s_stats.rx_epoch_us = clock_locked();
        const bool ok = ready && HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1U) == HAL_OK;
        s_stats.running = ok;
        if (ok) ++s_stats.restarts;
        else { s_phase = PORT_FAULT; s_stats.terminal_fault = true; }
        unlock(saved);
    }
    else if (start_tx)
    {
        /* s_tx 为模块静态副本，异步发送完成前不再修改。 */
        if (HAL_UART_Transmit_IT(&huart3, s_tx, (uint16_t)s_tx_length) != HAL_OK)
        {
            saved = lock();
            fault_locked();
            unlock(saved);
        }
    }
}

bool rs485_port_pop(Rs485Byte *out)
{
    if (out == NULL || !s_owned || osThreadGetId() != s_owner) return false;
    const uint32_t saved = lock();
    const bool ready = s_phase == PORT_RX && s_head != s_tail && !s_stats.terminal_fault;
    if (ready) { *out = s_rx[s_tail % RX_CAPACITY]; ++s_tail; }
    unlock(saved);
    return ready;
}

bool rs485_port_bus_idle(void)
{
    if (!s_owned) return false;
    const uint32_t saved = lock();
    const bool idle = s_phase == PORT_RX && s_head == s_tail && !s_stats.terminal_fault &&
        (huart3.Instance->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE |
         USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) == 0U;
    unlock(saved);
    return idle;
}

bool rs485_port_send(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > sizeof(s_tx) || !s_owned ||
        osThreadGetId() != s_owner || !rs485_port_bus_idle()) return false;
    /* 无待发送队列，同一时刻只保留当前应答的稳定副本。 */
    memcpy(s_tx, data, length);
    uint32_t saved = lock();
    if (s_phase != PORT_RX || s_head != s_tail ||
        (huart3.Instance->ISR & (USART_ISR_BUSY | USART_ISR_RXNE_RXFNE |
         USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) != 0U)
    {
        unlock(saved);
        return false;
    }
    s_phase = PORT_DE_SETUP;
    s_stats.transmitting = true;
    s_tx_length = length;
    s_tx_begin_ms = HAL_GetTick();
    unlock(saved);
    if (HAL_UART_AbortReceive(&huart3) != HAL_OK)
    {
        saved = lock(); fault_locked(); unlock(saved);
        return false;
    }
    saved = lock();
    if (s_phase != PORT_DE_SETUP) { unlock(saved); return false; }
    de(true);
    s_de_begin_us = clock_locked();
    unlock(saved);
    return true;
}

bool rs485_port_get_stats(Rs485PortStats *out)
{
    if (out == NULL) return false;
    const uint32_t saved = lock();
    *out = s_stats;
    unlock(saved);
    return true;
}
