#include "rc_snapshot.h"

#if defined(RC_SNAPSHOT_HOST_TEST)
/* 仅电脑端测试替换中断指令；固件不得定义此宏。 */
#include "rc_snapshot_test_port.h"
#else
#include "stm32g4xx.h"
#endif

static RcSnapshot s_snapshot = {0};

/**
 * @brief   保存状态，再关中断
 * 
 * @return  uint32_t 进入之前的中断屏蔽状态
 */
static uint32_t snapshot_lock(void)
{
    /* 读取 PRIMASK 寄存器最低位，保存到 saved_primask */
    const uint32_t saved_primask = __get_PRIMASK(); 

    /* 将 PRIMASK 设置为 1，屏蔽中断，用于任务切换的 PendSV 也会被屏蔽，
       但不会屏蔽 NMI、HardFault，也不会停止 DMA */
    __disable_irq();

    /* Data Memory Barrier， 数据内存屏障 */
    /* 保证屏障前后的内存访问按要求有序，不让访问顺序越过这条边界 */
    __DMB();

    /* 返回旧状态，交给调用者保管 */
    return saved_primask;
}

/**
 * @brief   恢复进入之前的状态
 * 
 * @param   saved_primask   原本的 PRIMASK 状态，本质是 PRIMASK 寄存器最低位
 */
static void snapshot_unlock(uint32_t saved_primask)
{
    __DMB();

    /* 恢复原本的 PRIMASK 状态 */
    /* 调用前可能已关中断，因此必须恢复原状态，而不是直接开中断。 */
    __set_PRIMASK(saved_primask);
}

/**
 * @brief   初始化共享快照，序号清零，has_frame = false 
 * 
 * @note    在生产者和消费者启动前调用，不用于运行中的失联处理。
 */
void rc_snapshot_init(void)
{
    const RcSnapshot empty_snapshot = {0};
    const uint32_t saved_primask = snapshot_lock();
    s_snapshot = empty_snapshot; /* 包括 received_ms 在内，全部字段归零。 */
    snapshot_unlock(saved_primask);
}

/**
 * @brief   发布新结果，复制 SbusFrame，更新时间，计数加一，更新 has_frame
 * 
 * @param   frame           传入的 SbusFrame，要更新到共享快照模块里
 * @param   received_ms     帧接收时间，单位 ms；调用者使用统一的毫秒时基。
 * 
 * @return  true 已复制；false 输入为 NULL，快照不变。
 * 
 * @note    输入须稳定且已通过解码检查；failsafe 和丢帧状态也要原样发布。
 */
bool rc_snapshot_publish(const SbusFrame *frame, uint32_t received_ms)
{
    if (frame == NULL)
    {
        return false;
    }

    const uint32_t saved_primask = snapshot_lock();
    s_snapshot.frame = *frame; /* 复制指针指向的结构体，不是保存指针。 */
    s_snapshot.received_ms = received_ms;
    ++s_snapshot.sequence;
    s_snapshot.has_frame = true;
    s_snapshot.transport_ok = true;
    snapshot_unlock(saved_primask);

    return true;
}

void rc_snapshot_invalidate(void)
{
    const uint32_t saved_primask = snapshot_lock();
    s_snapshot.transport_ok = false;
    snapshot_unlock(saved_primask);
}

/**
 * @brief   将共享快照复制到调用者自己的 RcSnapshot 中
 * 
 * @param   out     调用者提供的独立可写副本空间。
 * 
 * @return  true 复制成功（不代表数据有效）；false 输出指针为 NULL。
 * 
 * @note    读取者仍须检查 has_frame、协议状态和时效。
 */
bool rc_snapshot_read(RcSnapshot *out)
{
    if (out == NULL)
    {
        return false;
    }

    const uint32_t saved_primask = snapshot_lock();
    *out = s_snapshot; /* 整个复制过程受保护，不会混入另一帧。 */
    snapshot_unlock(saved_primask);

    return true;
}

