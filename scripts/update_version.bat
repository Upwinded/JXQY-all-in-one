@echo off
setlocal enabledelayedexpansion

:: �������
if "%~1"=="" (
    echo ������ָ���°汾�ţ����磺update_version.bat 1.4.1��
    exit /b 1
)
set "new_version=%~1"

:: ����������飨�谲װGit for Windows��
where sed >nul 2>&1
if %errorlevel% neq 0 (
    echo ��Ҫ��װ Git for Windows ���ṩ sed ����
    echo ���ص�ַ��https://git-scm.com/download/win
    exit /b 1
)

:: �ݹ鴦������ƽ̨�ļ�
for /r %%f in (.) do (
    pushd "%%f"
    call :process_files
    popd
)
echo ����ƽ̨�汾���Ѹ���Ϊ %new_version% �������ֶ���飡
exit /b 0

:process_files
:: 1. Windows .rc �ļ�����
set "win_file=../win/jxqy-all-in-one/jxqy-all-in-one.rc"
set "win_new_version=%new_version:.=,%"
sed -i "s/""FileVersion"", ""[0-9.]\+""/""FileVersion"", ""%new_version%.0""/g" "%win_file%"
sed -i "s/""ProductVersion"", ""[0-9.]\+""/""ProductVersion"", ""%new_version%.0""/g" "%win_file%"
sed -i "s/FILEVERSION [0-9,]\+/FILEVERSION %win_new_version%,0/g" "%win_file%"
sed -i "s/PRODUCTVERSION [0-9,]\+/PRODUCTVERSION %win_new_version%,0/g" "%win_file%"

:: 2. Xcode .pbxproj �ļ�����
set "xcode_file=../macos_ios/jxqy/jxqy.xcodeproj/project.pbxproj"
sed -i "s/MARKETING_VERSION = [0-9.]\+/MARKETING_VERSION = %new_version%/g" "%xcode_file%"

:: 3. Android build.gradle ����
set "android_file=../android/app/build.gradle"
sed -i "s/appVersion = ""[0-9.]\+""/appVersion = ""%new_version%""/g" "%android_file%"

:: 4. CMakeLists.txt ����
set "linux_file=../linux/CMakeLists.txt"
sed -i "s/PROJECT_VERSION [0-9.]\+/PROJECT_VERSION %new_version%/g" "%linux_file%"

exit /b