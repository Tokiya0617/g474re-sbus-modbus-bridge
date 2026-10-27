/* Deterministic HAL/DMA model: verifies data ownership and event ordering,
 * not physical UART timing or actual IRQ latency. No test main in firmware. */
#include "sbus_uart.h"
#include "sbus_uart_test_port.h"
#include "rc_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Assertions must remain enabled."
#endif

USART_TypeDef mock_usart1;
DMA_Channel_TypeDef mock_channel1;
uint32_t mock_dma_flags;
static DMA_HandleTypeDef dma;
UART_HandleTypeDef huart1;
static uint8_t *dma_buffer;
static uint16_t dma_size, dma_position;
static uint32_t primask, tick, priority = 5U, notifications, starts, aborts;
static uint32_t counter_calls, inject_on_counter, inject_bytes;
static uint32_t inject_uart_error;
static bool fail_start, fail_abort;
static osThreadId_t current_thread = (void *)1;

uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t mask) { assert(mask <= 1U); primask = mask; }
void __DMB(void) { __sync_synchronize(); }
uint32_t HAL_GetTick(void) { return tick; }
uint32_t NVIC_GetPriority(int irq) { (void)irq; return priority; }
osThreadId_t osThreadGetId(void) { return current_thread; }
osKernelState_t osKernelGetState(void) { return osKernelRunning; }
uint32_t osThreadFlagsSet(osThreadId_t id, uint32_t flags)
{
    assert(id == (void *)1 && flags == SBUS_UART_THREAD_FLAG);
    ++notifications;
    return flags;
}
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{
    assert(h == &huart1 && primask == 0U);
    ++starts;
    if (fail_start) return HAL_ERROR;
    assert(n == 256U && dma.State == HAL_DMA_STATE_READY);
    dma_buffer = p;
    dma_size = n;
    dma_position = 0U;
    mock_dma_flags = 0U;
    dma.State = HAL_DMA_STATE_BUSY;
    dma.Instance->CCR = DMA_CCR_EN;
    h->Instance->CR3 = USART_CR3_DMAR;
    h->RxState = HAL_UART_STATE_BUSY_RX;
    h->ErrorCode = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h)
{
    assert(h == &huart1 && primask == 0U);
    ++aborts;
    if (fail_abort) return HAL_ERROR;
    dma.Instance->CCR = 0U;
    dma.State = HAL_DMA_STATE_READY;
    h->RxState = HAL_UART_STATE_READY;
    h->Instance->ISR = 0U;
    h->Instance->CR3 = 0U;
    mock_dma_flags = 0U;
    return HAL_OK;
}
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(const UART_HandleTypeDef *h)
{
    return h->RxEventType;
}

