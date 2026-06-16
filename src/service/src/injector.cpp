#include "injector.h"

#include <TlHelp32.h>

namespace joyproxy {

bool InjectDll(DWORD pid, const std::wstring& dll_path, std::string& error) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!process) {
        error = "OpenProcess failed";
        return false;
    }

    const size_t size = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        error = "VirtualAllocEx failed";
        CloseHandle(process);
        return false;
    }
    if (!WriteProcessMemory(process, remote, dll_path.c_str(), size, nullptr)) {
        error = "WriteProcessMemory failed";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    const auto load_lib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(k32, "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_lib, remote, 0, nullptr);
    if (!thread) {
        error = "CreateRemoteThread failed";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    WaitForSingleObject(thread, 15000);
    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    if (exit_code == 0) {
        error = "LoadLibraryW returned null";
        return false;
    }
    return true;
}

}  // namespace joyproxy
