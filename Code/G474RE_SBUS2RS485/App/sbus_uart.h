#ifndef SBUS_UART_H
#define SBUS_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 专供 SBUS 任务的线程标志位；通知不携带字节，不等于一帧。 */
#define SBUS_UART_THREAD_FLAG (1UL << 0)

typedef struct
{
    uint32_t ht_events;
    uint32_t tc_events;
    uint32_t idle_events;
    uint32_t uart_error_events;
    uint32_t last_hal_error;   /* 最近 HAL UART 错误位，恢复后保留，便于诊断。 */
    uint32_t buffer_overruns;  /* 任务未及时取走数据，或复制期间被覆盖。 */
    uint32_t irq_late_events;  /* HT/TC 同时挂起，无法证明接收进度完整。 */
    uint32_t decoded_frames;
    uint32_t valid_frames;       /* 已发布且 frame_lost/failsafe 均未置位的帧数。 */
    uint32_t rejected_candidates; /* 25 字节候选解码失败次数，不等于坏字节数。 */
    uint32_t restarts;
    uint32_t restart_failures;
    bool running;
    bool terminal_fault;      /* 启动/恢复失败，不自动无限重试。 */
} SbusUartStats;

/**
 * @brief 初始化接收状态并启动 UART DMA 接收。
 * 
 * @return true HAL 接受启动请求；false 配置不满足、重复调用或启动失败。
 * 
 * @note 外设初始化及内核启动后，由唯一的 SBUS 任务调用一次，独占
 *       USART1 RX。内部保存调用任务，回调用线程标志唤醒它。
 *       rc_snapshot_init() 应在任务启动前调用。启动失败需复位排查。
 */
bool sbus_uart_start(void);

/**
 * @brief 处理 DMA 缓冲区中新收到的字节，组帧并发布快照。
 * 
 * @note 仅由同一个 SBUS 任务调用，不从中断中调用。
 *       每次最多处理一个有界批次；错误时丢弃残帧并在任务中停止/重启接收。
 *       回调及 IRQ 前置钩子必须同时接入，HT/TC 中断不得禁用。
 */
void sbus_uart_process(void);

/* 在对应 IRQ 的 USER CODE 0 区调用，必须早于 HAL 清除硬件标志。
 * 用于识别延迟的 HT/TC 和尚未进入 ErrorCallback 的硬件错误。
 * 仅供当前 USART1 / DMA1_Channel1 接收链，禁止其他上下文调用。
 */
void sbus_uart_before_dma_irq(void);
void sbus_uart_before_uart_irq(void);

/* 复制诊断计数；计数 uint32_t 自然回绕，NULL 返回 false。 */
bool sbus_uart_get_stats(SbusUartStats *out);

/* 仅由 uart_callbacks.c 在 USART1 的 HAL 回调上下文调用。 */
void sbus_uart_on_rx_event(uint16_t size);
void sbus_uart_on_error(void);

#ifdef __cplusplus
}
#endif

#endif  /* SBUS_UART_H */

