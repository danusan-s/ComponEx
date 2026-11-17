@echo off
REM Join a game over the internet (STUN + invite codes).
REM Enter the host's invite code, then send YOUR code (printed below) to the host.
title MultiplayerDemo - internet join
set /p CODE=Host's invite code (leave empty to paste it later): 
if "%CODE%"=="" (
  MultiplayerDemo.exe --nat --join
) else (
  MultiplayerDemo.exe --nat --join %CODE%
)
pause
