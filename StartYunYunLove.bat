@echo off
chcp 936 >nul
setlocal
title YUN-YUN-LOVE Launcher

echo [YUN-YUN-LOVE] 正在通过 Steam 启动 晕晕电波症候群 ...
start "" "steam://rungameid/2914150"

echo [YUN-YUN-LOVE] 等待游戏进程 (最多 120 秒) ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "for($i=0; $i -lt 60; $i++){ if(Get-Process Yunyun_Syndrome -ErrorAction SilentlyContinue){ exit 0 }; Start-Sleep -Seconds 2 }; exit 1"
if errorlevel 1 (
    echo [x] 没有检测到 Yunyun_Syndrome.exe，请确认游戏已安装并已启动。
    pause
    exit /b 1
)

echo [YUN-YUN-LOVE] 等游戏进入主界面 ...
timeout /t 10 /nobreak >nul

echo [YUN-YUN-LOVE] 注入覆盖层 ...
"%~dp0Injector.exe"
if errorlevel 1 (
    echo [x] 注入失败。若游戏已在运行，可右键本文件"以管理员身份运行"再试。
    pause
    exit /b 1
)

echo [YUN-YUN-LOVE] 完成。游戏内按 INSERT 显示/隐藏界面，按 END 卸载。
timeout /t 6 /nobreak >nul
endlocal