# 全套主机测试与验证边界

## 一次复跑

在本目录执行（Windows 主机 GCC，不是 Arm GCC）：

```powershell
.\run_all.cmd
# 或显式指定本机编译器，不写入工程配置：
.\run_all.cmd "D:\MinGW\mingw64\bin\gcc.exe"
```

也提供 `run_all.ps1 -Compiler gcc`；当前自动执行环境的 PowerShell 启动失败
（0x8009001d），因此实际全套验证使用 CMD 入口，未声称 PS 入口已运行验证。
CMD 使用 C11、Wall/Wextra/Werror/pedantic/O2，每个编译/运行检查退出码，失败即停止。
不定义 NDEBUG；全部 HOST_TEST 宏仅出现在主机测试命令，禁止加入固件目标。

## 覆盖矩阵

| 程序 | 运行模式 | 验证内容 |
|---|---|---|
| test_sbus_decoder | 正常 | 32768 通道值、固定向量、状态/长度、失败输出不变 |
| test_sbus_receiver | 正常 | 分段、噪声、半帧、重同步、插删字节、独立滑窗参考 |
| test_rc_snapshot | 正常 | 副本、失效/发布、嵌套屏蔽、单写双读并发 |
| test_rc_status | 正常 | 1344 组合、时间边界/回绕、无阈值不可用 |
| test_sbus_uart | 正常 / start-failure / abort-failure | DMA 回绕/复制覆盖、IRQ 滞后、错误和恢复、回调分派后的回归 |
| test_modbus | 正常 | 独立 CRC 参考/固定向量/单比特翻转；全部 406 读范围、异常/广播/字序；RTU 间隔/长度；10000 变长请求/输出容量 |
| test_rs485_port | 正常 / start-failure / clock-failure | 唯一回调分派、带时间字节环形数组、覆盖、DE setup、异步 TX 稳定副本、TC 释放 DE、超时/停止失败 |
| test_bridge_service | 正常 / disabled / bad-config / start-failure | 快照→状态→寄存器→RTU 应答；不跨帧、超时/协议状态、恢复、全毫秒计数周期、默认禁用门控 |

共 8 个可执行程序、15 次独立运行。每个错误启动模式用新进程，避免静态一次性启动
状态被测试人为清零。构建文件放 build（Git 忽略），不把测试 main 加入 EIDE。

## 模型各自替代什么

- `sbus_uart_test_port.h`：替换 USART1/DMA/RTOS，`uart_callbacks_sbus_stubs.c`
  只在这套测试替代另一个端口；不是固件文件。
- `rs485_port_test_port.h`：替换 USART3/GPIO/DWT/RTOS；测试显式模拟 UART TC，
  不能证明实际外设的中断延迟或 DE 电气波形。
- `bridge_service_test_port.h` 与 service 测试中的假端口/快照：让真实的服务、
  状态、寄存器、CRC、RTU 和从站代码联动；不把它称为整机/物理链路端到端测试。
- snapshot 的 Windows 锁模型验证副本一致性，不模拟所有 Cortex-M 抢占细节。
- 合成地址、115200、100 ms 等数字仅为测试输入，不是最终设备参数/安全阈值。

## 当前记录

2026-10-27：CMD 全套运行通过；13 个 App 文件的 Cortex-M4 独立严格编译通过，
6 个新核心模块 GCC -fanalyzer 无诊断，EIDE 编译链接通过。可选 UBSan 尝试因本机
MinGW 缺少 libubsan 链接失败，没有执行，也不算通过。没有安装或更换工具链。
尚无真实 GR01/RS-485/PLC 联调、时序、延迟或安全验收。
真实验证清单在仓库 `doc/bench_validation.md`，不能用 PASS 文本替代实测波形。
