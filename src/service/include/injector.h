#pragma once

#include <Windows.h>
#include <string>

namespace joyproxy {

bool InjectDll(DWORD pid, const std::wstring& dll_path, std::string& error);

}  // namespace joyproxy
