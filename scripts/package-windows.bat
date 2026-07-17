@echo off
setlocal
set "ROOT_DIR=%~dp0.."
call "%ROOT_DIR%\build.bat" || exit /b %ERRORLEVEL%
set "STAGE=%ROOT_DIR%\dist\stage-windows\homophone-replacer-windows-x64"
if exist "%ROOT_DIR%\dist\stage-windows" rmdir /s /q "%ROOT_DIR%\dist\stage-windows"
cmake --install "%ROOT_DIR%\build" --config Release --prefix "%STAGE%" || exit /b %ERRORLEVEL%
powershell -NoProfile -Command "Compress-Archive -Force '%STAGE%\*' '%ROOT_DIR%\dist\homophone-replacer-windows-x64.zip'"
exit /b %ERRORLEVEL%
