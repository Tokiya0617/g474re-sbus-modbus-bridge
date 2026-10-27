param([string]$Compiler = "gcc")
$ErrorActionPreference = "Stop"
$testDirectory = $PSScriptRoot
$script:TestRuns = 0

function Invoke-Checked {
    param([string]$Program, [string[]]$CommandArguments)
    & $Program @CommandArguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: exit code $LASTEXITCODE" }
}
function Build-And-Run {
    param([string]$Name, [string[]]$Sources, [string[]]$Defines = @(), [string[]]$Modes = @(""))
    $testExecutable = "build/$Name.exe"
    $compileArguments = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2", "-I", ".", "-I", "../App")
    $compileArguments += $Defines
    $compileArguments += $Sources
    $compileArguments += @("-o", $testExecutable)
    Write-Host "Building $Name"
    Invoke-Checked $Compiler $compileArguments
    foreach ($testMode in $Modes) {
        $runArguments = @()
        if ($testMode -ne "") { $runArguments = @($testMode) }
        Invoke-Checked "./$testExecutable" $runArguments
        $script:TestRuns += 1
    }
}

Push-Location $testDirectory
try {
    New-Item -ItemType Directory -Force build | Out-Null
    Build-And-Run "test_sbus_decoder" @("../App/sbus_decoder.c", "test_sbus_decoder.c")
    Build-And-Run "test_sbus_receiver" @("../App/sbus_decoder.c", "../App/sbus_receiver.c", "test_sbus_receiver.c")
    Build-And-Run "test_rc_snapshot" @("../App/rc_snapshot.c", "test_rc_snapshot.c") @("-DRC_SNAPSHOT_HOST_TEST")
    Build-And-Run "test_rc_status" @("../App/rc_status.c", "test_rc_status.c")
    Build-And-Run "test_sbus_uart" @("../App/sbus_uart.c", "../App/uart_callbacks.c", "../App/rc_snapshot.c", "../App/sbus_receiver.c", "../App/sbus_decoder.c", "uart_callbacks_sbus_stubs.c", "test_sbus_uart.c") @("-DSBUS_UART_HOST_TEST", "-DRC_SNAPSHOT_HOST_TEST") @("", "start-failure", "abort-failure")
    Build-And-Run "test_modbus" @("../App/modbus_crc.c", "../App/rc_status.c", "../App/register_map.c", "../App/modbus_slave.c", "../App/modbus_rtu.c", "../App/bridge_config.c", "test_modbus.c")
    Build-And-Run "test_rs485_port" @("../App/rs485_port.c", "../App/uart_callbacks.c", "../App/bridge_config.c", "../App/modbus_rtu.c", "test_rs485_port.c") @("-DRS485_PORT_HOST_TEST") @("", "start-failure", "clock-failure")
    Build-And-Run "test_bridge_service" @("../App/bridge_service.c", "../App/bridge_config.c", "../App/register_map.c", "../App/rc_status.c", "../App/modbus_slave.c", "../App/modbus_rtu.c", "../App/modbus_crc.c", "test_bridge_service.c") @("-DBRIDGE_SERVICE_HOST_TEST") @("", "disabled", "bad-config", "start-failure")
    Write-Host "PASS: 8 test executables, $script:TestRuns runs. Host models only; no hardware validation."
}
finally { Pop-Location }
