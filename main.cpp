// ctcmd - 全功能终端，支持命令/对象/方法/路径补全、批处理调用、变量替换、help 动态对象方法、DEBUG信息，编码建议GBK
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <cstdlib>
#include <cstdio>
#include <conio.h>
#include <algorithm>
// #define DEBUG

const std::vector<std::string> builtin_cmds = {
    "cd", "set", "env", "print", "ls", "ps", "pkill", "help", "exit"
};

#ifdef DEBUG
#define DBG(x) do { std::cout << "[DEBUG] " << x << std::endl; } while(0)
#else
#define DBG(x)
#endif

// 获取当前目录下所有文件和文件夹名（目录名以/结尾）
std::vector<std::string> listAllFiles(const std::string& dir, bool onlyDir = false) {
    std::vector<std::string> files;
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        DBG("FindFirstFileA failed: " << dir);
        return files;
    }
    do {
        if (strcmp(ffd.cFileName, ".") && strcmp(ffd.cFileName, "..")) {
            if (onlyDir && !(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::string name = ffd.cFileName;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                name += "/";
            files.push_back(name);
        }
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
    DBG("listAllFiles(" << dir << ") count=" << files.size());
    return files;
}

// 读取 path.txt，获取批处理文件搜索目录（UTF-8+BOM），优先在程序本体目录查找
std::vector<std::string> getSearchPaths() {
    std::vector<std::string> paths;
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t pos = exeDir.find_last_of("\\/");
    if (pos != std::string::npos) exeDir = exeDir.substr(0, pos);
    std::string rcfile = exeDir + "\\path.txt";
    DBG("open path.txt: " << rcfile);
    FILE* fp = fopen(rcfile.c_str(), "rb");
    if (!fp) {
        DBG("path.txt not found, create one");
        FILE* wf = fopen(rcfile.c_str(), "wb");
        if (wf) {
            unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            fwrite(bom, 1, 3, wf);
            fwrite(exeDir.c_str(), 1, exeDir.size(), wf);
            fwrite("\n", 1, 1, wf);
            fclose(wf);
        }
        fp = fopen(rcfile.c_str(), "rb");
        if (!fp) {
            DBG("still fail, fallback to exeDir");
            paths.push_back(exeDir);
            return paths;
        }
    }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    size_t offset = 0;
    if (n >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        offset = 3;
        DBG("BOM detected");
    }
    std::string content(buf + offset, n - offset);
    size_t p = 0;
    while (p < content.size()) {
        size_t end = content.find('\n', p);
        std::string line = (end == std::string::npos) ? content.substr(p) : content.substr(p, end - p);
        while (!line.empty() && ((unsigned char)line[0] == 0xEF || (unsigned char)line[0] == 0xBB || (unsigned char)line[0] == 0xBF))
            line.erase(0, 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        while (!line.empty() && (line.front() == ' ')) line.erase(0, 1);
        if (!line.empty()) {
            DBG("path: " << line);
            paths.push_back(line);
        }
        if (end == std::string::npos) break;
        p = end + 1;
    }
    fclose(fp);
    DBG("getSearchPaths count=" << paths.size());
    return paths;
}

// TAB 补全主逻辑（命令、对象.方法、路径，支持cd/ls参数补全，Tab轮换）
struct TabState {
    std::string last_line;
    std::vector<std::string> candidates;
    size_t idx = 0;
};
TabState g_tabState;

std::string tabCompleteRotate(const std::string& line) {
    if (line.empty()) return line;
    size_t pos = line.find_last_of(" \t");
    std::string prefix = (pos == std::string::npos) ? line : line.substr(pos + 1);
    std::vector<std::string> candidates;

    // cd/ls 路径补全
    if (line.substr(0, 3) == "cd " || line.substr(0, 3) == "ls ") {
        std::string arg = line.substr(3);
        std::string dir = ".";
        size_t slash = arg.find_last_of("/\\");
        std::string base = arg;
        if (slash != std::string::npos) {
            dir = arg.substr(0, slash + 1);
            base = arg.substr(slash + 1);
        }
        std::vector<std::string> files = listAllFiles(dir);
        for (const auto& f : files) {
            if (f.find(base) == 0) {
                std::string full = dir == "." ? f : dir + f;
                if (full.substr(0, 2) == "./") full = full.substr(2);
                candidates.push_back(full);
            }
        }
    } else if (line.find('.') != std::string::npos && line.find(' ') == std::string::npos) {
        // 对象.补全方法
        size_t dot = line.find('.');
        std::string obj = line.substr(0, dot);
        std::string methodPrefix = line.substr(dot + 1);
        std::vector<std::string> searchPaths = getSearchPaths();
        for (const auto& dir : searchPaths) {
            std::string objDir = dir + "\\" + obj;
            std::vector<std::string> methods = listAllFiles(objDir);
            for (const auto& m : methods) {
                std::string name = m;
                if (name.size() > 4 && name.substr(name.size() - 4) == ".bat")
                    name = name.substr(0, name.size() - 4);
                if (!name.empty() && name.back() == '/')
                    name.pop_back();
                if (name.find(methodPrefix) == 0)
                    candidates.push_back(obj + "." + name);
            }
        }
    } else {
        // 命令补全
        for (const auto& cmd : builtin_cmds)
            if (cmd.find(prefix) == 0) candidates.push_back(cmd);
        // 路径补全
        std::vector<std::string> files = listAllFiles(".");
        for (const auto& f : files)
            if (f.find(prefix) == 0) candidates.push_back(f);
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    if (line != g_tabState.last_line || candidates != g_tabState.candidates) {
        g_tabState.last_line = line;
        g_tabState.candidates = candidates;
        g_tabState.idx = 0;
    } else {
        if (!candidates.empty())
            g_tabState.idx = (g_tabState.idx + 1) % candidates.size();
    }
    if (!candidates.empty()) {
        size_t clear_len = prefix.size();
        for (size_t i = 0; i < clear_len; ++i) std::cout << "\b \b";
        std::cout << candidates[g_tabState.idx];
        std::string completed = (pos == std::string::npos ? "" : line.substr(0, pos + 1)) + candidates[g_tabState.idx];
        DBG("tab complete: " << completed);
        return completed;
    }
    return line;
}

// 命令历史
#include <deque>
#include <fstream>
std::deque<std::string> g_history;
int g_historyIdx = -1;

// 读取历史记录
void loadHistory() {
    char* home = getenv("USERPROFILE");
    std::string histFile = home ? std::string(home) + "\\.ctcmd" : ".ctcmd";
    std::ifstream fin(histFile, std::ios::in);
    if (fin) {
        std::string line;
        while (std::getline(fin, line)) {
            if (!line.empty()) g_history.push_back(line);
        }
        fin.close();
    }
}

// 保存历史记录
void saveHistory() {
    char* home = getenv("USERPROFILE");
    std::string histFile = home ? std::string(home) + "\\.ctcmd" : ".ctcmd";
    std::ofstream fout(histFile, std::ios::out | std::ios::trunc);
    if (fout) {
        for (const auto& cmd : g_history) fout << cmd << std::endl;
        fout.close();
    }
}

// 读取一行，支持 TAB 补全、方向键历史、左右移动与行内编辑
std::string getlineWithTab() {
    std::string line;
    int ch;
    int cursor = 0;
    g_tabState = TabState();
    g_historyIdx = -1;
    while ((ch = _getch()) != '\r') {
        if (ch == '\b') {
            if (cursor > 0) {
                line.erase(cursor - 1, 1);
                cursor--;
                std::cout << "\b";
                for (size_t i = cursor; i < line.size(); ++i) std::cout << line[i];
                std::cout << " ";
                for (size_t i = cursor; i <= line.size(); ++i) std::cout << "\b";
                g_tabState = TabState();
            }
        } else if (ch == '\t') {
            std::string completed = tabCompleteRotate(line);
            if (completed != line) {
                // 清除整行并重绘（包括提示符和补全内容）
                char cwd[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, cwd);
                std::cout << "\r";
                size_t total_len = strlen(cwd) + 1 + line.size();
                for (size_t i = 0; i < total_len; ++i) std::cout << " ";
                std::cout << "\r" << cwd << ">" << completed;
                line = completed;
                cursor = line.size();
            }
        } else if (ch == 3) {
            exit(0);
        } else if (ch == 224) {
            int arrow = _getch();
            if (arrow == 72) { // 上
                if (!g_history.empty() && g_historyIdx + 1 < (int)g_history.size()) {
                    g_historyIdx++;
                    for (int i = 0; i < cursor; ++i) std::cout << "\b";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << " ";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << "\b";
                    line = g_history[g_history.size() - 1 - g_historyIdx];
                    cursor = line.size();
                    std::cout << line;
                    g_tabState = TabState();
                }
            } else if (arrow == 80) { // 下
                if (g_historyIdx > 0) {
                    g_historyIdx--;
                    for (int i = 0; i < cursor; ++i) std::cout << "\b";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << " ";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << "\b";
                    line = g_history[g_history.size() - 1 - g_historyIdx];
                    cursor = line.size();
                    std::cout << line;
                    g_tabState = TabState();
                } else if (g_historyIdx == 0) {
                    g_historyIdx = -1;
                    for (int i = 0; i < cursor; ++i) std::cout << "\b";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << " ";
                    for (size_t i = 0; i < line.size(); ++i) std::cout << "\b";
                    line.clear();
                    cursor = 0;
                    g_tabState = TabState();
                }
            } else if (arrow == 75) { // 左
                if (cursor > 0) {
                    std::cout << "\b";
                    cursor--;
                }
            } else if (arrow == 77) { // 右
                if (cursor < (int)line.size()) {
                    std::cout << line[cursor];
                    cursor++;
                }
            }
        } else {
            line.insert(cursor, 1, (char)ch);
            for (size_t i = cursor; i < line.size(); ++i) std::cout << line[i];
            cursor++;
            for (size_t i = cursor; i < line.size(); ++i) std::cout << "\b";
            g_tabState = TabState();
        }
    }
    std::cout << std::endl;
    if (!line.empty() && (g_history.empty() || g_history.back() != line)) {
        g_history.push_back(line);
        if (g_history.size() > 100) g_history.pop_front();
        saveHistory();
    }
    DBG("input: " << line);
    return line;
}

extern "C" char **_environ;

// 环境变量替换
std::string replaceEnvVars(const std::string& input) {
    std::regex envPattern(R"(%(\w+)%|\$\{(\w+)\})");
    std::string result = input;
    std::smatch m;
    auto s = result.cbegin();
    while (std::regex_search(s, result.cend(), m, envPattern)) {
        std::string var = m[1].matched ? m[1].str() : m[2].str();
        char* val = getenv(var.c_str());
        result.replace(m.position(0), m.length(0), val ? val : "");
        s = result.cbegin() + m.position(0) + (val ? strlen(val) : 0);
    }
    return result;
}

// 打印环境变量
void printEnv(const std::string& key = "") {
    if (key.empty()) {
        for (char **env = _environ; *env; ++env) {
            std::cout << *env << std::endl;
        }
    } else {
        char* val = getenv(key.c_str());
        if (val)
            std::cout << key << "=" << val << std::endl;
        else
            std::cout << "环境变量未设置: " << key << std::endl;
    }
}

// 列出目录
void listDir(const std::string& path = ".") {
    DBG("listDir: " << path);
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        std::cout << "系统找不到指定的路径: " << path << std::endl;
        return;
    }
    do {
        std::cout << (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "[DIR] " : "      ") << ffd.cFileName << std::endl;
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}

// 列出进程
void listProcesses() {
    DBG("listProcesses");
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cout << "无法获取进程列表" << std::endl;
        return;
    }
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        std::cout << "PID\t进程名" << std::endl;
        do {
            char procName[MAX_PATH];
            size_t i = 0;
            for (; i < MAX_PATH - 1 && pe.szExeFile[i] != 0; ++i) {
                procName[i] = (char)pe.szExeFile[i];
            }
            procName[i] = 0;
            std::cout << pe.th32ProcessID << "\t" << procName << std::endl;
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

// 按进程名杀进程
void killProcessByName(const std::string& name) {
    DBG("killProcessByName: " << name);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cout << "无法获取进程列表" << std::endl;
        return;
    }
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(hSnap, &pe)) {
        do {
            char procName[MAX_PATH];
            size_t i = 0;
            for (; i < MAX_PATH - 1 && pe.szExeFile[i] != 0; ++i) {
                procName[i] = (char)pe.szExeFile[i];
            }
            procName[i] = 0;
            if (_stricmp(procName, name.c_str()) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (TerminateProcess(hProc, 0))
                        std::cout << "已终止进程: " << procName << " (PID=" << pe.th32ProcessID << ")" << std::endl;
                    else
                        std::cout << "终止失败: " << procName << std::endl;
                    CloseHandle(hProc);
                }
                found = true;
            }
        } while (Process32Next(hSnap, &pe));
    }
    if (!found)
        std::cout << "未找到进程: " << name << std::endl;
    CloseHandle(hSnap);
}

// 解析参数
std::vector<std::string> parseArgs(const std::string& params) {
    std::vector<std::string> args;
    size_t start = 0, end = 0;
    while ((end = params.find(',', start)) != std::string::npos) {
        args.push_back(params.substr(start, end - start));
        start = end + 1;
    }
    if (!params.empty())
        args.push_back(params.substr(start));
    return args;
}

// help 动态输出对象.方法及注释
void printHelpObjects() {
    std::vector<std::string> searchPaths = getSearchPaths();
    std::cout << "\n可用对象.方法：" << std::endl;
    for (const auto& dir : searchPaths) {
        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA((dir + "\\*").c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0) {
                    std::string objName = ffd.cFileName;
                    std::string objDir = dir + "\\" + objName;
                    std::map<std::string, std::string> helpMap;
                    std::string helpPath = objDir + "\\help.json";
                    FILE* fp = fopen(helpPath.c_str(), "rb");
                    if (fp) {
                        fseek(fp, 0, SEEK_END);
                        long len = ftell(fp);
                        fseek(fp, 0, SEEK_SET);
                        std::string json(len, '\0');
                        fread(&json[0], 1, len, fp);
                        fclose(fp);
                        size_t p = 0;
                        while ((p = json.find('"', p)) != std::string::npos) {
                            size_t p2 = json.find('"', p + 1);
                            if (p2 == std::string::npos) break;
                            std::string key = json.substr(p + 1, p2 - p - 1);
                            size_t c = json.find(':', p2);
                            if (c == std::string::npos) break;
                            size_t v1 = json.find('"', c);
                            size_t v2 = json.find('"', v1 + 1);
                            if (v1 == std::string::npos || v2 == std::string::npos) break;
                            std::string val = json.substr(v1 + 1, v2 - v1 - 1);
                            helpMap[key] = val;
                            p = v2 + 1;
                        }
                        DBG("read help.json: " << helpPath << " count=" << helpMap.size());
                    }
                    std::vector<std::string> methods = listAllFiles(objDir);
                    // 先收集所有方法及注释，计算最大长度
                    struct HelpEntry { std::string method; std::string comment; };
                    std::vector<HelpEntry> entries;
                    size_t maxlen = 0;
                    for (const auto& m : methods) {
                        // 只收集 .bat 文件，忽略文件夹和其它文件
                        if (m.size() <= 4 || m.substr(m.size() - 4) != ".bat") continue;
                        std::string name = m.substr(0, m.size() - 4);
                        if (!name.empty() && name.back() == '/') continue; // 忽略目录
                        std::string full = objName + "." + name;
                        std::string comment = helpMap.count(name) ? helpMap[name] : "";
                        entries.push_back({full, comment});
                        if (full.size() > maxlen) maxlen = full.size();
                    }
                    // 统一对齐输出
                    for (const auto& e : entries) {
                        std::cout << "  " << e.method;
                        size_t pad = maxlen > e.method.size() ? maxlen - e.method.size() : 0;
                        for (size_t i = 0; i < pad + 4; ++i) std::cout << " ";
                        if (!e.comment.empty()) std::cout << e.comment;
                        std::cout << std::endl;
                    }
                }
            } while (FindNextFileA(hFind, &ffd));
            FindClose(hFind);
        }
    }
}

