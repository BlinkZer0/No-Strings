@echo off
rem ==========================================================================
rem  Builds the console renderer (DSP core only, no JUCE) and renders the test
rem  WAVs into ..\renders, then prints the measurement report.
rem  Usage:  render.bat [extra NoStringsRender args, e.g. --only pressure]
rem ==========================================================================
setlocal
set "WORK=%LOCALAPPDATA%\NoStringsVST"
set "BUILD=%WORK%\build"

set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

rem --- same configure as build.bat (one build folder), reusing E-Piano's JUCE when present ---
set "JUCEARG="
if exist "%LOCALAPPDATA%\EPianoVST\deps\juce-src\CMakeLists.txt" set "JUCEARG=-DNOSTRINGS_JUCE_DIR=%LOCALAPPDATA%\EPianoVST\deps\juce-src"

echo [No Strings] configuring...
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -DFETCHCONTENT_BASE_DIR="%WORK%\deps" %JUCEARG% >nul || goto :fail
echo [No Strings] building NoStringsRender...
"%CMAKE%" --build "%BUILD%" --config Release --target NoStringsRender --parallel || goto :fail

"%BUILD%\Release\NoStringsRender.exe" --out "%~dp0..\renders" %*
exit /b %errorlevel%

:fail
echo [No Strings] BUILD FAILED
exit /b 1
