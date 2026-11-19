@echo off
rem ==========================================================================
rem  Builds the No Strings VST3 + Standalone (Release).
rem  Usage:  build.bat [Debug|Release]
rem  Build output and the JUCE download go to %LOCALAPPDATA%\NoStringsVST so
rem  OneDrive doesn't try to sync ~1 GB of intermediate files.
rem  Steps: configure -> generate ui/js/params.js from the C++ parameter
rem  table -> build the plugin (the UI is compiled into it).
rem ==========================================================================
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "WORK=%LOCALAPPDATA%\NoStringsVST"
set "BUILD=%WORK%\build"

rem --- find CMake (PATH first, then the copy bundled with Visual Studio) ---
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

rem --- git is needed to fetch JUCE ---
where git >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles%\Git\cmd\git.exe" set "PATH=%ProgramFiles%\Git\cmd;%PATH%"

rem --- reuse E-Piano's JUCE checkout when it is there (same version) ---
set "JUCEARG="
if exist "%LOCALAPPDATA%\EPianoVST\deps\juce-src\CMakeLists.txt" set "JUCEARG=-DNOSTRINGS_JUCE_DIR=%LOCALAPPDATA%\EPianoVST\deps\juce-src"

echo [No Strings] configuring...
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -DFETCHCONTENT_BASE_DIR="%WORK%\deps" %JUCEARG% || goto :fail

echo [No Strings] generating ui\js\params.js...
"%CMAKE%" --build "%BUILD%" --config %CONFIG% --target GenParams --parallel || goto :fail

echo [No Strings] building %CONFIG%...
"%CMAKE%" --build "%BUILD%" --config %CONFIG% --target NoStringsVST_VST3 NoStringsVST_Standalone --parallel || goto :fail

echo.
echo [No Strings] Done.
echo   VST3       : %BUILD%\NoStringsVST_artefacts\%CONFIG%\VST3\No Strings.vst3
echo   Standalone : %BUILD%\NoStringsVST_artefacts\%CONFIG%\Standalone\No Strings.exe
echo   Run install.bat to copy the VST3 into C:\Program Files\Common Files\VST3
exit /b 0

:fail
echo [No Strings] BUILD FAILED
exit /b 1
