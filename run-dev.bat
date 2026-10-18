@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%CD%"

where powershell.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Windows PowerShell was not found.
  pause
  exit /b 1
)

rem First launch / source change: build. Otherwise start directly.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\needs-build.ps1" -Root "%ROOT%"
if errorlevel 1 (
  echo [INFO] Debug binaries missing or source changed - compiling...
  call :build_debug
  if errorlevel 1 (
    echo [ERROR] Debug build failed. See the messages above.
    pause
    exit /b 1
  )
) else (
  echo [INFO] No source changes - launching without rebuilding.
)

if not exist "%ROOT%\out\dev\LuckyKissTrainer.exe" (
  echo [ERROR] Debug UI executable missing.
  pause
  exit /b 1
)
start "" "%ROOT%\out\dev\LuckyKissTrainer.exe"
exit /b 0

:build_debug
call :setup_vs
if errorlevel 1 exit /b 1
where dotnet.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] .NET SDK 8 or later is required.
  exit /b 1
)
if not exist "%ROOT%\out\dev" mkdir "%ROOT%\out\dev"
echo [1/2] Compiling native runtime mod [Debug]...
cl /nologo /std:c++17 /utf-8 /Zi /Od /W4 /EHsc /MT /LD /Fo"%ROOT%\out\dev\LuckyKissMod.obj" /Fd"%ROOT%\out\dev\LuckyKissMod-compile.pdb" /DUNICODE /D_UNICODE /I"%ROOT%\src" "%ROOT%\src\LuckyKissMod.cpp" /link /DEBUG /PDB:"%ROOT%\out\dev\LuckyKissMod.pdb" /IMPLIB:"%ROOT%\out\dev\LuckyKissMod.lib" /OUT:"%ROOT%\out\dev\LuckyKissMod.dll" kernel32.lib user32.lib
if errorlevel 1 exit /b 1
echo [2/2] Compiling WPF UI [Debug]...
dotnet build "%ROOT%\ui\LuckyKissAssistant.csproj" -c Debug -o "%ROOT%\out\dev" --nologo
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\scripts\needs-build.ps1" -Root "%ROOT%" -Update
if errorlevel 1 (
  echo [ERROR] Could not record source manifest.
  exit /b 1
)
echo [OK] Debug binaries: %ROOT%\out\dev
exit /b 0

:setup_vs
where cl.exe >nul 2>nul
if not errorlevel 1 exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
  echo [ERROR] Visual Studio C++ x64 tools were not found.
  exit /b 1
)
for /f "usebackq delims=" %%V in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%V"
if not defined VSPATH (
  echo [ERROR] Install Visual Studio C++ Desktop development workload.
  exit /b 1
)
call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
exit /b 0
