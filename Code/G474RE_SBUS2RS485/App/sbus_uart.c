#include "sbus_uart.h"
#include "sbus_receiver.h"
#include "rc_snapshot.h"

#if defined(SBUS_UART_HOST_TEST)
#include "sbus_uart_test_port.h"
#else
#include "usart.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#endif

/* 软件容量，不是 SBUS 帧长或失联阈值。原始数组和稳定副本均为定长。 */
#define SBUS_UART_DMA_BUFFER_SIZE 256U

/* 编译期检查，要求容量为 2 的幂，例如 128、256、512。 */
/* 当前累计计数回绕后的数组下标计算依赖这个约束。 */
_Static_assert((SBUS_UART_DMA_BUFFER_SIZE & (SBUS_UART_DMA_BUFFER_SIZE - 1U)) == 0U,
               "Capacity must be a power of two for uint32_t progress wrap.");

#define FAULT_UART    (1UL << 0)
#define FAULT_DMA     (1UL << 1)
#define FAULT_OVERRUN (1UL << 2)
#define FAULT_LATE    (1UL << 3)
#define FAULT_STATE   (1UL << 4)

static uint8_t s_dma_buffer[SBUS_UART_DMA_BUFFER_SIZE];     /* DMA 写入，接收串口原始字节，会循环覆盖 */ 
static uint8_t s_work_buffer[SBUS_UART_DMA_BUFFER_SIZE];    /* SBUS 任务写入，保存本次要处理的稳定副本 */
static SbusReceiver s_receiver;     /* 跨多次调用保存组帧进度 */
static osThreadId_t s_owner;        /* 哪个任务拥有这个接收模块 */
static bool s_owned;                /* 是否已经被任务领取，避免重复启动 */
static bool s_receiving;            /* 是否处于已启动接收的阶段 */
static uint32_t s_faults;           /* 当前锁存的故障位 */
                                    /* FAULT_UART 等宏采用不同二进制位，因此 s_faults 可以同时记录多个故障 */
                                    /* s_faults |= FAULT_UART | FAULT_OVERRUN; */
static uint16_t s_dma_position;     /* 上次观察到的 DMA 环形写入位置 */
static uint32_t s_written;          /* 从本轮接收开始，软件通过采样 DMA 位置，累计观察到的字节数 */
static uint32_t s_consumed;         /* 本轮接收中，任务已经安全复制到工作数组的累计字节数 */
static uint32_t s_observed_ms;      /* 最新一次观察到新增字节的时间 */
static SbusUartStats s_stats;       /* 事件、错误、恢复次数等诊断信息 */

/**
 * @brief   进入 UART 临界区，使用 PRIMASK 保存当前屏蔽现场，之后屏蔽所有中断，开启内存屏障
 * 
 * @return  uint32_t __get_PRIMASK() 的返回值
 */
static uint32_t uart_lock(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}

/**
 * @brief   释放 UART 临界区，恢复保存的现场
 * 
 * @param   saved   之前保存的屏蔽现场
 */
static void uart_unlock(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}

/**
 * @brief   通知接收模块的拥有者任务（sbusTask），让其检查并处理接收数据或故障
 * 
 * @note    是一种任务通知 Task Notification，工作流程如下：
 *          1. sbusTask 等待标志，处于阻塞状态
 *          2. 接收事件或错误发生，调用 wake_owner()
 *          3. 设置标志，满足任务等待条件
 *          4. 任务解除阻塞，获得 CPU 后执行 process()
 */
static void wake_owner(void)
{
    if (s_owner != NULL)
    {
        /* CMSIS-RTOS V2 支持从普通 ISR 设置线程标志，不创建队列或分配内存。 */
        (void)osThreadFlagsSet(s_owner, SBUS_UART_THREAD_FLAG);
    }
}

/**
 * @brief   处理新传入的错误/锁存故障
 *          对 OVERRUN 和 LATE 错误进行计数，将新增故障合并到故障合集
 *          更新 s_stats 状态，使快照失效
 * 
 * @note    立即使旧快照失效，不等待任务调度
 */
