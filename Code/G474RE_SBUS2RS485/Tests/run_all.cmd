@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 exit /b 1
if not exist build mkdir build
if errorlevel 1 exit /b 1
set "BRIDGE_TEST_CC=gcc"
if not "%~1"=="" set "BRIDGE_TEST_CC=%~1"
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App ../App/sbus_decoder.c test_sbus_decoder.c -o build/test_sbus_decoder.exe
if errorlevel 1 exit /b 1
build\test_sbus_decoder.exe
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App ../App/sbus_decoder.c ../App/sbus_receiver.c test_sbus_receiver.c -o build/test_sbus_receiver.exe
if errorlevel 1 exit /b 1
build\test_sbus_receiver.exe
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App -DRC_SNAPSHOT_HOST_TEST ../App/rc_snapshot.c test_rc_snapshot.c -o build/test_rc_snapshot.exe
if errorlevel 1 exit /b 1
build\test_rc_snapshot.exe
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App ../App/rc_status.c test_rc_status.c -o build/test_rc_status.exe
if errorlevel 1 exit /b 1
build\test_rc_status.exe
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App -DSBUS_UART_HOST_TEST -DRC_SNAPSHOT_HOST_TEST ../App/sbus_uart.c ../App/uart_callbacks.c ../App/rc_snapshot.c ../App/sbus_receiver.c ../App/sbus_decoder.c uart_callbacks_sbus_stubs.c test_sbus_uart.c -o build/test_sbus_uart.exe
if errorlevel 1 exit /b 1
build\test_sbus_uart.exe
if errorlevel 1 exit /b 1
build\test_sbus_uart.exe start-failure
if errorlevel 1 exit /b 1
build\test_sbus_uart.exe abort-failure
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App ../App/modbus_crc.c ../App/rc_status.c ../App/register_map.c ../App/modbus_slave.c ../App/modbus_rtu.c ../App/bridge_config.c test_modbus.c -o build/test_modbus.exe
if errorlevel 1 exit /b 1
build\test_modbus.exe
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App -DRS485_PORT_HOST_TEST ../App/rs485_port.c ../App/uart_callbacks.c ../App/bridge_config.c ../App/modbus_rtu.c test_rs485_port.c -o build/test_rs485_port.exe
if errorlevel 1 exit /b 1
build\test_rs485_port.exe
if errorlevel 1 exit /b 1
build\test_rs485_port.exe start-failure
if errorlevel 1 exit /b 1
build\test_rs485_port.exe clock-failure
if errorlevel 1 exit /b 1
"%BRIDGE_TEST_CC%" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -I . -I ../App -DBRIDGE_SERVICE_HOST_TEST ../App/bridge_service.c ../App/bridge_config.c ../App/register_map.c ../App/rc_status.c ../App/modbus_slave.c ../App/modbus_rtu.c ../App/modbus_crc.c test_bridge_service.c -o build/test_bridge_service.exe
if errorlevel 1 exit /b 1
build\test_bridge_service.exe
if errorlevel 1 exit /b 1
build\test_bridge_service.exe disabled
if errorlevel 1 exit /b 1
build\test_bridge_service.exe bad-config
if errorlevel 1 exit /b 1
build\test_bridge_service.exe start-failure
if errorlevel 1 exit /b 1
echo PASS: 8 test executables, 15 runs. Host models only.
exit /b 0
