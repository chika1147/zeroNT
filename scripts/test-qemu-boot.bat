@echo off
setlocal
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    py -3 "%~dp0test-qemu-boot.py" %*
) else (
    python "%~dp0test-qemu-boot.py" %*
)
exit /b %ERRORLEVEL%
