@echo off
cd /d "%~dp0"
py -3 tools\serve-game-files.py %*
pause
