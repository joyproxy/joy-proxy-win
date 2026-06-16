#include "config_mapping.h"
#include "injector.h"
#include "json_util.h"
#include "pipe_server.h"

#include <joyproxy/security.h>

#include <Psapi.h>
#include <ShlObj.h>
#include <TlHelp32.h>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

std::mutex g_state_mutex;
joyproxy::ConfigMapping g_mapping;
joyproxy::HookConfig g_active_cfg{};
std::wstring g_target_exe;
std::wstring g_hook_dll_path;
std::set<DWORD> g_injected_pids;
DWORD g_parent_pid = 0;
bool g_running = false;
bool g_should_exit = false;
std::thread g_watch_thread;
joyproxy::PipeServer* g_pipe_server = nullptr;
HANDLE g_mutex = nullptr;

void LogLine(const char* msg) {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base))) {
        return;
    }
    std::wstring dir = std::wstring(base) + L"\\JoyProxy";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\service.log";
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"a");
    if (!f) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u %s\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
        st.wSecond, msg);
    fclose(f);
}

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path = buf;
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    }
    return path;
}

void StopSessionUnlocked() {
    g_running = false;
    g_mapping.Disable();
}

void StopSession() {
    std::lock_guard lock(g_state_mutex);
    StopSessionUnlocked();
}

void JoinWatchThread() {
    if (g_watch_thread.joinable()) {
        g_watch_thread.join();
    }
    g_mapping.Close();
}

std::wstring GetProcessPath(DWORD pid) {
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        return {};
    }
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(proc, 0, buf, &size)) {
        result = buf;
    }
    CloseHandle(proc);
    return result;
}

