@echo off
setlocal

cd /d "%~dp0.."

set "VOICE_OUTPUT=%TEMP%\xingzhi-voice.wav"
if not "%XINGZHI_VOICE_OUTPUT%"=="" set "VOICE_OUTPUT=%XINGZHI_VOICE_OUTPUT%"

py -3 -u tools\windows_xingzhi_voice_dictation.py ^
  --watch ^
  --receive-timeout 0 ^
  --output "%VOICE_OUTPUT%" ^
  %*

exit /b %ERRORLEVEL%
