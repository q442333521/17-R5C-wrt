@echo off
REM Gateway Build and Start Script for Windows + WSL
REM Windows 下通过 WSL 编译和运行网关项目

echo =========================================
echo Gateway Project - Windows + WSL
echo =========================================
echo.

REM 检查 WSL 是否安装
wsl --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: WSL is not installed
    echo Please install WSL first: https://aka.ms/wsl2
    pause
    exit /b 1
)

echo WSL detected
echo.

REM 转换 Windows 路径到 WSL 路径
set PROJECT_PATH=%~dp0..
for /f "tokens=*" %%i in ('wsl wslpath -u "%PROJECT_PATH%"') do set WSL_PATH=%%i

echo Project path: %PROJECT_PATH%
echo WSL path: %WSL_PATH%
echo.

REM 选择操作
echo Choose an action:
echo   1. Install dependencies
echo   2. Build project
echo   3. Start services
echo   4. Stop services
echo   5. View logs
echo   6. Test Modbus connection
echo   0. Exit
echo.

set /p choice="Enter your choice (0-6): "

if "%choice%"=="1" goto install
if "%choice%"=="2" goto build
if "%choice%"=="3" goto start
if "%choice%"=="4" goto stop
if "%choice%"=="5" goto logs
if "%choice%"=="6" goto test
if "%choice%"=="0" goto end

echo Invalid choice
pause
exit /b 1

:install
echo.
echo Installing dependencies...
wsl bash -c "cd %WSL_PATH% && sudo apt update && sudo apt install -y build-essential cmake pkg-config libmodbus-dev libjsoncpp-dev"
echo.
echo Dependencies installed
pause
goto end

:build
echo.
echo Building project...
wsl bash -c "cd %WSL_PATH% && chmod +x scripts/*.sh && ./scripts/build.sh"
echo.
echo Build completed
pause
goto end

:start
echo.
echo Starting services...
wsl bash -c "cd %WSL_PATH% && ./scripts/start.sh"
echo.
echo Services started
echo Web interface: http://localhost:8080
echo.
pause
goto end

:stop
echo.
echo Stopping services...
wsl bash -c "cd %WSL_PATH% && ./scripts/stop.sh"
echo.
echo Services stopped
pause
goto end

:logs
echo.
echo Viewing logs...
echo Press Ctrl+C to stop
wsl bash -c "tail -f /tmp/gw-test/logs/*.log"
goto end

:test
echo.
echo Testing Modbus connection...
echo Press Ctrl+C to stop
wsl python3 "%WSL_PATH%/tests/test_modbus_client.py"
goto end

:end
echo.
echo Done
