// ctcmd - �Զ����ն�������֧������ power.shutdown(1) �����������
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <cstdlib>
#include <cstdio>

extern "C" char **_environ;

// ��ȡ path.txt����ȡ�������ļ�����Ŀ¼��UTF-8+BOM��
std::vector<std::string> getSearchPaths() {
    std::vector<std::string> paths;
    const char* rcfile = "path.txt";
#ifdef DEBUG
    printf("[DEBUG] �������ļ�: %s\n", rcfile);
#endif
    FILE* fp = fopen(rcfile, "rb");
    if (!fp) {
#ifdef DEBUG
        printf("[DEBUG] δ�ҵ������ļ������Դ���...\n");
#endif
        char cwd[MAX_PATH] = {0};
        GetCurrentDirectoryA(MAX_PATH, cwd);
        FILE* wf = fopen(rcfile, "wb");
        if (wf) {
            unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
            fwrite(bom, 1, 3, wf);
            fwrite(cwd, 1, strlen(cwd), wf);
            fwrite("\n", 1, 1, wf);
            fclose(wf);
#ifdef DEBUG
            printf("[DEBUG] ��д�뵱ǰĿ¼�������ļ�: %s\n", cwd);
#endif
        }
        fp = fopen(rcfile, "rb");
        if (!fp) {
#ifdef DEBUG
            printf("[DEBUG] ��ȡ�����ļ���ʧ�ܣ�ֱ�ӷ��ص�ǰĿ¼\n");
#endif
            paths.push_back(std::string(cwd));
            return paths;
        }
    }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    size_t offset = 0;
    if (n >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        offset = 3;
#ifdef DEBUG
        printf("[DEBUG] ��⵽BOM��������\n");
#endif
    }
    std::string content(buf + offset, n - offset);
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        std::string line = (end == std::string::npos) ? content.substr(pos) : content.substr(pos, end - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) {
#ifdef DEBUG
            printf("[DEBUG] ��ȡ��·��: %s\n", line.c_str());
#endif
            paths.push_back(line);
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    fclose(fp);
#ifdef DEBUG
    printf("[DEBUG] ·���б�����: %d\n", (int)paths.size());
#endif
    return paths;
}

// ��������
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

// ���������滻
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

// ��ӡ��������
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
            std::cout << "��������δ����: " << key << std::endl;
    }
}

// �г�Ŀ¼
void listDir(const std::string& path = ".") {
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        std::cout << "ϵͳ�Ҳ���ָ����·��: " << path << std::endl;
        return;
    }
    do {
        std::cout << (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "[DIR] " : "      ") << ffd.cFileName << std::endl;
    } while (FindNextFileA(hFind, &ffd));
    FindClose(hFind);
}

// �г�����
void listProcesses() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cout << "�޷���ȡ�����б�" << std::endl;
        return;
    }
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        std::cout << "PID\t������" << std::endl;
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

// ��������ɱ����
void killProcessByName(const std::string& name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cout << "�޷���ȡ�����б�" << std::endl;
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
                        std::cout << "����ֹ����: " << procName << " (PID=" << pe.th32ProcessID << ")" << std::endl;
                    else
                        std::cout << "��ֹʧ��: " << procName << std::endl;
                    CloseHandle(hProc);
                }
                found = true;
            }
        } while (Process32Next(hSnap, &pe));
    }
    if (!found)
        std::cout << "δ�ҵ�����: " << name << std::endl;
    CloseHandle(hSnap);
}

