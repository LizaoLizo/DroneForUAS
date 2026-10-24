@echo off
setlocal
cd /d "%~dp0"
call build.cmd
if errorlevel 1 (
    echo Build failed. The program was not started.
    pause
    exit /b 1
)
echo Click the UAS Keyboard Commands window, then hold W/S, A/D or Q/E.
bin\uas_keyboard.exe
if errorlevel 1 (
    echo The keyboard window could not run normally.
    pause
    exit /b 1
)
exit /b 0
