@echo off
setlocal enabledelayedexpansion
mkdir dist
copy /Y .\temp\main.exe .\dist\ctcmd.exe
curl https://raw.githubusercontent.com/xingguangcuican6666/ctcmd_repo/refs/heads/main/module/module_x86-64_latest_full.zip -o .\temp\temp.zip
"C:\Program Files\ctcmd\module\tools\7z.exe" x .\temp\temp.zip -o.\dist\
del .\temp\temp.zip
cd dist
.\ctcmd.exe "module.winpt(update)"
.\ctcmd.exe "module.winpt(install,wsl)"
.\ctcmd.exe "module.winpt(install,win)"
.\ctcmd.exe "module.winpt(install,std)"
.\ctcmd.exe "module.winpt(install,module)"