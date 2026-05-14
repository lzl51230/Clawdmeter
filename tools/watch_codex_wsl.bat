@echo off
setlocal

cd /d "%~dp0.."

py -3 -u tools\windows_claude_usage_ble.py ^
  --usage-source codex-wsl ^
  --watch ^
  --require-ack ^
  --poll-interval 60 ^
  --retry-delay 5 ^
  %*

exit /b %ERRORLEVEL%
