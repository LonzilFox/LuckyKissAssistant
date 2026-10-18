@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%CD%"
call :vs || (pause & exit /b 1)
where dotnet.exe >nul 2>nul || (echo [ERROR] Install .NET SDK 8 or later. & pause & exit /b 1)
if not exist "out\release" mkdir "out\release"
echo [1/2] Compiling native runtime mod [Release]...
cl /nologo /std:c++17 /utf-8 /O2 /W4 /EHsc /MT /LD /Fo"%ROOT%\out\release\LuckyKissMod.obj" /DUNICODE /D_UNICODE /I"%ROOT%\src" "%ROOT%\src\LuckyKissMod.cpp" /link /IMPLIB:"%ROOT%\out\release\LuckyKissMod.lib" /OUT:"%ROOT%\out\release\LuckyKissMod.dll" kernel32.lib user32.lib
if errorlevel 1 (echo [ERROR] Native build failed & pause & exit /b 1)
echo [2/2] Publishing WPF UI [Release]...
dotnet publish "%ROOT%\ui\LuckyKissAssistant.csproj" -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -o "%ROOT%\out\release" --nologo
if errorlevel 1 (echo [ERROR] UI publish failed & pause & exit /b 1)
echo [OK] out\release\LuckyKissTrainer.exe + LuckyKissMod.dll
pause
exit /b 0
:vs
where cl.exe >nul 2>nul && exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (echo [ERROR] C++ toolchain not found. & exit /b 1)
for /f "usebackq delims=" %%V in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%V"
if not defined VSPATH (echo [ERROR] C++ x64 compiler missing. & exit /b 1)
call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
exit /b 0
