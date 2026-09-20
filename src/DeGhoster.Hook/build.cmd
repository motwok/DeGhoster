@echo off
REM Baut beide Bitness-Varianten der Hook-DLL nach bin\, je in isolierter vcvars-Umgebung.
cd /d "%~dp0"

cmd /c "%~dp0build64.cmd"
if errorlevel 1 (echo x64 build failed & exit /b 1)

cmd /c "%~dp0build32.cmd"
if errorlevel 1 (echo x86 build failed & exit /b 1)

del "%~dp0..\..\build\*.obj" "%~dp0..\..\build\*.exp" "%~dp0..\..\build\*.lib" >nul 2>&1
echo Built: build\DeGhoster.Hook64.dll + build\DeGhoster.Hook32.dll
exit /b 0
