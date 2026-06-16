#pragma once

#include <Windows.h>
#include <functional>
#include <string>

namespace joyproxy {

class PipeServer {
public:
    using Handler = std::function<std::string(const std::string& line)>;

    explicit PipeServer(std::wstring pipe_name);
    ~PipeServer();

    bool Start(Handler handler);
    void Stop();

private:
    std::wstring pipe_name_;
    Handler handler_;
    HANDLE stop_event_ = nullptr;
    HANDLE thread_ = nullptr;
    static DWORD WINAPI ThreadProc(LPVOID param);
    void Run();
};

}  // namespace joyproxy