void KillStaleServices() {
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"JoyProxyService.exe") != 0 || pe.th32ProcessID == self) {
                continue;
            }
            HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (!proc) {
                continue;
            }
            TerminateProcess(proc, 0);
            WaitForSingleObject(proc, 3000);
            CloseHandle(proc);
            LogLine("terminated stale JoyProxyService");
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void InjectMatchingProcesses() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID <= 4) {
                continue;
            }
            const std::wstring path = GetProcessPath(pe.th32ProcessID);
            if (path.empty()) {
                continue;
            }
            if (path.find(L"JoyProxy") != std::wstring::npos) {
                continue;
            }
            if (!joyproxy::PathsEqual(path, g_target_exe)) {
                continue;
            }
            std::lock_guard lock(g_state_mutex);
            if (g_injected_pids.count(pe.th32ProcessID) != 0) {
                continue;
            }
            std::string err;
            if (joyproxy::InjectDll(pe.th32ProcessID, g_hook_dll_path, err)) {
                g_injected_pids.insert(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void WatchLoop() {
    while (g_running) {
        InjectMatchingProcesses();
        Sleep(1500);
    }
}

bool ParseStartPayload(const std::string& json, joyproxy::HookConfig& cfg, std::wstring& exe_path) {
    const std::string proxy_type = joyproxy::JsonGetString(json, "proxyType");
    if (proxy_type == "http") {
        cfg.proxy_type = joyproxy::ProxyType::Http;
    } else {
        cfg.proxy_type = joyproxy::ProxyType::Socks5;
    }
    const std::string host = joyproxy::JsonGetString(json, "host");
    const int port = joyproxy::JsonGetInt(json, "port", 0);
    const std::string user = joyproxy::JsonGetString(json, "username");
    const std::string pass = joyproxy::JsonGetString(json, "password");
    const std::string exe = joyproxy::JsonGetString(json, "exePath");
    if (host.empty() || port <= 0 || exe.empty()) {
        return false;
    }
    strncpy_s(cfg.proxy_host, host.c_str(), _TRUNCATE);
    cfg.proxy_port = static_cast<uint16_t>(port);
    strncpy_s(cfg.proxy_user, user.c_str(), _TRUNCATE);
    strncpy_s(cfg.proxy_pass, pass.c_str(), _TRUNCATE);
    MultiByteToWideChar(CP_UTF8, 0, exe.c_str(), -1, cfg.target_exe_path, 512);
    exe_path = joyproxy::NormalizePath(std::wstring(cfg.target_exe_path));
    wcsncpy_s(cfg.target_exe_path, exe_path.c_str(), _TRUNCATE);
    cfg.magic = joyproxy::kHookConfigMagic;
    cfg.version = 1;
    cfg.enabled = 1;
    return true;
}

std::string HandleRequest(const std::string& line) {
    const std::string type = joyproxy::JsonGetString(line, "type");
    const int id = joyproxy::JsonGetInt(line, "id", 0);
    if (type == "ping") {
        return "{\"type\":\"pong\",\"id\":" + std::to_string(id) + "}";
    }
    if (type == "stop") {
        StopSession();
        JoinWatchThread();
        return "{\"type\":\"stopped\",\"id\":" + std::to_string(id) + "}";
    }
    if (type == "shutdown") {
        StopSession();
        JoinWatchThread();
        g_should_exit = true;
        return "{\"type\":\"shutdown\",\"id\":" + std::to_string(id) + "}";
    }
    if (type == "status") {
        std::lock_guard lock(g_state_mutex);
        const char* state = g_running ? "running" : "stopped";
        return std::string("{\"type\":\"status\",\"id\":") + std::to_string(id) + ",\"payload\":{\"state\":\"" + state +
               "\",\"injected\":" + std::to_string(g_injected_pids.size()) + "}}";
    }
    if (type == "start") {
        joyproxy::HookConfig cfg{};
        std::wstring exe_path;
        if (!ParseStartPayload(line, cfg, exe_path)) {
            return "{\"type\":\"error\",\"id\":" + std::to_string(id) +
                   ",\"code\":\"INVALID_CONFIG\",\"message\":\"invalid start payload\"}";
        }
        {
            std::lock_guard lock(g_state_mutex);
            if (g_running) {
                return "{\"type\":\"error\",\"id\":" + std::to_string(id) +
                       ",\"code\":\"ALREADY_RUNNING\",\"message\":\"session already active\"}";
            }
            g_active_cfg = cfg;
            g_target_exe = exe_path;
            g_injected_pids.clear();
            if (!g_mapping.Create(g_active_cfg)) {
                return "{\"type\":\"error\",\"id\":" + std::to_string(id) +
                       ",\"code\":\"CONFIG_MAP_FAILED\",\"message\":\"cannot create shared config\"}";
            }
            g_running = true;
        }
        InjectMatchingProcesses();
        g_watch_thread = std::thread(WatchLoop);
        LogLine("session started");
        return "{\"type\":\"started\",\"id\":" + std::to_string(id) + ",\"payload\":{\"engine\":\"winsock-hook\"}}";
    }
    return "{\"type\":\"error\",\"id\":" + std::to_string(id) +
           ",\"code\":\"UNKNOWN\",\"message\":\"unknown request\"}";
}

DWORD WINAPI ParentWatchThread(LPVOID param) {
    const DWORD parent = *static_cast<DWORD*>(param);
    if (parent == 0) {
        return 0;
    }
    const HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, parent);
    if (proc) {
        WaitForSingleObject(proc, INFINITE);
        CloseHandle(proc);
    }
    LogLine("parent exited, shutting down");
    g_should_exit = true;
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    bool replace_stale = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--replace") == 0) {
            replace_stale = true;
        }
        if (wcscmp(argv[i], L"--parent-pid") == 0 && i + 1 < argc) {
            g_parent_pid = static_cast<DWORD>(_wtoi(argv[i + 1]));
        }
    }

    if (replace_stale) {
        KillStaleServices();
        Sleep(400);
    }

    g_mutex = CreateMutexW(joyproxy::EveryoneSecurityAttributes(), TRUE, L"Global\\JoyProxyService_v1");
    if (!g_mutex) {
        LogLine("CreateMutex failed");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (replace_stale) {
            KillStaleServices();
            Sleep(400);
            CloseHandle(g_mutex);
            g_mutex = CreateMutexW(joyproxy::EveryoneSecurityAttributes(), TRUE, L"Global\\JoyProxyService_v1");
            if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
                LogLine("mutex still held after replace");
                return 2;
            }
        } else {
            LogLine("another instance already running");
            return 0;
        }
    }

    if (g_parent_pid != 0) {
        HANDLE thread = CreateThread(nullptr, 0, ParentWatchThread, &g_parent_pid, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }

    const std::wstring dir = GetExeDir();
    g_hook_dll_path = dir + L"JoyProxyHook.dll";
    LogLine("JoyProxyService started");

    joyproxy::PipeServer server(LR"(\\.\pipe\joyproxy-v1)");
    g_pipe_server = &server;
    if (!server.Start(HandleRequest)) {
        LogLine("PipeServer start failed");
        return 1;
    }

    while (!g_should_exit) {
        Sleep(100);
    }

    StopSession();
    JoinWatchThread();
    server.Stop();
    if (g_mutex) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
    LogLine("JoyProxyService exit");
    return 0;
}
