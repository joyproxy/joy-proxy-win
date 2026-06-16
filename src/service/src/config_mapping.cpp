#include "config_mapping.h"

#include <joyproxy/security.h>

#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace joyproxy {

std::wstring NormalizePath(const std::wstring& path) {
    wchar_t buf[MAX_PATH] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH, buf, nullptr) == 0) {
        return path;
    }
    std::wstring out = buf;
    for (auto& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return NormalizePath(a) == NormalizePath(b);
}

bool ConfigMapping::Create(const HookConfig& cfg) {
    Close();
    map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, EveryoneSecurityAttributes(), PAGE_READWRITE, 0,
        sizeof(HookConfig), SharedConfigName());
    if (!map_) {
        return false;
    }
    view_ = static_cast<HookConfig*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HookConfig)));
    if (!view_) {
        CloseHandle(map_);
        map_ = nullptr;
        return false;
    }
    *view_ = cfg;
    return true;
}

bool ConfigMapping::Update(const HookConfig& cfg) {
    if (!view_) {
        return false;
    }
    *view_ = cfg;
    return true;
}

bool ConfigMapping::Disable() {
    if (!view_) {
        return false;
    }
    view_->enabled = 0;
    return true;
}

void ConfigMapping::Close() {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (map_) {
        CloseHandle(map_);
        map_ = nullptr;
    }
}

}  // namespace joyproxy
