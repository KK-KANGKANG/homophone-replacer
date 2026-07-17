@echo off
sc.exe stop HomophoneReplacer >nul 2>&1
sc.exe delete HomophoneReplacer
exit /b %ERRORLEVEL%
