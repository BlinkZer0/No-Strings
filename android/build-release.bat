@echo off
rem ==========================================================================
rem  Release build of the No Strings Android app:
rem    release\NoStrings-<version>.apk   signed APK for installing directly (sideload / adb)
rem    release\NoStrings-<version>.aab   Android App Bundle for the Google Play Console
rem  Both are signed with the upload key in android\keystore (see keystore.properties).
rem  Usage:  build-release.bat
rem ==========================================================================
setlocal
cd /d "%~dp0"

if not exist "%~dp0keystore.properties" (
  echo keystore.properties not found: the release would be signed with the debug key.
  echo Create android\keystore\nostrings-upload.jks with keytool and android\keystore.properties
  echo ^(storeFile, storePassword, keyAlias, keyPassword^). See the README.
  goto :fail
)

rem --- the web build the APK wraps ---
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\vst\webhost\build-web.ps1" || goto :fail

if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not defined JAVA_HOME (
  for /d %%d in ("%USERPROFILE%\.vscode\extensions\redhat.java-*") do (
    for /d %%j in ("%%d\jre\*") do if exist "%%j\bin\javac.exe" set "JAVA_HOME=%%j"
  )
)
if not defined JAVA_HOME (
  echo No JDK found. Install a JDK 17 or newer and set JAVA_HOME.
  goto :fail
)

echo [No Strings] building the release APK and the App Bundle...
call "%~dp0gradlew.bat" --no-daemon -q assembleRelease bundleRelease || goto :fail

for /f "tokens=2 delims== " %%v in ('findstr /c:"versionName" app\build.gradle.kts') do set "VER=%%~v"
set "VER=%VER:"=%"
set "OUT=%~dp0..\release"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "app\build\outputs\apk\release\app-release.apk" "%OUT%\NoStrings-%VER%.apk" >nul || goto :fail
copy /y "app\build\outputs\bundle\release\app-release.aab" "%OUT%\NoStrings-%VER%.aab" >nul || goto :fail

echo.
echo [No Strings] Done.
echo   APK (install directly) : %OUT%\NoStrings-%VER%.apk
echo   AAB (Play Console)     : %OUT%\NoStrings-%VER%.aab
echo   Back up android\keystore\nostrings-upload.jks and keystore.properties: Play updates need this key.
exit /b 0

:fail
echo [No Strings] BUILD FAILED
exit /b 1
