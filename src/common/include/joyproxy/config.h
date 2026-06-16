#pragma once

#include <cstdint>

namespace joyproxy {

enum class ProxyType : uint8_t { Socks5 = 0, Http = 1 };

#pragma pack(push, 1)
struct HookConfig {
    uint32_t magic;          // 'JPX1'
    uint32_t version;        // 1
    ProxyType proxy_type;
    uint16_t proxy_port;
    uint16_t dest_port_reserved;
    char proxy_host[256];
    char proxy_user[128];
    char proxy_pass[128];
    wchar_t target_exe_path[512];
    uint8_t enabled;
    uint8_t reserved[3];
};
#pragma pack(pop)

constexpr uint32_t kHookConfigMagic = 0x3158504A; // JPX1

const wchar_t* SharedConfigName();

}  // namespace joyproxy
