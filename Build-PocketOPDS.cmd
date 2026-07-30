@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
set "BUILD_RESULT=%ERRORLEVEL%"
if not "%BUILD_RESULT%"=="0" (
  echo.
  echo PocketOPDS build failed with error code %BUILD_RESULT%.
  echo The window will remain open so the error can be read.
  echo.
  pause
  exit /b %BUILD_RESULT%
)
echo.
echo PocketOPDS build completed successfully.
pause
exit /b %BUILD_RESULT%
