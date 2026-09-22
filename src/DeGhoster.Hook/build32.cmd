@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)
cd /d "%~dp0"
set "OUT=%~dp0..\..\build"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /LD /MT /EHsc /O2 /W3 /DUNICODE /D_UNICODE Hook.cpp /Fo:"%OUT%\\" /Fe:"%OUT%\DeGhoster.Hook32.dll" /link /DEF:exports.def user32.lib dwmapi.lib
if errorlevel 1 exit /b %errorlevel%
cl /nologo /MT /EHsc /O2 /W3 /DUNICODE /D_UNICODE "%~dp0..\DeGhoster.Helper32\Helper.cpp" /Fo:"%OUT%\\" /Fe:"%OUT%\DeGhoster.Helper32.exe" /link user32.lib shell32.lib /SUBSYSTEM:WINDOWS
exit /b %errorlevel%
