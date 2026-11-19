@echo off
rem ==========================================================================
rem  Builds the No Strings Android app (debug APK, self-signed) and installs
rem  it on the connected phone.
rem  Usage:  build-apk.bat [noinstall]
rem  Steps: regenerate ..\web\index.html (the app IS the web build) -> gradle
rem  assembleDebug -> adb install -r.
rem  Needs: the Android SDK (%LOCALAPPDATA%\Android\Sdk or ANDROID_HOME) and a
rem  JDK 17+ (JAVA_HOME, or one found on this machine, see below).
rem ==========================================================================
setlocal
cd /d "%~dp0"

rem --- the web build the APK wraps ---
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\vst\webhost\build-web.ps1" || goto :fail

rem --- SDK ---
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not exist "%ANDROID_HOME%\platform-tools\adb.exe" (
  echo Android SDK not found at %ANDROID_HOME%. Install it with Android Studio or set ANDROID_HOME.
  goto :fail
)

rem --- JDK: JAVA_HOME, else a JDK installed by VS Code's Java extension ---
if not defined JAVA_HOME (
  for /d %%d in ("%USERPROFILE%\.vscode\extensions\redhat.java-*") do (
    for /d %%j in ("%%d\jre\*") do if exist "%%j\bin\javac.exe" set "JAVA_HOME=%%j"
  )
)
if not defined JAVA_HOME (
  echo No JDK found. Install a JDK 17 or newer, e.g. Microsoft Build of OpenJDK, and set JAVA_HOME.
  goto :fail
)
echo [No Strings] JDK: %JAVA_HOME%

echo [No Strings] building the APK...
call "%~dp0gradlew.bat" --no-daemon -q assembleDebug || goto :fail

set "APK=%~dp0app\build\outputs\apk\debug\app-debug.apk"
echo [No Strings] Done: %APK%
if /i "%~1"=="noinstall" exit /b 0

echo [No Strings] installing on the connected phone...
"%ANDROID_HOME%\platform-tools\adb.exe" install -r "%APK%" || goto :fail
"%ANDROID_HOME%\platform-tools\adb.exe" shell am start -n com.blinkzero.nostrings/.MainActivity >nul
echo [No Strings] Installed and started. Plug in a USB or Bluetooth MIDI keyboard and play.
exit /b 0

:fail
echo [No Strings] BUILD FAILED
exit /b 1
