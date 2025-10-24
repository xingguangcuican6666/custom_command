@echo off
setlocal enabledelayedexpansion
if "%1" == "clean" goto clean
call .\build.bat
mkdir dist
copy /Y .\temp\main.exe .\dist\ctcmd.exe
curl https://raw.githubusercontent.com/xingguangcuican6666/ctcmd_repo/refs/heads/main/module/module_x86-64_latest.zip -o .\temp\temp.zip
if not exist "C:\Program Files\ctcmd\module\tools\7z.exe" (
    curl https://hk.gh-proxy.com/https://github.com/ip7z/7zip/releases/download/25.01/7z2501-x64.msi -o .\temp\7z.msi
    msiexec /i 7z.msi /passive
)
"C:\Program Files\7-Zip\7z.exe" x .\temp\temp.zip -o.\dist\
del .\temp\temp.zip
cd dist
mkdir .\module\cache
mkdir .\module\temp
curl https://raw.githubusercontent.com/xingguangcuican6666/ctcmd_repo/refs/heads/main/source.ctcmd -o .\module\source.ctcmd
timeout /t 1 >nul
.\ctcmd.exe "module.winpt(update)"
.\ctcmd.exe "module.winpt(install,wsl)"
.\ctcmd.exe "module.winpt(install,win)"
.\ctcmd.exe "module.winpt(install,std)"
.\ctcmd.exe "module.winpt(install,module)"
@REM .\ctcmd.exe "module.winpt(install,7zip)"
@REM .\ctcmd.exe "module.winpt(install,vcpkg)"
:clean
rd /s /q dist
