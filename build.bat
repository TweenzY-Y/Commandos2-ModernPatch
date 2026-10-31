@echo off
rem Builds build\ddraw.dll (32-bit) with the MSVC toolchain.
rem Requires Visual Studio 2019 or newer with "Desktop development with C++".
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - is Visual Studio installed?
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio installation with C++ tools found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1

if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /W3 /LD src\ddraw_proxy.cpp /Fobuild\ /Febuild\ddraw.dll ^
   /link /DEF:src\ddraw.def /DLL user32.lib kernel32.lib gdi32.lib winmm.lib || exit /b 1
echo BUILD OK
