@echo off
setlocal
cd /d "%~dp0"
py -3 -u tools\windows_codex_usage_serial.py --watch --port COM3 --baud 115200 --poll-interval 60 --retry-delay 5 %*
exit /b %ERRORLEVEL%
