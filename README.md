# G474RE：SBUS → 最新快照 → Modbus RTU / RS-485

本仓库的第一版固件软件链路已实现，并有分层主机模型测试；尚未完成真实 GR01、
外置 RS-485 收发器、PLC 和车辆安全联调。不能据此认定已经降低实测延迟。

当前默认：USART1 保持已有 SBUS 候选配置并运行；**Modbus/USART3 服务默认禁用**。
`App/bridge_config.c` 中配置全部保持未确认状态，禁止为了消除配置故障随意填值。
CubeMX 的 USART3 初始化仍会执行，但禁用服务不会启动其接收或发送应答。

## 先看架构

```text
USART1 循环 DMA → sbus_uart → sbus_receiver → sbus_decoder
                         │                       │
                         └── 接收故障失效 ──────┤
                                                 ▼
                                             rc_snapshot
                                                 │ 一次复制
                                                 ▼
PLC 请求 → USART3 IT → modbus_rtu → bridge_service + rc_status
                                                 │
                                          register_map
                                                 │
                                          modbus_slave + CRC
                                                 │
                                         rs485_port 异步应答
                                                 ▼
                                         外置收发器 → PLC
```

中断只搬入原始字节/采样进度、记录故障并通知；协议在任务中处理。没有遥控帧
待发送队列，没有通道到车辆动作的映射。三个静态任务的数量、优先级与栈配置未改。

## 文档入口

- [软件架构与阅读路线](doc/software_architecture.md)：任务、启动、数据所有权、错误路径。
- [候选寄存器协议](doc/modbus_registers.md)：28 个寄存器、状态位、字序、功能码与测试向量。
- [上板检查清单](doc/bench_validation.md)：需要确认的配置、接线和硬件验证步骤。
- [全套软件测试](Code/G474RE_SBUS2RS485/Tests/README_bridge.md)：复跑方式及测试边界。

建议全部写完后按以下顺序阅读：

1. `app_freertos.c`：先知道三个任务做什么。
2. `sbus_decoder` → `sbus_receiver` → `rc_snapshot` → `rc_status`。
3. `sbus_uart` → `uart_callbacks` → `stm32g4xx_it.c`。
4. `modbus_crc` → `register_map` → `modbus_slave` → `modbus_rtu`。
5. `rs485_port` → `bridge_config` → `bridge_service`。
6. 对应测试文件：先正常路径，再边界、故障与恢复。

## 编译和测试

固件：使用现有 EIDE Debug 目标，App 已作为源目录；不要添加 Tests。
电脑端：在 `Code/G474RE_SBUS2RS485/Tests` 中运行：

```powershell
.\run_all.cmd
# gcc 不在 PATH 时：
.\run_all.cmd "D:\MinGW\mingw64\bin\gcc.exe"
```

脚本严格编译 8 个测试程序、运行 15 个场景，失败即停止，不会运行旧测试产物冒充成功。
所有 HOST_TEST 宏只属于主机测试，不得加入 EIDE。构建产物位于忽略的 build 目录。

## 发布边界

- 软件实现和测试通过不等于物理串口格式、反相、DE 时序已验证。
- 尚未确认正式从站地址、寄存器基址、字序、失联阈值、PLC 端口与接线。
- 没有启用看门狗：喂狗健康条件、故障恢复时限和车辆安全策略尚需评审，不能无条件喂狗。
- 没有改动 PLC 工程，也没有烧录或驱动执行机构。代码公开不等于完成实物验证。
- 急停、停车与控制权互锁必须由 PLC/独立安全链路实现，不能依赖此模块继续正常运行。