static void fault_locked(uint32_t faults)
{
    const uint32_t new_faults = faults & ~s_faults;     // 去掉已经记录过的故障，只留下新增部分

    /* 对新增故障计数 */
    if ((new_faults & FAULT_OVERRUN) != 0U) ++s_stats.buffer_overruns;  // 缓冲积压/覆盖
    if ((new_faults & FAULT_LATE) != 0U) ++s_stats.irq_late_events;     // HT、TC 标志积压

    s_faults |= faults;         // 将本次所有故障合并到故障合集
    s_stats.running = false;    // 表示诊断状态已经不再是正常运行
    rc_snapshot_invalidate();   // 将共享快照的 transport_ok 清除为 false，防止消费者使用
}

/**
 * @brief   错误检查，包括 USART、DMA 错误和 HT/TC 积压情况
 * 
 * @return  uint32_t，多个 bit 组合表示多个错误
 * 
 * @note    不能仅依赖 ErrorCallback：另一个 IRQ 或任务可能先观察到坏数据
 */
static uint32_t hardware_faults(void)
{
    uint32_t faults = 0U;

    /* PE、FE、NE、ORE 检查 */
    if ((huart1.Instance->ISR & (USART_ISR_PE | USART_ISR_FE |
                                 USART_ISR_NE | USART_ISR_ORE)) != 0U)
    {
        faults |= FAULT_UART;
    }

    /* DMA 传输错误 TE 检查 */
    if (__HAL_DMA_GET_FLAG(huart1.hdmarx,
                          __HAL_DMA_GET_TE_FLAG_INDEX(huart1.hdmarx)) != 0U)
    {
        faults |= FAULT_DMA;
    }

    /* 两个边界标志同时积压时，保守丢弃；标志只有一位，不能计数多圈。
     * 必须在 HAL_DMA_IRQHandler 清标志前检查（见 IRQ 前置钩子）。
     */
    if ((__HAL_DMA_GET_FLAG(huart1.hdmarx,
                           __HAL_DMA_GET_HT_FLAG_INDEX(huart1.hdmarx)) != 0U) &&
        (__HAL_DMA_GET_FLAG(huart1.hdmarx,
                           __HAL_DMA_GET_TC_FLAG_INDEX(huart1.hdmarx)) != 0U))
    {
        faults |= FAULT_LATE;
    }

    return faults;
}

/**
 * @brief   检查硬件是否正常，计算 DMA 写入了多少字节，更新累计计数，并判断未读数据是否已经挤压到危险程度
 *          如果有 USART、DMA 或者 HT、TC 积压，或者传输启动后状态错误，就进入错误处理，提前 return 
 *          如果没有错误，就更新 DMA 搬运索引、新增字节数量和时间戳
 *          检查 overrun，即未读数据是否积压到容量上限
 * 
 * @note    持锁采样实际 CNDTR
 *          不直接用回调 Size 或 HT/TC 事件边界更新环形位置
 */
static void observe_locked(void)
{
    /* 尚未启动接收，或已经锁存故障，就返回。 */
    if (!s_receiving || s_faults != 0U) return;

    uint32_t faults = hardware_faults();                                    // 检查硬件错误
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(huart1.hdmarx);        // 读取剩余传输数量
    faults |= hardware_faults();                                            // 补查读取计数期间出现的硬件错误
    
    /* 检查 DMA 剩余传输大小、UART 硬件开关、DMA 硬件开关 */
    if (remaining > SBUS_UART_DMA_BUFFER_SIZE ||
        huart1.RxState != HAL_UART_STATE_BUSY_RX ||
        (huart1.Instance->CR3 & USART_CR3_DMAR) == 0U ||
        (huart1.hdmarx->Instance->CCR & DMA_CCR_EN) == 0U)
    {
        faults |= FAULT_STATE;
    }
    
    /* 错误处理 */
    if (faults != 0U)
    {
        fault_locked(faults);
        return;
    }

    /* 更新 DMA 搬运索引和新增字节数量 */
    /* position：这次观察到的 DMA 下一写入下标 */
    /* delta：从上次观察到现在，DMA 新写入了多少字节 */
    const uint16_t position = (uint16_t)((SBUS_UART_DMA_BUFFER_SIZE - remaining) %
                                          SBUS_UART_DMA_BUFFER_SIZE);
    const uint32_t delta = (position + SBUS_UART_DMA_BUFFER_SIZE - s_dma_position) %
                           SBUS_UART_DMA_BUFFER_SIZE;
    s_dma_position = position;
    s_written += delta;

    /* 更新时间戳 */
    if (delta != 0U) s_observed_ms = HAL_GetTick();

    /* 检查 DMA 已经写入、但任务还没安全取走的数据，是否到达了缓冲区容量上限 */
    if ((uint32_t)(s_written - s_consumed) >= SBUS_UART_DMA_BUFFER_SIZE)
    {
        /* 即使刚好满一圈也丢弃，避免零余量时 DMA 继续改写未读字节。 */
        fault_locked(FAULT_OVERRUN);
    }
}

