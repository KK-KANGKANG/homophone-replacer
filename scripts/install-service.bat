@echo off
setlocal
net session >nul 2>&1 || (echo Run as Administrator & exit /b 1)
set "ROOT_DIR=%~dp0.."
set "SERVER=%ROOT_DIR%\bin\homophone-replacer-server.exe"
set "CONFIG=%ROOT_DIR%\config\service.json"
sc.exe create HomophoneReplacer binPath= "\"%SERVER%\" --service --config \"%CONFIG%\"" DisplayName= "Homophone Replacer" start= auto
if errorlevel 1 exit /b %ERRORLEVEL%
sc.exe failure HomophoneReplacer reset= 86400 actions= restart/3000/restart/3000/""/0
exit /b %ERRORLEVEL%
