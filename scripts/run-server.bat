@echo off
setlocal
set "ROOT_DIR=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=config\service.json"
set "SERVER=%ROOT_DIR%\bin\homophone-replacer-server.exe"
if not exist "%SERVER%" set "SERVER=%ROOT_DIR%\build\bin\Release\homophone-replacer-server.exe"
if not exist "%SERVER%" (
  echo Server executable not found: %SERVER%
  exit /b 1
)
pushd "%ROOT_DIR%"
if not exist "%CONFIG%" (
  echo Config file not found: %CONFIG%
  popd
  exit /b 1
)
"%SERVER%" --config "%CONFIG%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