/**
 * @brief   检查串口和 DMA 配置，以及 IRQ 优先级
 * 
 * @return  如果没有问题，返回 true；如果有错误，返回 false
 */
static bool configuration_ready(void)
{
    if (huart1.Instance != USART1 || huart1.hdmarx == NULL ||                   // USART1 和 DMA 检查
        huart1.RxState != HAL_UART_STATE_READY ||                               // USART 接收过程状态检查
        (huart1.Init.Mode & UART_MODE_RX) == 0U ||                              // USART Mode 配置检查
        /* 字节 DMA 不支持 9 个有效数据位。8 数据位 + 校验仍可按字节保存。 */
        (huart1.Init.WordLength == UART_WORDLENGTH_9B &&
         huart1.Init.Parity == UART_PARITY_NONE)) return false;

    const DMA_HandleTypeDef *dma = huart1.hdmarx;                               // 通过 DMA 只读访问接收 DMA 句柄
    return dma->Instance == DMA1_Channel1 && dma->Parent == &huart1 &&          // 检查 DMA 通道及所属 USART
        dma->State == HAL_DMA_STATE_READY &&                                    // DMA 过程状态检查
        dma->Init.Request == DMA_REQUEST_USART1_RX &&                           // DMA 请求来源检查
        dma->Init.Direction == DMA_PERIPH_TO_MEMORY &&                          // DMA 方向检查
        dma->Init.PeriphInc == DMA_PINC_DISABLE &&                              // DMA 外设地址自增检查
        dma->Init.MemInc == DMA_MINC_ENABLE &&                                  // DMA 内存地址自增检查
        dma->Init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE &&                 // DMA 外设端数据宽度检查
        dma->Init.MemDataAlignment == DMA_MDATAALIGN_BYTE &&                    // DMA 内存端数据宽度检查
        dma->Init.Mode == DMA_CIRCULAR &&                                       // DMA 模式检查
        /* 优先级检查 */
        NVIC_GetPriority(USART1_IRQn) >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY &&
        NVIC_GetPriority(DMA1_Channel1_IRQn) >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
}

/**
 * @brief   开始接收函数
 *          重置组帧器，临界区中初始化静态记录数据
 *          启用 USART1 + DMA
 *          临界区中保存启动结果，进行一次 observe，判断启动状态和错误，更新 s_stats
 *          根据启动状态和错误，判断中止当前 USART 操作，还是
 * 
 * @return  bool
 * 
 * @note    既用于首次启动，也用于故障后的重新启动
 *          只由所属任务调用。HAL 调用放在临界区外，不带锁等待硬件。
 */
static bool begin_receive(void)
{
    /* 重置组帧器和本轮接收进度 */
    /* 这里清除的是接收层状态，历史共享快照仍然保留 */
    sbus_receiver_init(&s_receiver);            // 重置组帧器
    uint32_t saved = uart_lock();               // 进入临界区
        s_dma_position = 0U;                        // 上次观察到的 DMA 环形写入位置清零
        s_written = 0U;                             // 本轮累计观察到的字节数清零
        s_consumed = 0U;                            // 本轮已安全复制到工作数组的累计字节数清零
        s_faults = 0U;                              // 清除本模块锁存的软件故障位
        s_receiving = false;                        // 暂时标记为未处于接收运行阶段
    uart_unlock(saved);                         // 退出临界区

    /* 启用 USART1 + DMA，HT/TC/IDLE 都保留 */
    /* 函数返回后，数组必须仍然存在，所以定义为 static */
    const HAL_StatusTypeDef result = HAL_UARTEx_ReceiveToIdle_DMA(
        &huart1, s_dma_buffer, (uint16_t)sizeof(s_dma_buffer));

    saved = uart_lock();                        // 进入临界区
        s_receiving = result == HAL_OK;             // 将启动接收结果保存到 s_receiving
        
        observe_locked();

        const bool ok = result == HAL_OK && s_faults == 0U;

        if (ok)
        {
            s_stats.running = true;
        }
        else
        {
            s_receiving = false;
            s_stats.running = false;
            s_stats.terminal_fault = true;
            ++s_stats.restart_failures;
            rc_snapshot_invalidate();
        }
    uart_unlock(saved);

    if (!ok) (void)HAL_UART_AbortReceive(&huart1);  // 终止当前 USART 接收操作，并清理接受相关状态
    else wake_owner();                  // 启动后主动通知一次，补偿启动窗口内可能被忽略的接收事件
    return ok;
}

