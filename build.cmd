@echo off
setlocal
cd /d "%~dp0"

rem Reuse an existing Developer Command Prompt, or locate Visual Studio.
where cl.exe >nul 2>nul
if not errorlevel 1 goto compiler_ready

set "UAS_VCVARS="
for %%V in ("%ProgramFiles%\Microsoft Visual Studio\18\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools") do (
    if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "UAS_VCVARS=%%~V\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined UAS_VCVARS (
    echo Could not find the Microsoft C++ compiler.
    echo Install Visual Studio with Desktop development with C++, then try again.
    exit /b 1
)
call "%UAS_VCVARS%" >nul
if errorlevel 1 exit /b 1

:compiler_ready
if not exist bin mkdir bin
if not exist bin exit /b 1

rem /W4 /WX: enable compiler warnings and treat them as errors.
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /DUNICODE /D_UNICODE keyboard_control.cpp /Fobin\keyboard_control.obj /Febin\uas_keyboard.exe
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX keyboard_logic_tests.cpp /Fobin\keyboard_logic_tests.obj /Febin\keyboard_logic_tests.exe
if errorlevel 1 exit /b 1

bin\keyboard_logic_tests.exe
if errorlevel 1 exit /b 1
echo Build and keyboard-logic checks passed.
exit /b 0
