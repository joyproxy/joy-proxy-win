#pragma once

#include "joyproxy/config.h"

#include <Windows.h>
#include <string>

namespace joyproxy {

class ConfigMapping {
public:
    bool Create(const HookConfig& cfg);
    void Close();
    bool Update(const HookConfig& cfg);
    bool Disable();

private:
    HANDLE map_ = nullptr;
    HookConfig* view_ = nullptr;
};

std::wstring NormalizePath(const std::wstring& path);
bool PathsEqual(const std::wstring& a, const std::wstring& b);

}  // namespace joyproxy
