# 快照主机测试

在此 Tests 目录下用 Windows MinGW GCC 编译。先创建 build 目录（已有则跳过）。
PowerShell 命令如下；如果 gcc 不在 PATH，用本机 gcc.exe 完整路径替换 gcc。

```powershell
New-Item -ItemType Directory -Force build
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DRC_SNAPSHOT_HOST_TEST -I . -I ../App ../App/rc_snapshot.c test_rc_snapshot.c -o build/test_rc_snapshot.exe
```

确认 `$LASTEXITCODE` 为 0 后再运行，避免执行旧程序：

```powershell
.\build\test_rc_snapshot.exe
```

- 不定义 NDEBUG，不把 Tests 添加到 STM32 源文件列表。
- RC_SNAPSHOT_HOST_TEST 仅用于这条主机测试命令，不添加到 EIDE 固件宏定义。
- rc_snapshot_test_port.h 及测试中的 Windows 临界区仅替代 CMSIS 中断操作；不能替代板上中断和时序验证。
- 测试包括 30000 次发布、两个读线程各 30000 次读取；检查同一快照内通道、状态、时间、序号对应关系。
- UART 接入后新增 transport_ok 和 invalidate 接口：测试故障失效保留历史字段，下一次发布恢复 transport_ok；该标志不代替帧年龄或 failsafe 检查。
- 序号的 uint32_t 自然回绕在接口中约定，本测试没有遍历 2^32 次发布；时间戳 UINT32_MAX 到 0 的保存已覆盖。
- 解码、组帧测试仍使用各自独立的 main 和编译命令。