/**
 * @brief   USART 启动函数，由 SBUS 任务调用
 *          判断资源占用状态、占用者 ID、内核状态、USART 和 DMA 配置
 *          记录占用者身份，更新占用状态
 *          调用 begin_receive()，进行后续动作
 * 
 * @return  bool，begin_receive() 的结果
 */
bool sbus_uart_start(void)
{
    if (s_owned || osKernelGetState() != osKernelRunning ||
        osThreadGetId() == NULL || !configuration_ready()) return false;

    /* 记录调用任务的身份，之后接收事件就通知这个任务。 */
    s_owner = osThreadGetId();
    s_owned = true;
    return begin_receive();
}

/**
 * @brief   清理故障接收，重新启动
 * 
 * @note    重启成功只表示恢复了接收，不表示旧快照重新有效；必须等下一帧成功发布
 */
static void recover_receive(void)
{
    uint32_t saved = uart_lock();
        s_receiving = false;        // 标记当前不再处于正常接收阶段，让相关回调暂时忽略接收事件
        rc_snapshot_invalidate();   // 使共享快照的 transport_ok 变成 false，防止消费者继续信任旧数据
    uart_unlock(saved);

    /* HAL 的 DMA 错误路径可能已停止接收，这里统一清理并丢弃残留字节 */
    /* 在临界区外调用，因为可能涉及等待，不适合长时间关中断执行 */
    const HAL_StatusTypeDef result = HAL_UART_AbortReceive(&huart1);

    /* 确认函数返回值、DMA HAL 状态已就绪、DMA 通道实际已经关闭 */
    /* 如果停止失败，记录 s_stats 故障状态，提前 return */
    if (result != HAL_OK || huart1.hdmarx->State != HAL_DMA_STATE_READY ||
        (huart1.hdmarx->Instance->CCR & DMA_CCR_EN) != 0U)
    {
        saved = uart_lock();
            ++s_stats.restart_failures;
            s_stats.terminal_fault = true;
            s_stats.running = false;
        uart_unlock(saved);
        return;
    }

    /* 如果停止成功，就重新初始化组帧状态和接受进度，再启动 DMA，成功后累计一次重启次数 */
    if (begin_receive())
    {
        saved = uart_lock();
            ++s_stats.restarts;
        uart_unlock(saved);
    }
}

/**
 * @brief   
 * 
 */
