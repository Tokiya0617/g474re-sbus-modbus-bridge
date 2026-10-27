# 遥控快照状态判断（纯计算，已由桥接服务调用）

## 模块位置与职责

`App/rc_status.c/.h` 在 `rc_snapshot` 与未来的寄存器映射之间，统一计算：
是否有帧、传输是否正常、SBUS 状态、帧年龄、是否超时以及 `usable`。
它只读调用者副本，不调用 HAL、RTOS 或快照接口，无动态分配或内部状态，
不需要初始化，也不修改通道、序号、接收时间或共享快照。

2026-10-27 后续已由 Modbus 任务中的 bridge_service 周期调用；默认桥接配置禁用，
阈值未赋值，不能声称正式失联保护已启用。EIDE 已包含该文件，测试 main 不加入固件。
消费者使用方式示意：

```c
RcSnapshot snapshot;
RcStatus status;
if (rc_snapshot_read(&snapshot))
{
    /* 在复制后取当前时间，统一使用 HAL_GetTick() 毫秒时基。
     * approved_timeout_ms 来自获批配置；本项目尚未给它正式赋值。 */
    if (rc_status_evaluate(&snapshot, HAL_GetTick(), approved_timeout_ms, &status))
    {
        /* 使用 status.usable，并把各原因原样映射给 PLC；这里不执行车辆动作。 */
    }
}
```

这只是后续接入示意，不是可直接粘贴的完整运行代码。

## 判断规则及待确认项

- true 返回值仅表示计算成功；必须另看 `status.usable`。
- `usable` 要求：有帧、`transport_ok`、未置 `frame_lost`/`failsafe`、阈值
  已配置且帧年龄小于阈值。任一不满足都不可用；这不是车辆动作授权。
- 将 `frame_lost` 置位帧保守判为不可用是本软件筛选规则，不是 GR01 已测行为；
  不锁存此状态，下一份满足条件的新快照可恢复，PLC 仍负责控制权互锁。
- 超时比较为 `age_ms >= timeout_ms`，边界即超时。阈值允许 1..INT32_MAX ms，
  此范围是时间计算接口限制，不是推荐失联时间。0 或超范围表示配置不可用，
  `timeout_configured=false`、`usable=false`、`timed_out=false`，不代表新鲜。
- 无帧时年龄为 UINT32_MAX；不把上电时间 0 当作最近收到帧。无帧或配置不可用
  都不能仅根据 `timed_out=false` 使用数据。`has_frame` 不依赖 sequence 是否为 0。
- 年龄使用 uint32_t 无符号减法，允许跨计数回绕；真实帧年龄必须小于 2^32 ms
  （约 49.7 天）。长期保留历史快照超过该范围无法区分整圈，应由系统另行管理；
  本纯函数不提供自动历史淘汰；持续运行的 bridge_service 另用 uint64 累积同一帧
  年龄，防止已超时旧快照在完整回绕后重新 usable；仍不能处理时基停顿。
- `received_ms` 仍是 UART 批次的进度观察时间，不是精确硬件帧时间；HAL 毫秒
  时基本身停顿时也不能代表真实经过时间。实测延迟和时基健康仍须验证。
- TODO(待确认)：GR01 输出周期/状态行为、获批失联阈值、实际控件映射和车辆安全值。
  本模块不校验物理通道范围，不改变 Modbus 寄存器草案，不判定 RS-485 通信故障。
- 这是按需计算，不是定时监视器。后续消费者必须定期/每次使用前计算；不能在
  永久等通知的 SBUS 任务里只判断一次，就声称无输入时也会自动报告超时。
  当前 Modbus 任务的等待有 1 tick 上限，无输入时仍计算状态。

## 测试复跑

在 Tests 目录执行；使用 Windows 主机 GCC，不是 arm-none-eabi-gcc。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I ../App ../App/rc_status.c test_rc_status.c -o build/test_rc_status.exe
if ($LASTEXITCODE -ne 0) { throw "Status test compilation failed" }
./build/test_rc_status.exe
if ($LASTEXITCODE -ne 0) { throw "Status test failed" }
```

不定义 NDEBUG。预期输出 PASS、退出码为 0。覆盖 1344 个状态/阈值/年龄组合，
无帧、未配置、阈值前/等于/超过、毫秒计数回绕、传输失效、协议状态恢复、
序号为零、空指针、输入不变及计算结果不锁存。100 ms 等数字均为合成测试值，
不作为设备参数或获批阈值。恢复用例通过构造快照模拟，不等于真实 HAL 重启测试。

验收边界：主机测试和 Arm 独立编译只证明这些输入下的软件行为；任务调用已经接入，
但尚未批准正式配置，未完成实际断线超时、GR01、PLC 联调或车辆失联保护验证。

## 2026-10-27 软件验证记录

- MinGW GCC 严格编译（Wall/Wextra/Werror/pedantic）及上述测试通过。
- Arm GCC Cortex-M4 独立编译（同样严格警告）通过；EIDE 固件编译链接通过，
  sourceList 已包含 rc_status.c，不含测试 main。后续桥接服务已使用本模块。
- decoder、receiver、snapshot 及 UART 模型回归通过，UART 的 start-failure 和
  abort-failure 独立运行也通过。未执行硬件烧录或新的实际链路验证。
