@echo off
REM Host a game over the internet (STUN + invite codes).
REM 1. Wait for "Your invite code: ..." to appear below and send it to your friend.
REM 2. When they send theirs back, paste it into THIS window and press Enter.
title MultiplayerDemo - internet host
MultiplayerDemo.exe --nat
pause
