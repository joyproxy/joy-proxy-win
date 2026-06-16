#include "joyproxy/config.h"
#include "proxy_connector.h"

#include <MinHook.h>

#include <mutex>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace {

using ConnectFn = int (WSAAPI*)(SOCKET, const sockaddr*, int);
using WSAConnectFn = int (WSAAPI*)(SOCKET, const sockaddr*, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);

ConnectFn g_real_connect = nullptr;
WSAConnectFn g_real_wsaconnect = nullptr;

joyproxy::HookConfig g_cfg{};
bool g_cfg_loaded = false;
std::once_flag g_init_flag;
std::mutex g_log_mutex;

void LogLine(const char* msg) {
    std::lock_guard lock(g_log_mutex);
    char path[MAX_PATH] = {};
    if (GetEnvironmentVariableA("JOYPROXY_LOG", path, MAX_PATH) == 0) {
        return;
    }
    FILE* f = nullptr;
    fopen_s(&f, path, "a");
    if (!f) {
        return;
    }
    fprintf(f, "%s\n", msg);
    fclose(f);
}

bool LoadConfig() {
    const HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, joyproxy::SharedConfigName());
    if (!map) {
        return false;
    }
    const void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(joyproxy::HookConfig));
    if (!view) {
        CloseHandle(map);
        return false;
    }
    g_cfg = *static_cast<const joyproxy::HookConfig*>(view);
    UnmapViewOfFile(view);
    CloseHandle(map);
    g_cfg_loaded = g_cfg.magic == joyproxy::kHookConfigMagic && g_cfg.version == 1 && g_cfg.enabled != 0;
    return g_cfg_loaded;
}

bool ShouldBypassDest(const sockaddr* addr, int len) {
    if (!addr || len < static_cast<int>(sizeof(sockaddr_in))) {
        return true;
    }
    if (addr->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
        const uint32_t ip = ntohl(in->sin_addr.S_un.S_addr);
        if ((ip >> 24) == 127 || ip == 0) {
            return true;
        }
        const uint16_t port = ntohs(in->sin_port);
        if (port == g_cfg.proxy_port) {
            return true;
        }
    }
    return false;
}

int WSAAPI HookConnect(SOCKET s, const sockaddr* name, int namelen) {
    if (!g_cfg_loaded || !name) {
        return g_real_connect(s, name, namelen);
    }
    if (ShouldBypassDest(name, namelen)) {
        return g_real_connect(s, name, namelen);
    }
    if (joyproxy::ProxyConnector::ConnectThroughProxy(s, name, namelen, g_cfg)) {
        return 0;
    }
    LogLine("ConnectThroughProxy failed, falling back");
    return g_real_connect(s, name, namelen);
}

int WSAAPI HookWSAConnect(
    SOCKET s,
    const sockaddr* name,
    int namelen,
    LPWSABUF,
    LPWSABUF,
    LPQOS,
    LPQOS) {
    return HookConnect(s, name, namelen);
}

void InstallHooks() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    LoadConfig();
    if (MH_Initialize() != MH_OK) {
        return;
    }
    MH_CreateHookApi(L"ws2_32", "connect", &HookConnect, reinterpret_cast<LPVOID*>(&g_real_connect));
    MH_CreateHookApi(
        L"ws2_32", "WSAConnect", &HookWSAConnect, reinterpret_cast<LPVOID*>(&g_real_wsaconnect));
    MH_EnableHook(MH_ALL_HOOKS);
}

void RemoveHooks() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            std::call_once(g_init_flag, InstallHooks);
            break;
        case DLL_PROCESS_DETACH:
            RemoveHooks();
            break;
        default:
            break;
    }
    return TRUE;
}
