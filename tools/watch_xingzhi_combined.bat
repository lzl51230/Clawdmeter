@echo off
setlocal

cd /d "%~dp0.."

set "VOICE_OUTPUT=%TEMP%\xingzhi-voice.wav"
if not "%XINGZHI_VOICE_OUTPUT%"=="" set "VOICE_OUTPUT=%XINGZHI_VOICE_OUTPUT%"

py -3 -u tools\windows_xingzhi_combined_watch.py ^
  --usage-source codex-wsl ^
  --require-ack ^
  --poll-interval 60 ^
  --retry-delay 5 ^
  --receive-timeout 0 ^
  --voice-output "%VOICE_OUTPUT%" ^
  %*

exit /b %ERRORLEVEL%
