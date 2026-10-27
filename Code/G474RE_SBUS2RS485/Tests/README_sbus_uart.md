# SBUS UART/DMA 接收测试与上板观察

## 当前调用链

`MX_FREERTOS_Init()` 在任务运行前初始化共享快照。调度器启动后，唯一的
`StartSbusTask()` 调用一次 `sbus_uart_start()`，然后阻塞等待线程标志，醒来调用
`sbus_uart_process()`。本次构建烧录后将实际启用 USART1 接收，不再是占位任务。
USART1 波特率、反相、引脚等沿用 CubeMX 候选配置，仍须验证 GR01 实际输出。

- `DMA1_Channel1_IRQHandler` / `USART1_IRQHandler` 的 USER CODE 0 区先调用
  `sbus_uart_before_*_irq()`，再调用原 HAL handler。这两个前置钩子不可省略：
  必须在 HAL 清标志前识别挂起的 HT+TC、DMA TE 和 UART PE/FE/NE/ORE。
- `HAL_UARTEx_RxEventCallback()` 只记录 HT/TC/IDLE、采样实际 CNDTR、通知任务；
  不把回调 Size 或 IDLE 解释为帧边界。启动后保留 HT、TC、错误和 IDLE 中断。
- `HAL_UART_ErrorCallback()` 保存 HAL 错误位、使快照失效并通知任务；
  任务负责 abort、重置组帧器、丢弃残帧并重启接收，不在 ISR 调用阻塞恢复。
- 2026-10-27：公共 HAL 回调统一位于 `App/uart_callbacks.c`，按 huart1/huart3
  分派到 SBUS/RS485。SBUS 实现改名为 sbus_uart_on_rx_event/on_error；不要重复定义
  HAL 同名回调。USART3 前置钩子仅用于其端口，不代替 USART1/DMA 的钩子。

## 为什么同时检查进度和硬件标志

单看环形下标，无法区别「没收到」和「已绕一圈」。这里按实际 CNDTR 的位置增量
维护累计写入量，任务维护累计消费量。IRQ 正常运行而任务停顿时，累计未读量达到
256 字节就丢弃恢复；IRQ 长时间不运行时，HT 与 TC 同时挂起表示圈数不可信，
即使只延迟半圈也保守丢弃。不能关闭 HT/TC 中断或移除 IRQ 前置钩子。

任务先把最多 255 个未读字节复制到独立的固定工作数组，再次检查写进度、硬件错误
及覆盖情况。检查通过才确认消费并解析副本。DMA 始终可能继续写 RAM，关中断不等于
停止 DMA。工作副本不是待发送帧队列，解码仍按收到的原始顺序进行。

从 IRQ 前置钩子到 HAL 清标志/回调的整个执行窗口（包含被更高优先级 ISR 抢占的时间），
以及采样临界区，都必须远短于 DMA 填满一个缓冲区的时间。若在 HAL 清标志之前被
抢占多圈，单比特标志仍可能丢失历史，不能声称任意长 IRQ 延迟都可可靠计数。
不能在其中打印、等待、单步停留或调用阻塞 HAL。必须上板测量最坏 IRQ 延迟和复制时间。
该实现不能证明任意调试暂停、硬件故障、内存破坏情况下无丢字节，也不能排除无 CRC
SBUS 的假帧。断点恢复后宜复位，不能把调试暂停后的数据当作实时输入。

## 快照与故障语义

- `has_frame` 表示发布过帧；`transport_ok` 表示发布后未检测到已知接收故障。
- PE/FE/NE/ORE、DMA 错误、未读覆盖或 HT/TC 积压会立即清除 `transport_ok`。
  保留通道、sequence、received_ms 供诊断；不伪造 failsafe，不重置序号。
- 接收恢复成功仍保持 `transport_ok=false`，直到新的结构合法帧发布；
  协议的 failsafe/frame_lost 即使置位也原样发布，消费者必须另行判断。
- `received_ms` 当前为该批次尾字节的 DMA 进度观察时间，使用 HAL_GetTick 毫秒时基，
  同一批次解析出的帧共用此时间。它不是精确的引脚接收时间，可能晚于实际到达时间，
  上板后需量化该误差，不能据此声称微秒级延迟测量或已经实现正式失联保护。
- 未收到新字节时不会自动清除 `transport_ok`；消费者还必须检查帧年龄。
  失联阈值、物理映射、寄存器状态映射仍待确认，本次不赋值。
- 启动/恢复失败进入 terminal_fault，不反复重启；正常运行后检测到接收故障则尝试
  一次清理和重启。本次没有看门狗或车辆安全动作。

## 电脑端测试

在本 Tests 目录执行，`gcc` 必须是 Windows 主机 GCC；固件不得定义两个 HOST_TEST 宏。
若 gcc 未加入 PATH，换成自己的完整路径，例如 `D:/MinGW/mingw64/bin/gcc.exe`。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DSBUS_UART_HOST_TEST -DRC_SNAPSHOT_HOST_TEST -I . -I ../App ../App/sbus_uart.c ../App/uart_callbacks.c ../App/rc_snapshot.c ../App/sbus_receiver.c ../App/sbus_decoder.c uart_callbacks_sbus_stubs.c test_sbus_uart.c -o build/test_sbus_uart.exe
if ($LASTEXITCODE -ne 0) { throw "UART test compilation failed" }
./build/test_sbus_uart.exe
./build/test_sbus_uart.exe start-failure
./build/test_sbus_uart.exe abort-failure
```

三个运行均须输出 PASS、退出码为 0。模型覆盖全部 24 个帧切分点、环形回绕、
无 IDLE 连续输入、重复/迟到回调、其他 UART 隔离、PE/FE/NE/ORE、DMA TE、
任务落后多圈、IRQ 延迟多圈、复制期间覆盖、恢复后新帧、毫秒时间回绕、
启动失败、停止失败和重启失败。模拟寄存器/回调不等于实际 STM32 外设测试。
snapshot 回归见 README_rc_snapshot.md；新增失效后历史字段保持和恢复测试。
现在也可直接运行 run_all.cmd，复跑 SBUS 与桥接软件的全部主机测试。

## 板上检查（尚未执行）

先只连接开发板，隔离 PLC 执行机构输出。用最新 ELF 烧录后观察：

1. `g_sbusStartOk` 应为 true，LED 仍闪烁；无输入时快照 has_frame/transport_ok
   均为 false，SBUS 任务阻塞，不能把初始化成功当作已收到遥控数据。
2. 可在调试器查看 sbus_uart.c 文件私有 `s_stats`、`s_written`、`s_consumed`；
   不建议给每个 IRQ 加断点。也可从低频诊断代码调用 `sbus_uart_get_stats()`。
3. 先确认信号源的电平、共地、针序、实际串口格式和极性，再接入可重复的已知帧。
   检查通道、状态、序号和时间戳，连续输入时 HT/TC 都应有进展。
4. 用测试信号源制造校验/停止位错误，检查错误计数、transport_ok 立即失效、
   restarts 增加，以及恢复后的新帧。断开信号线不一定产生 UART 错误，后续必须
   通过已批准的年龄阈值判定失联，不能期待本模块立刻报错。
5. 受控地延迟任务/IRQ，验证覆盖检测与恢复，并用仪器测量临界区/处理最坏耗时。
   本机测试没有执行烧录、硬件错误注入或延迟测量。
