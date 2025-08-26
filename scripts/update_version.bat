@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

:: 参数检查
if "%~1"=="" (
    echo 错误：请指定新版本号（例如：update_version.bat 1.4.1）
    exit /b 1
)
set "new_version=%~1"

:: 工具依赖检查（需安装Git for Windows）
where sed >nul 2>&1
if %errorlevel% neq 0 (
    where git >nul 2>&1
    if %errorlevel% neq 0 (
        echo 需要安装 Git for Windows 以提供 sed 工具
        echo 下载地址：https://git-scm.com/download/win
    )
    echo 请使用git bash运行此脚本
    exit /b 1
)

:: 递归处理所有平台文件
for /r %%f in (.) do (
    pushd "%%f"
    call :process_files
    popd
)
echo 所有平台版本号已更新为 %new_version% ，但请手动检查！
exit /b 0

:process_files
:: 1. Windows .rc 文件处理
set "win_file=../win/jxqy-all-in-one/jxqy-all-in-one.rc"
set "win_new_version=%new_version:.=,%"
sed -i "s/""FileVersion"", ""[0-9.]\+""/""FileVersion"", ""%new_version%.0""/g" "%win_file%"
sed -i "s/""ProductVersion"", ""[0-9.]\+""/""ProductVersion"", ""%new_version%.0""/g" "%win_file%"
sed -i "s/FILEVERSION [0-9,]\+/FILEVERSION %win_new_version%,0/g" "%win_file%"
sed -i "s/PRODUCTVERSION [0-9,]\+/PRODUCTVERSION %win_new_version%,0/g" "%win_file%"

:: 2. Xcode .pbxproj 文件处理
set "xcode_file=../macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
sed -i "s/MARKETING_VERSION = [0-9.]\+/MARKETING_VERSION = %new_version%/g" "%xcode_file%"

:: 3. Android build.gradle 处理
set "android_file=../android/app/build.gradle"
sed -i "s/appVersion = ""[0-9.]\+""/appVersion = ""%new_version%""/g" "%android_file%"

:: 4. CMakeLists.txt 处理
set "linux_file=../linux/CMakeLists.txt"
sed -i "s/PROJECT_VERSION [0-9.]\+/PROJECT_VERSION %new_version%/g" "%linux_file%"

exit /b