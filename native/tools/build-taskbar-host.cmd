@echo off
call "%~1\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /DUNICODE /D_UNICODE "%~3" /Fo"%~2\taskbar_host.obj" /Fe"%~2\WinCalTaskbarHost.exe" /link /SUBSYSTEM:WINDOWS shell32.lib
exit /b %errorlevel%
