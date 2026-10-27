# 固件架构与阅读路线（2026-10-27）

## 1. 层次和所有权

| 层 | 模块 | 执行上下文和职责 |
|---|---|---|
| 纯算法 | sbus_decoder | 25 字节候选格式 → 通道/协议状态，无 HAL/RTOS |
| 字节流 | sbus_receiver | 保存半帧，候选失败后重新同步 |
| 共享数据 | rc_snapshot | PRIMASK 短临界区复制整份最新帧，故障立即失效 |
| 有效性 | rc_status | 对稳定快照按指定时间/阈值计算，不修改共享数据 |
| SBUS 接收 | sbus_uart | DMA/工作副本；中断采样，唯一 SBUS 任务解码/发布/恢复 |
| HAL 分派 | uart_callbacks | 唯一公共回调，按 huart1/huart3 转交业务模块 |
| 原始 485 字节 | rs485_port | USART3 单字节 RX IT，512 个带时间戳字节缓存；异步 TX IT |
| RTU 边界 | modbus_rtu | 唯一 Modbus 任务按静默间隔组请求，不用 IDLE 代替帧边界 |
| 协议 | modbus_crc / modbus_slave | CRC、地址、03 查询、异常应答、容量检查 |
| 寄存器 | register_map | 同一快照与状态 → 固定 28 个 uint16；不重排通道 |
| 配置/应用 | bridge_config / bridge_service | 显式批准配置、任务调度、年龄/诊断状态、应答协调 |

DMA 数组由硬件写；SBUS 工作数组只由 SBUS 任务使用。快照只由 SBUS 任务发布，
普通 ISR 可使其失效。Modbus 任务读取副本后构造寄存器及应答，不把输入帧排队。
诊断计数来自独立采样，只保证通道/帧状态/序号/received_ms 来自同一快照，
不声称 UART、Modbus 计数都与该帧原子同步。

## 2. 启动顺序

1. main 初始化 GPIO、DMA、USART1/USART3、HAL 时基和 RTOS 内核。
2. `MX_FREERTOS_Init` 在任务运行前初始化快照，然后静态创建三个任务并检查句柄。
3. `StartSbusTask` 启动 USART1 DMA；等待线程标志，醒来调用 process。
4. `StartModbusTask` 调用 `bridge_service_start(bridge_config_get())`。
   默认 enabled=false，不启动 USART3 RX，不触碰 DE；仍周期计算诊断状态。
5. `StartDiagTask` 保持原 500 tick 心跳，复制诊断到 `g_bridgeDiagnostics`，不打印阻塞日志。

当前 RTOS 1000 Hz；Modbus 任务等待 RS485_THREAD_FLAG 的最长时间为 1 tick。
此周期仅决定处理延迟，不是 RTU 静默阈值；微秒计时独立使用 DWT。

## 3. 两个串口的中断链

```text
DMA1_Channel1 IRQ → SBUS 前置钩子 → HAL_DMA_IRQHandler
USART1 IRQ       → SBUS 前置钩子 → HAL_UART_IRQHandler
                   ↓ HAL 接收事件/错误
                 uart_callbacks → sbus_uart_on_* → 通知 SBUS 任务

USART3 IRQ       → RS485 前置钩子 → HAL_UART_IRQHandler
                   ↓ HAL 单字节 RX / UART TC / 错误
                 uart_callbacks → rs485_port_on_* → 通知 Modbus 任务
```

Set/Wait 是 CMSIS-RTOS V2 线程标志，当前底层使用 FreeRTOS 任务通知，不传字节。
同一标志可合并，实际数据在数组，故障/进度在模块状态；不能用通知次数算帧数。
HAL 回调移到了 uart_callbacks.c，不能再在各业务文件定义同名强符号。
保留 USART1 的 HT/TC 和全部 IRQ 前置钩子。

## 4. 一次 PLC 事务

1. IRQ 把 USART3 字节及接收完成观察时间写入有界数组，然后重挂 1 字节接收。
2. Modbus 任务每次最多取 512 个字节，检查 RTU 静默/字节间隔；太迟导致旧请求
   已被下一事务超过时，丢弃旧请求，不排队旧应答。
