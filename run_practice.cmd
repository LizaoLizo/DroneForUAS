@echo off
setlocal
cd /d "%~dp0"

rem Compile the learner's actual saved file; do not change its contents.
set "UAS_PRACTICE_SOURCE=keyboardpractice.cpp"
if not exist "%UAS_PRACTICE_SOURCE%" (
    echo Could not find %UAS_PRACTICE_SOURCE% in this folder.
    goto failed
)

where cl.exe >nul 2>nul
if not errorlevel 1 goto compiler_ready

set "UAS_VCVARS="
for %%V in ("%ProgramFiles%\Microsoft Visual Studio\18\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools") do (
    if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "UAS_VCVARS=%%~V\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined UAS_VCVARS (
    echo Could not find the Microsoft C++ compiler.
    echo Use Visual Studio with Desktop development with C++ installed.
    goto failed
)
call "%UAS_VCVARS%" >nul
if errorlevel 1 goto failed

:compiler_ready
if not exist bin mkdir bin
if not exist bin goto failed
echo Compiling: %CD%\%UAS_PRACTICE_SOURCE%
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX "%UAS_PRACTICE_SOURCE%" /Fobin\keyboardpractice.obj /Febin\keyboardpractice.exe
if errorlevel 1 goto failed
if /i "%~1"=="--build-only" exit /b 0

echo.
echo Running your practice program. Follow the instructions printed below.
bin\keyboardpractice.exe
if errorlevel 1 goto failed
echo.
pause
exit /b 0

:failed
echo.
echo Could not build or run the practice program. Read the error above.
if /i not "%~1"=="--build-only" pause
exit /b 1
