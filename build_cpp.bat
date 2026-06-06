@echo off
chcp 65001 >nul
title Build Clipboard Typer (C++)

:: 启用 MSYS2 环境
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

echo Compiling clipboard_typer.cpp ...
echo.

g++ clipboard_typer.cpp -luser32 -o clipboard_typer.exe -O2 -s -mwindows -std=c++17

if %errorlevel% equ 0 (
    echo.
    echo [OK] clipboard_typer.exe 编译成功
    echo.
    echo 使用说明：
    echo   clipboard_typer.exe        启动（无窗口后台运行）
    echo.
    echo 快捷键：
    echo   Ctrl + Alt + P  = 读取剪贴板并打字
    echo   Ctrl + Alt + Q  = 退出程序
) else (
    echo.
    echo [FAIL] 编译失败，错误码 %errorlevel%
)

pause