void handleSingleCommand(const std::string& cmdline) {
    std::string line = replaceEnvVars(cmdline);

    if (line == "help") {
        std::cout << "ctcmd 支持如下命令：" << std::endl;
        std::cout << "  cd [目录]         切换目录" << std::endl;
        std::cout << "  set KEY=VALUE     设置环境变量" << std::endl;
        std::cout << "  env [KEY]         查看环境变量" << std::endl;
        std::cout << "  print 内容        输出内容（支持变量替换）" << std::endl;
        std::cout << "  ls [目录]         列出目录内容" << std::endl;
        std::cout << "  ps                查看进程列表" << std::endl;
        std::cout << "  pkill 进程名      按名杀进程" << std::endl;
        std::cout << "  对象.方法(参数)   调用批处理（如 power.shutdown(1)）" << std::endl;
        std::cout << "  exit              退出终端" << std::endl;
        printHelpObjects();
        return;
    }
    if (line == "ps") {
        listProcesses();
        return;
    }
    if (line.substr(0, 2) == "ls") {
        std::string path = ".";
        if (line.length() > 2 && (line[2] == ' ' || line[2] == '\t'))
            path = line.substr(3);
        path = replaceEnvVars(path);
        listDir(path);
        return;
    }
    if (line.substr(0, 5) == "pkill") {
        if (line.length() == 5 || (line.length() == 6 && (line[5] == ' ' || line[5] == '\t'))) {
            std::cout << "用法: pkill 进程名" << std::endl;
            return;
        }
        std::string name = line.substr(6);
        name = replaceEnvVars(name);
        killProcessByName(name);
        return;
    }
    if (line.substr(0, 2) == "cd") {
        std::string path = "";
        if (line.length() > 2 && (line[2] == ' ' || line[2] == '\t'))
            path = line.substr(3);
        char* home = getenv("USERPROFILE");
        std::string homeDir = home ? home : "C:\\";
        if (path.empty()) path = homeDir;
        path = replaceEnvVars(path);
        if (SetCurrentDirectoryA(path.c_str()))
        {
            char curdir[MAX_PATH] = {0};
            GetCurrentDirectoryA(MAX_PATH, curdir);
            SetEnvironmentVariableA("cd", curdir);
            _putenv(("cd=" + std::string(curdir)).c_str());
        }
        else
            std::cout << "系统找不到指定的路径: " << path << std::endl;
        return;
    }
    if (line.substr(0, 3) == "set") {
        if (line.length() == 3 || (line.length() == 4 && (line[3] == ' ' || line[3] == '\t'))) {
            std::cout << "用法: set KEY=VALUE" << std::endl;
            return;
        }
        if (line.substr(0, 4) == "set ") {
            size_t eq = line.find('=', 4);
            if (eq != std::string::npos) {
                std::string key = line.substr(4, eq - 4);
                std::string val = line.substr(eq + 1);
                if (SetEnvironmentVariableA(key.c_str(), val.c_str())) {
                    _putenv((key + "=" + val).c_str());
                    std::cout << "已设置环境变量: " << key << "=" << val << std::endl;
                } else {
                    std::cout << "设置环境变量失败: " << key << std::endl;
                }
            } else {
                std::cout << "用法: set KEY=VALUE" << std::endl;
            }
            return;
        }
    }
    if (line.substr(0, 3) == "env") {
        if (line.length() == 3)
            printEnv();
        else
            printEnv(line.substr(4));
        return;
    }
    if (line.substr(0, 6) == "print ") {
        std::string msg = line.substr(6);
        msg = replaceEnvVars(msg);
        std::cout << msg << std::endl;
        return;
    }

    // power.xxx(1,2,3) 语法，支持 path 文件指定目录
    std::regex pattern(R"((\w+)\.(\w+)\((.*)\))");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        std::cout << cmdline << " 不是内部或外部命令，也不是可运行的程序或批处理文件。" << std::endl;
        return;
    }
    std::string object = match[1];
    std::string method = match[2];
    std::string params = match[3];
    std::vector<std::string> args = parseArgs(params);

    std::vector<std::string> searchPaths = getSearchPaths();
    std::string batPath;
    bool found = false;
    for (const auto& dir : searchPaths) {
        batPath = dir;
        if (batPath.back() != '\\' && batPath.back() != '/')
            batPath += "\\";
        batPath += object + "\\" + method + ".bat";
        if (_access(batPath.c_str(), 0) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        batPath = "./" + object + "/" + method + ".bat";
        if (_access(batPath.c_str(), 0) == 0) {
            found = true;
        }
    }
    if (!found) {
        std::cout << "系统找不到指定的文件: " << object << "/" << method << ".bat" << std::endl;
        return;
    }

    std::string cmd = "\"" + batPath + "\"";
    for (const auto& arg : args) {
        if (!arg.empty())
            cmd += " " + arg;
    }

    DBG("run bat: " << cmd);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL success = CreateProcessA(
        NULL,
        (LPSTR)cmd.c_str(),
        NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );
    if (!success) {
        std::cout << "无法启动批处理文件: " << cmd << std::endl;
        return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char* argv[]) {
    char* home = getenv("USERPROFILE");
    std::string homeDir = home ? home : "C:\\";
    std::regex pattern(R"((\w+)\.(\w+)\((.*)\))");

    if (argc == 2) {
        std::string arg1 = argv[1];
        if (arg1 == "help") {
            std::cout << "ctcmd 支持如下命令：" << std::endl;
            std::cout << "  cd [目录]         切换目录" << std::endl;
            std::cout << "  set KEY=VALUE     设置环境变量" << std::endl;
            std::cout << "  env [KEY]         查看环境变量" << std::endl;
            std::cout << "  print 内容        输出内容（支持变量替换）" << std::endl;
            std::cout << "  ls [目录]         列出目录内容" << std::endl;
            std::cout << "  ps                查看进程列表" << std::endl;
            std::cout << "  pkill 进程名      按名杀进程" << std::endl;
            std::cout << "  对象.方法(参数)   调用批处理（如 power.shutdown(1)）" << std::endl;
            std::cout << "  exit              退出终端" << std::endl;
            printHelpObjects();
            return 0;
        }
        handleSingleCommand(argv[1]);
        return 0;
    }

    std::cout << "Custom CMD [版本 10.0.26100.4946]" << std::endl;
    std::cout << "(c) OracleLoadStar。保留所有权利。" << std::endl;
    std::cout << "ctcmd - 自定义终端，输入 help 获取命令帮助。" << std::endl;
    std::string line;
    loadHistory();
    while (true) {
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        SetEnvironmentVariableA("cd", cwd);
        _putenv(("cd=" + std::string(cwd)).c_str());
        std::cout << cwd << ">" << std::flush;
        line = getlineWithTab();
        if (line.empty()) continue;
        if (line == "exit") break;
        handleSingleCommand(line);
    }
    saveHistory();
    // std::cout << "已退出 ctcmd 终端。" << std::endl;
    return 0;
}
