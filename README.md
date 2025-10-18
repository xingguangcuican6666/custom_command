# Ctcmd

Ctcmd (全名 Custom Command) 是一个 Windows 下的自定义终端程序，支持类似 Java 风格的“对象.方法(参数)”语法调用批处理文件，并兼容常用命令和变量替换。并提供社区支持框架，该框架类似Ubuntu/Debian 的 apt，允许用户使用内置工具Winpt进行软件包下载和安装。

使用文档：https://docs.oraclestar.cn/ctcmd/

## 功能特性

- **批处理函数调用**  
  输入如 `power.shutdown(1)`，自动查找并执行 `power/shutdown.bat 1`，实现类似函数调用体验。

- **常用命令支持**  
  - `cd [目录]`         切换目录
  - `set KEY=VALUE`     设置环境变量
  - `env [KEY]`         查看环境变量
  - `print 内容`        输出内容（支持变量替换）
  - `ls [目录]`         列出目录内容
  - `ps`                查看进程列表
  - `pkill 进程名`      按名杀进程
  - `exit`              退出终端
  - `help`              查看命令帮助

- **变量替换**  
  支持 `%VAR%` 或 `${VAR}` 形式的环境变量替换。

- **path.txt 配置**  
  程序首次运行自动生成 path.txt，指定批处理文件搜索目录（UTF-8 编码）。

## 使用方法

1. 编译  
   推荐使用 Visual Studio 或 g++，需链接 Windows API。

2. 运行  
   ```
   ctcmd.exe
   ```
   或直接命令行调用：
   ```
   ctcmd.exe power.shutdown(1)
   ctcmd.exe help
   ```

3. 批处理文件结构  
   例如 `power/shutdown.bat`，可根据需要自定义参数处理。

## 启动界面示例

```
Custom CMD [版本 1.1.0]
(c) OracleLoadStar。保留所有权利。
ctcmd - 自定义终端，输入 help 获取命令帮助。
C:\Users\xxx>
```

## 版权

MIT License
