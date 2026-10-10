@echo off
rem start_heartbeat.cmd - open a visible cmd window running the 5-minute heartbeat.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0heartbeat.ps1"
pause