void sbus_uart_process(void)
{
    /* 1. 确认调用者和运行条件 */
    if (!s_owned || osThreadGetId() != s_owner || s_stats.terminal_fault) return;

    /* 2. 观察 DMA，确定这一批要处理的数据 */
    uint32_t saved = uart_lock();
        observe_locked();
        /* 如果发现故障，先退出临界区，再执行恢复，不解析这批数据 */
        if (s_faults != 0U)
        {
            uart_unlock(saved);
            recover_receive();
            return;
        }

        /* 没有故障时，取出三个信息 */
        const uint32_t count = s_written - s_consumed;                  // 这一批尚未取走的字节数量
        const uint32_t start = s_consumed % SBUS_UART_DMA_BUFFER_SIZE;  // 这一批在 DMA 环形数组中的起始下标
        const uint32_t observed_ms = s_observed_ms;                     // 此次批次使用的接收观察时间
    uart_unlock(saved);

    /* 如果没有新字节，直接返回 */
    if (count == 0U) return;

    /* 3. 把数据复制到稳定的工作数组 */
    /* DMA 不受 PRIMASK 控制！先复制，再重新检查累计写进度。
     * 只在副本确认未被覆盖后解析；复制期间保持中断开启。
     */
    const volatile uint8_t *source = s_dma_buffer;
    for (uint32_t i = 0U; i < count; ++i)
    {
        s_work_buffer[i] = source[(start + i) % SBUS_UART_DMA_BUFFER_SIZE];
    }

    /* 4. 复制后再检查，确认可以使用副本 */
    saved = uart_lock();
        observe_locked();
        /* 如果发现缓冲覆盖等故障，退出临界区，恢复接收，这份数据不再拿去解码 */
        if (s_faults != 0U)
        {
            uart_unlock(saved);
            recover_receive();
            return;
        }

        /* 没有检测到故障，才提交消费进度 */
        s_consumed += count;
    uart_unlock(saved);

    /* 5. 逐字节组帧，成功后发布快照 */
    SbusFrame frame;
    for (uint32_t i = 0U; i < count; ++i)
    {
        const bool full_candidate = s_receiver.length == SBUS_FRAME_SIZE - 1U;
        const bool decoded = sbus_receiver_push_byte(&s_receiver, s_work_buffer[i], &frame);
        if (full_candidate && !decoded)
        {
            saved = uart_lock();
            ++s_stats.rejected_candidates;
            uart_unlock(saved);
        }
        if (decoded)
        {
            saved = uart_lock();
                observe_locked();
                if (s_faults == 0U)
                {
                    ++s_stats.decoded_frames;
                    if (!frame.frame_lost && !frame.failsafe) ++s_stats.valid_frames;
                    /* 批次尾字节的 DMA 进度观察时间，非逐帧硬件时间戳。 */
                    (void)rc_snapshot_publish(&frame, observed_ms);
                }
            uart_unlock(saved);
        }
    }

    /* 得到完整帧后，再检查一次接收状态 */
    saved = uart_lock();
        const bool faulted = s_faults != 0U;
    uart_unlock(saved);

    /* 退出前再检查故障 */
    if (faulted) recover_receive();
}

/**
 * @brief   在 HAL 清标志前观察，发现故障时，唤醒所属任务
 */
void sbus_uart_before_dma_irq(void)
{
    const uint32_t saved = uart_lock();
        observe_locked();
        const bool faulted = s_faults != 0U;
    uart_unlock(saved);
    if (s_owned && faulted) wake_owner();
}

/**
 * @brief   前置钩子
 */
void sbus_uart_before_uart_irq(void)
{
    sbus_uart_before_dma_irq();
}

/**
 * @brief   接收事件通知，处理 HT、TC、IDLE 接收事件
 */
void sbus_uart_on_rx_event(uint16_t size)
{
    /* 句柄已由 uart_callbacks.c 分派，这里只检查本模块运行阶段。 */
    if (!s_receiving) return;

    (void)size; /* 位置取 CNDTR，避免迟到的 HT/TC Size 引起重复/倒退。 */
    const uint32_t saved = uart_lock();
        /* 统计事件次数 */
        const HAL_UART_RxEventTypeTypeDef event = HAL_UARTEx_GetRxEventType(&huart1);
        if (event == HAL_UART_RXEVENT_HT) ++s_stats.ht_events;
        else if (event == HAL_UART_RXEVENT_TC) ++s_stats.tc_events;
        else if (event == HAL_UART_RXEVENT_IDLE) ++s_stats.idle_events;
        observe_locked();
    uart_unlock(saved);
    wake_owner();
}

/**
 * @brief   记录错误，交给任务恢复
 */
void sbus_uart_on_error(void)
{
    /* 仅 USART1 错误由统一回调转交到这里。 */
    if (!s_receiving) return;

    const uint32_t saved = uart_lock();
        ++s_stats.uart_error_events;
        s_stats.last_hal_error = huart1.ErrorCode;
        fault_locked(FAULT_UART);
    uart_unlock(saved);
    wake_owner();
}

/**
 * @brief   安全读取统计信息
 * 
 * @param   out 调用者提供的容器
 * 
 * @return  bool
 * 
 * @note    不清空统计值，也不负责恢复故障，返回 true 只表示复制成功，不代表链路健康
 */
bool sbus_uart_get_stats(SbusUartStats *out)
{
    /* Check */
    if (out == NULL) return false;

    const uint32_t saved = uart_lock();
        *out = s_stats;

    uart_unlock(saved);

    return true;
}