static void feed_byte(uint8_t byte)
{
    assert(dma_buffer != NULL && (dma.Instance->CCR & DMA_CCR_EN) != 0U);
    dma_buffer[dma_position++] = byte;
    if (dma_position == dma_size / 2U) mock_dma_flags |= MOCK_DMA_HT;
    if (dma_position == dma_size)
    {
        dma_position = 0U;
        mock_dma_flags |= MOCK_DMA_TC;
    }
}
uint32_t mock_dma_counter(void)
{
    ++counter_calls;
    if (counter_calls == inject_on_counter)
    {
        /* Models DMA moving while the task copied its work buffer. */
        for (uint32_t i = 0U; i < inject_bytes; ++i) feed_byte(0xFFU);
        huart1.Instance->ISR |= inject_uart_error;
    }
    return dma_size - dma_position;
}
static void dma_irq(void)
{
    sbus_uart_before_dma_irq(); /* Must run before emulated HAL flag clear. */
    if ((mock_dma_flags & MOCK_DMA_HT) != 0U)
    {
        mock_dma_flags &= ~MOCK_DMA_HT;
        huart1.RxEventType = HAL_UART_RXEVENT_HT;
        HAL_UARTEx_RxEventCallback(&huart1, dma_size / 2U);
    }
    else if ((mock_dma_flags & MOCK_DMA_TC) != 0U)
    {
        mock_dma_flags &= ~MOCK_DMA_TC;
        huart1.RxEventType = HAL_UART_RXEVENT_TC;
        HAL_UARTEx_RxEventCallback(&huart1, dma_size);
    }
}
static void idle_irq(void)
{
    sbus_uart_before_uart_irq();
    huart1.RxEventType = HAL_UART_RXEVENT_IDLE;
    HAL_UARTEx_RxEventCallback(&huart1, dma_position);
}
static void feed(const uint8_t *bytes, size_t count, bool service_irqs)
{
    for (size_t i = 0U; i < count; ++i)
    {
        feed_byte(bytes[i]);
        if (service_irqs && mock_dma_flags != 0U) dma_irq();
    }
}
static void make_frame(uint8_t bytes[25], uint32_t seed)
{
    memset(bytes, 0, 25U);
    bytes[0] = 0x0FU;
    for (size_t ch = 0U; ch < 16U; ++ch)
    {
        const uint16_t value = (uint16_t)((seed + 37U * ch) & 2047U);
        for (size_t bit = 0U; bit < 11U; ++bit)
        {
            if ((value & (1U << bit)) != 0U)
            {
                const size_t offset = ch * 11U + bit;
                bytes[1U + offset / 8U] |= (uint8_t)(1U << (offset % 8U));
            }
        }
    }
    bytes[23] = (uint8_t)((seed & 3U) << 2U);
}
static RcSnapshot read_snapshot(void)
{
    RcSnapshot out;
    assert(rc_snapshot_read(&out));
    assert(primask == 0U);
    return out;
}
static SbusUartStats stats(void)
{
    SbusUartStats out;
    assert(sbus_uart_get_stats(&out));
    return out;
}
static void check_frame(uint32_t seed)
{
    const RcSnapshot out = read_snapshot();
    assert(out.has_frame && out.transport_ok);
    for (size_t ch = 0U; ch < 16U; ++ch)
        assert(out.frame.channels[ch] == ((seed + 37U * ch) & 2047U));
    assert(out.frame.frame_lost == ((seed & 1U) != 0U));
    assert(out.frame.failsafe == ((seed & 2U) != 0U));
}
static void send_frame(uint32_t seed)
{
    uint8_t bytes[25];
    make_frame(bytes, seed);
    ++tick;
    feed(bytes, sizeof(bytes), true);
    idle_irq();
    sbus_uart_process();
    check_frame(seed);
}

