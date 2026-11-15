@echo off
setlocal
python "%~dp0tools\project.py" build %*
exit /b %errorlevel%
