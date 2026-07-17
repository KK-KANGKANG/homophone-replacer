@echo off
rem Windows构建脚本 for homophone-replacer-standalone
rem 需要Visual Studio 2019或更高版本

echo ====================================================
echo Building Homophone Replacer Standalone for Windows
echo ====================================================

rem 检查是否在VS开发者命令提示符中
if "%VCINSTALLDIR%"=="" (
    echo Error: Please run this script from Visual Studio Developer Command Prompt
    echo 请在Visual Studio开发者命令提示符中运行此脚本
    pause
    exit /b 1
)

rem 创建构建目录
if not exist build\local mkdir build\local
cd build\local

rem 运行CMake配置
echo Running CMake configuration...
cmake ../.. -A x64 -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    echo CMake配置失败！
    pause
    exit /b 1
)

rem 编译项目
echo Building project...
cmake --build . --config Release --parallel
if %errorlevel% neq 0 (
    echo Build failed!
    echo 编译失败！
    pause
    exit /b 1
)

echo.
echo ====================================================
echo Build completed successfully!
echo 编译成功完成！
echo.
echo Executable location: build\local\bin\Release\homophone-replacer-standalone.exe
echo 可执行文件位置: build\local\bin\Release\homophone-replacer-standalone.exe
echo.
echo Usage example:
echo 使用示例:
echo   cd build\local\bin\Release
echo   homophone-replacer-standalone.exe --text "他想知道这个问题的答案"
echo ====================================================

pause