3. 字节队列为空且硬件没有正在接收字符时，才可能结束 RTU 请求。
4. 复制最新快照并取 HAL 毫秒时间，计算有效性，构造整张寄存器副本。
5. CRC/地址合法的本站请求进入从站；03 回复连续范围，写请求异常拒绝。
6. 端口再次确认没有新接收活动，复制本次应答，停止 RX，置 DE，等待配置的 setup。
7. `HAL_UART_Transmit_IT` 开始发送，任务继续运行、SBUS 可抢占；没有阻塞发包等待。
8. UART TC 回调确认移位寄存器发完后释放 DE、清理回显/残留并重挂 RX。
9. 后续请求重新复制最新快照。收到合法本站流量才刷新 PLC 活性时间。

应答是构造时的快照，不是“发送的每一位都实时更新”。PLC 须考虑串行传输/轮询
延迟并自行实现序号停滞及通信超时保护。发送超时包括 DE 准备与传输，需留任务调度余量。

## 5. 故障与状态

SBUS UART/DMA 错误、覆盖、HT/TC 积压：立即清 transport_ok；任务丢弃半帧并恢复。
恢复成功不使旧帧有效，须等新 publish。启动/恢复失败保留 terminal_fault，不无限重试。

RS-485 UART 错误、原始数组满或发送失败/超时：记录故障代数、立即释放 DE，任务
abort 收发并丢弃残字节，RTU 重新等待静默。清理/重挂失败进入 terminal_fault。
错误中断不解码、不运行阻塞恢复，也不修改 SBUS 快照。

服务状态优先级：DISABLED → CONFIG_FAULT → COMM_FAULT → STARTUP →
SBUS_FAILSAFE → SBUS_LOST → INPUT_FAULT → ACTIVE（前面的条件成立则优先报告）。
这只是诊断选择顺序，不是车辆模式状态机。原始原因仍可在 input/port/modbus 中查看。
COMM_FAULT 表示端口失败或批准的 PLC 活性超时；不把它伪装成 SBUS failsafe。

无输入事件时也周期计算年龄。服务层持续累积同一旧快照的 uint64 年龄，避免 HAL
毫秒计数绕完整一圈后旧数据重新可用。相邻服务调用仍必须少于 2^32 ms，HAL 时基
必须持续运行；它不能解决挂死、停钟或未执行服务的问题。

## 6. DWT、缓冲与实时边界

- DWT 周期计数累积转换到 uint32 微秒，保存换算余数，周期寄存器自身回绕可处理。
  固定主频、特权模式、至少每秒采样；不支持动态频率/STOP/调试暂停后的实时继续。
- RX 时间戳是 IRQ 观察值，不是硬件捕获。两次完成时间差减去一个字符时长估计静默。
  极限间隔与高优先级抢占必须用逻辑分析仪验证；不声称已经达到 Modbus 时序符合性。
- `BUSY`/RXNE 检查减少在下一字节进行中错误结束帧；它不提供多主站总线仲裁。
  假设 PLC 遵循单主站请求-应答，错误流量可保守丢弃，不保证任意输入都得到回复。
- 关中断不停止 DMA。SBUS 缓冲的正确性仍受其 IRQ/复制延迟前提约束，见 UART 测试文档。
- 1 字节 IT 在高波特率下中断较多；先测最坏执行时间/栈余量，再决定是否替换为 DMA。
- 当前未启用看门狗，也不输出执行器动作。需经健康条件及安全评审后再增加喂狗。

## 7. 阅读路径

先看 README 的图和 app_freertos.c，再按 decoder → receiver → snapshot → status →
UART → 回调/IRQ → CRC → 寄存器 → slave → RTU → port → config → service 阅读。
每个模块先读头文件契约，再读正常路径，最后读失败分支与测试。不要一开始沿 HAL
所有函数展开；只有需要确认 TC、错误恢复或回调来源时再进入驱动实现。
