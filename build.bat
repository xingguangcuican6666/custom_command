@echo off
REM 编译为控制台终端应用，所有中间产物输出到 temp 文件夹
if not exist temp mkdir temp
cl.exe /Zi /EHsc /nologo /std:c++17 main.cpp /Fo:temp\\ /Fd:temp\\ /Fe:temp\\main.exe /link /SUBSYSTEM:CONSOLE /OUT:temp\\main.exe user32.lib shell32.lib
pause