int main(int argc, char **argv)
{
    dma.Instance = DMA1_Channel1;
    dma.Parent = &huart1;
    dma.State = HAL_DMA_STATE_READY;
    dma.Init.Request = DMA_REQUEST_USART1_RX;
    dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    dma.Init.PeriphInc = DMA_PINC_DISABLE;
    dma.Init.MemInc = DMA_MINC_ENABLE;
    dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    dma.Init.Mode = DMA_CIRCULAR;
    huart1.Instance = USART1;
    huart1.hdmarx = &dma;
    huart1.RxState = HAL_UART_STATE_READY;
    huart1.Init.Mode = UART_MODE_RX;
    huart1.Init.WordLength = UART_WORDLENGTH_9B;
    huart1.Init.Parity = 1U;
    rc_snapshot_init();

    if (argc == 2 && strcmp(argv[1], "start-failure") == 0)
    {
        fail_start = true;
        assert(!sbus_uart_start());
        assert(stats().terminal_fault && !stats().running && starts == 1U);
        assert(!read_snapshot().has_frame && !read_snapshot().transport_ok);
        assert(!sbus_uart_start());
        sbus_uart_process();
        assert(starts == 1U && primask == 0U);
        puts("PASS: initial HAL start failure is terminal and leaves snapshot unavailable");
        return 0;
    }

    sbus_uart_process(); /* Safe before start. */
    sbus_uart_before_dma_irq();
    priority = 4U;
    assert(!sbus_uart_start() && starts == 0U);
    priority = 5U;
    dma.Init.Mode = 0U;
    assert(!sbus_uart_start() && starts == 0U);
    dma.Init.Mode = DMA_CIRCULAR;
    huart1.Init.Parity = UART_PARITY_NONE;
    assert(!sbus_uart_start() && starts == 0U);
    huart1.Init.Parity = 1U;
    assert(sbus_uart_start());
    assert(!sbus_uart_start() && starts == 1U);
    assert(stats().running && notifications != 0U);
    assert(!sbus_uart_get_stats(NULL));

    if (argc == 2 && strcmp(argv[1], "abort-failure") == 0)
    {
        send_frame(99U);
        HAL_UART_ErrorCallback(&huart1);
        fail_abort = true;
        sbus_uart_process();
        assert(stats().terminal_fault && !stats().running && starts == 1U);
        assert(!read_snapshot().transport_ok);
        sbus_uart_process();
        assert(starts == 1U && aborts == 1U && primask == 0U);
        puts("PASS: failed abort stops recovery without a second DMA start");
        return 0;
    }

    /* Every split point, duplicate IDLE, payload flags, and repeated ring wraps. */
    for (size_t split = 1U; split < 25U; ++split)
    {
        uint8_t bytes[25];
        make_frame(bytes, (uint32_t)split);
        const uint32_t sequence = read_snapshot().sequence;
        feed(bytes, split, true);
        idle_irq();
        sbus_uart_process();
        assert(read_snapshot().sequence == sequence);
        feed(bytes + split, sizeof(bytes) - split, true);
        idle_irq();
        sbus_uart_process();
        check_frame((uint32_t)split);
        assert(read_snapshot().sequence == sequence + 1U);
        idle_irq();
        sbus_uart_process();
        assert(read_snapshot().sequence == sequence + 1U);
    }
    /* Continuous stream without IDLE: HT/TC alone must make progress. */
    uint32_t processed_frames = stats().decoded_frames;
    for (uint32_t n = 0U; n < 120U; ++n)
    {
        uint8_t bytes[25];
        make_frame(bytes, n + 100U);
        for (size_t i = 0U; i < sizeof(bytes); ++i)
        {
            feed_byte(bytes[i]);
            if (mock_dma_flags != 0U) { dma_irq(); sbus_uart_process(); }
        }
    }
    idle_irq();
    sbus_uart_process();
    assert(stats().decoded_frames == processed_frames + 120U);
    check_frame(219U);

    /* IDLE samples beyond HT first; delayed HT Size=128 must not rewind cursor. */
    uint8_t noise[200];
    memset(noise, 0xFF, sizeof(noise));
    feed(noise, 20U, true);
    idle_irq();
    sbus_uart_process();
    const uint32_t before_duplicate = stats().decoded_frames;
    huart1.RxEventType = HAL_UART_RXEVENT_HT;
    HAL_UARTEx_RxEventCallback(&huart1, 128U);
    sbus_uart_process();
    assert(stats().decoded_frames == before_duplicate && stats().buffer_overruns == 0U);
    UART_HandleTypeDef other = {0};
    const uint32_t before_notify = notifications;
    HAL_UART_ErrorCallback(&other);
    HAL_UARTEx_RxEventCallback(&other, 25U);
    assert(notifications == before_notify);
    send_frame(300U);

    /* UART errors invalidate immediately, retain history, discard partial frame. */
    const uint32_t errors[] = {USART_ISR_PE, USART_ISR_FE, USART_ISR_NE, USART_ISR_ORE};
    for (size_t e = 0U; e < sizeof(errors) / sizeof(errors[0]); ++e)
    {
        uint8_t bytes[25];
        make_frame(bytes, 400U + (uint32_t)e);
        feed(bytes, 10U, true);
        idle_irq();
        sbus_uart_process();
        const RcSnapshot before = read_snapshot();
        huart1.Instance->ISR = errors[e];
        sbus_uart_before_uart_irq();
        assert(!read_snapshot().transport_ok);
        huart1.ErrorCode = errors[e];
        huart1.Instance->ISR = 0U; /* HAL clears flags before error callback. */
        huart1.RxState = HAL_UART_STATE_READY;
        huart1.Instance->CR3 = 0U;
        dma.State = HAL_DMA_STATE_READY;
        dma.Instance->CCR = 0U;
        HAL_UART_ErrorCallback(&huart1);
        assert(read_snapshot().sequence == before.sequence);
        assert(read_snapshot().received_ms == before.received_ms);
        sbus_uart_process();
        assert(stats().running && !read_snapshot().transport_ok);
        send_frame(500U + (uint32_t)e);
    }
    assert(stats().uart_error_events == 4U && stats().last_hal_error == USART_ISR_ORE);

    /* Task stalled, IRQs still serviced: cumulative bytes detect multiple laps. */
    uint32_t overruns = stats().buffer_overruns;
    feed(noise, sizeof(noise), true);
    feed(noise, sizeof(noise), true);
    assert(!read_snapshot().transport_ok);
    sbus_uart_process();
    assert(stats().buffer_overruns == overruns + 1U && stats().running);
    send_frame(601U);

    /* IRQs stalled for multiple laps: raw HT+TC flags detect ambiguity. */
    const uint32_t late = stats().irq_late_events;
    for (size_t n = 0U; n < 3U; ++n) feed(noise, sizeof(noise), false);
    dma_irq();
    assert(!read_snapshot().transport_ok);
    sbus_uart_process();
    assert(stats().irq_late_events == late + 1U && stats().running);
    send_frame(602U);

    /* DMA overruns while task copies: reject whole work batch before decoding. */
    feed(noise, sizeof(noise), true);
    idle_irq();
    const uint32_t sequence = read_snapshot().sequence;
    overruns = stats().buffer_overruns;
    counter_calls = 0U;
    inject_on_counter = 2U;
    inject_bytes = 60U; /* 200 unread + 60 newly arrived exceeds capacity. */
    sbus_uart_process();
    inject_on_counter = 0U;
    assert(stats().buffer_overruns == overruns + 1U);
    assert(read_snapshot().sequence == sequence && !read_snapshot().transport_ok);
    send_frame(603U);

    /* Hardware error becomes pending before its HAL error callback runs. */
    uint8_t pending_frame[25];
    make_frame(pending_frame, 604U);
    feed(pending_frame, sizeof(pending_frame), true);
    idle_irq();
    const uint32_t before_pending_error = read_snapshot().sequence;
    counter_calls = 0U;
    inject_on_counter = 2U;
    inject_bytes = 0U;
    inject_uart_error = USART_ISR_PE;
    sbus_uart_process();
    inject_on_counter = 0U;
    inject_uart_error = 0U;
    assert(read_snapshot().sequence == before_pending_error);
    assert(!read_snapshot().transport_ok && stats().running);
    send_frame(605U);

    /* DMA transfer error, timer wrap, wrong owner, nested mask and failed restart. */
    mock_dma_flags |= MOCK_DMA_TE;
    sbus_uart_before_dma_irq();
    sbus_uart_process();
    assert(stats().running && !read_snapshot().transport_ok);
    tick = UINT32_MAX - 1U;
    send_frame(701U);
    assert(read_snapshot().received_ms == UINT32_MAX);
    send_frame(702U);
    assert(read_snapshot().received_ms == 0U);
    current_thread = (void *)2;
    sbus_uart_process();
    current_thread = (void *)1;
    __disable_irq();
    SbusUartStats out;
    assert(sbus_uart_get_stats(&out) && primask == 1U);
    __set_PRIMASK(0U);
    fail_start = true;
    HAL_UART_ErrorCallback(&huart1);
    sbus_uart_process();
    assert(stats().terminal_fault && !stats().running && !read_snapshot().transport_ok);
    const uint32_t start_count = starts;
    sbus_uart_process();
    assert(starts == start_count && aborts != 0U && primask == 0U);
    puts("PASS: UART DMA splits/wraps, continuous HT/TC, duplicate events, UART/DMA errors, overrun, late IRQ, recovery and failure");
    return 0;
}
