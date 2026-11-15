@echo off
setlocal
python "%~dp0tools\project.py" package %*
exit /b %errorlevel%