// ��������������в���ģʽ��
void handleSingleCommand(const std::string& cmdline) {
    std::string line = replaceEnvVars(cmdline);

    // �ڲ�����
    if (line == "help") {
        std::cout << "ctcmd ֧���������" << std::endl;
        std::cout << "  cd [Ŀ¼]         �л�Ŀ¼" << std::endl;
        std::cout << "  set KEY=VALUE     ���û�������" << std::endl;
        std::cout << "  env [KEY]         �鿴��������" << std::endl;
        std::cout << "  print ����        ������ݣ�֧�ֱ����滻��" << std::endl;
        std::cout << "  ls [Ŀ¼]         �г�Ŀ¼����" << std::endl;
        std::cout << "  ps                �鿴�����б�" << std::endl;
        std::cout << "  pkill ������      ����ɱ����" << std::endl;
        std::cout << "  ����.����(����)   ������������ power.shutdown(1)��" << std::endl;
        std::cout << "  exit              �˳��ն�" << std::endl;
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
            std::cout << "�÷�: pkill ������" << std::endl;
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
            ;
        else
            std::cout << "ϵͳ�Ҳ���ָ����·��: " << path << std::endl;
        return;
    }
    if (line.substr(0, 3) == "set") {
        if (line.length() == 3 || (line.length() == 4 && (line[3] == ' ' || line[3] == '\t'))) {
            std::cout << "�÷�: set KEY=VALUE" << std::endl;
            return;
        }
        if (line.substr(0, 4) == "set ") {
            size_t eq = line.find('=', 4);
            if (eq != std::string::npos) {
                std::string key = line.substr(4, eq - 4);
                std::string val = line.substr(eq + 1);
                if (SetEnvironmentVariableA(key.c_str(), val.c_str())) {
                    _putenv((key + "=" + val).c_str());
                    std::cout << "�����û�������: " << key << "=" << val << std::endl;
                } else {
                    std::cout << "���û�������ʧ��: " << key << std::endl;
                }
            } else {
                std::cout << "�÷�: set KEY=VALUE" << std::endl;
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

    // power.xxx(1,2,3) �﷨��֧�� path �ļ�ָ��Ŀ¼
    std::regex pattern(R"((\w+)\.(\w+)\((.*)\))");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        std::cout << cmdline << " �����ڲ����ⲿ���Ҳ���ǿ����еĳ�����������ļ���" << std::endl;
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
            batPath += "/";
        batPath += object + "/" + method + ".bat";
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
        std::cout << "ϵͳ�Ҳ���ָ�����ļ�: " << object << "/" << method << ".bat" << std::endl;
        return;
    }

    std::string cmd = "\"" + batPath + "\"";
    for (const auto& arg : args) {
        if (!arg.empty())
            cmd += " " + arg;
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL success = CreateProcessA(
        NULL,
        (LPSTR)cmd.c_str(),
        NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );
    if (!success) {
        std::cout << "�޷������������ļ�: " << cmd << std::endl;
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

    // �����в���ģʽ
    if (argc == 2) {
        std::string arg1 = argv[1];
        if (arg1 == "help") {
            std::cout << "ctcmd ֧���������" << std::endl;
            std::cout << "  cd [Ŀ¼]         �л�Ŀ¼" << std::endl;
            std::cout << "  set KEY=VALUE     ���û�������" << std::endl;
            std::cout << "  env [KEY]         �鿴��������" << std::endl;
            std::cout << "  print ����        ������ݣ�֧�ֱ����滻��" << std::endl;
            std::cout << "  ls [Ŀ¼]         �г�Ŀ¼����" << std::endl;
            std::cout << "  ps                �鿴�����б�" << std::endl;
            std::cout << "  pkill ������      ����ɱ����" << std::endl;
            std::cout << "  ����.����(����)   ������������ power.shutdown(1)��" << std::endl;
            std::cout << "  exit              �˳��ն�" << std::endl;
            return 0;
        }
        handleSingleCommand(argv[1]);
        return 0;
    }

    std::cout << "Custom Cmd [�汾 1.0.0]" << std::endl;
    std::cout << "(c) OracleLoadStar����������Ȩ����" << std::endl;
    std::cout << "ctcmd - �Զ����նˣ����� help ��ȡ���������" << std::endl;
    std::string line;
    while (true) {
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        std::cout << cwd << ">" << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "exit") break;

        // help ����
        if (line == "help") {
            std::cout << "ctcmd ֧���������" << std::endl;
            std::cout << "  cd [Ŀ¼]         �л�Ŀ¼" << std::endl;
            std::cout << "  set KEY=VALUE     ���û�������" << std::endl;
            std::cout << "  env [KEY]         �鿴��������" << std::endl;
            std::cout << "  print ����        ������ݣ�֧�ֱ����滻��" << std::endl;
            std::cout << "  ls [Ŀ¼]         �г�Ŀ¼����" << std::endl;
            std::cout << "  ps                �鿴�����б�" << std::endl;
            std::cout << "  pkill ������      ����ɱ����" << std::endl;
            std::cout << "  ����.����(����)   ������������ power.shutdown(1)��" << std::endl;
            std::cout << "  exit              �˳��ն�" << std::endl;
            continue;
        }
        // cd ����
        if (line.substr(0, 2) == "cd") {
            std::string path = "";
            if (line.length() > 2 && (line[2] == ' ' || line[2] == '\t'))
                path = line.substr(3);
            if (path.empty()) path = homeDir;
            path = replaceEnvVars(path);
            if (SetCurrentDirectoryA(path.c_str()))
                ;
            else
                std::cout << "ϵͳ�Ҳ���ָ����·��: " << path << std::endl;
            continue;
        }

        // set ����
        if (line.substr(0, 3) == "set") {
            if (line.length() == 3 || (line.length() == 4 && (line[3] == ' ' || line[3] == '\t'))) {
                std::cout << "�÷�: set KEY=VALUE" << std::endl;
                continue;
            }
            if (line.substr(0, 4) == "set ") {
                size_t eq = line.find('=', 4);
                if (eq != std::string::npos) {
                    std::string key = line.substr(4, eq - 4);
                    std::string val = line.substr(eq + 1);
                    if (SetEnvironmentVariableA(key.c_str(), val.c_str())) {
                        _putenv((key + "=" + val).c_str());
                        std::cout << "�����û�������: " << key << "=" << val << std::endl;
                    } else {
                        std::cout << "���û�������ʧ��: " << key << std::endl;
                    }
                } else {
                    std::cout << "�÷�: set KEY=VALUE" << std::endl;
                }
                continue;
            }
        }

        // env ����
        if (line.substr(0, 3) == "env") {
            if (line.length() == 3)
                printEnv();
            else
                printEnv(line.substr(4));
            continue;
        }

        // print ����
        if (line.substr(0, 6) == "print ") {
            std::string msg = line.substr(6);
            msg = replaceEnvVars(msg);
            std::cout << msg << std::endl;
            continue;
        }

        // ls ����
        if (line.substr(0, 2) == "ls") {
            std::string path = ".";
            if (line.length() > 2 && (line[2] == ' ' || line[2] == '\t'))
                path = line.substr(3);
            path = replaceEnvVars(path);
            listDir(path);
            continue;
        }

        // ps ����
        if (line == "ps") {
            listProcesses();
            continue;
        }

        // pkill ����
        if (line.substr(0, 5) == "pkill") {
            if (line.length() == 5 || (line.length() == 6 && (line[5] == ' ' || line[5] == '\t'))) {
                std::cout << "�÷�: pkill ������" << std::endl;
                continue;
            }
            std::string name = line.substr(6);
            name = replaceEnvVars(name);
            killProcessByName(name);
            continue;
        }

        // �����滻
        line = replaceEnvVars(line);

        std::smatch match;
        if (!std::regex_match(line, match, pattern)) {
            std::cout << line << " �����ڲ����ⲿ���Ҳ���ǿ����еĳ�����������ļ���" << std::endl;
            continue;
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
                batPath += "/";
            batPath += object + "/" + method + ".bat";
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
            std::cout << "ϵͳ�Ҳ���ָ�����ļ�: " << object << "/" << method << ".bat" << std::endl;
            continue;
        }

        std::string cmd = "\"" + batPath + "\"";
        for (const auto& arg : args) {
            if (!arg.empty())
                cmd += " " + arg;
        }

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        BOOL success = CreateProcessA(
            NULL,
            (LPSTR)cmd.c_str(),
            NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
        );
        if (!success) {
            std::cout << "�޷������������ļ�: " << cmd << std::endl;
            continue;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    std::cout << "���˳� ctcmd �նˡ�" << std::endl;
    return 0;
}