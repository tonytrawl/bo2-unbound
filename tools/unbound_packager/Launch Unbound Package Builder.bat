@echo off
setlocal
cd /d "%~dp0"
if exist "dist\Unbound Package Builder.exe" (
  start "" "dist\Unbound Package Builder.exe"
  exit /b 0
)
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 packager_gui.py
) else (
  python packager_gui.py
)
if errorlevel 1 pause
