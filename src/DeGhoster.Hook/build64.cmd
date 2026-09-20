@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set "OUT=%~dp0..\..\build"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /LD /EHsc /O2 /W3 /DUNICODE /D_UNICODE Hook.cpp /Fo:"%OUT%\\" /Fe:"%OUT%\DeGhoster.Hook64.dll" /link user32.lib dwmapi.lib
exit /b %errorlevel%
