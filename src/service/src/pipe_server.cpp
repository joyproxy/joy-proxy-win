#include "pipe_server.h"

#include <joyproxy/security.h>

#include <string>

namespace joyproxy {

PipeServer::PipeServer(std::wstring pipe_name) : pipe_name_(std::move(pipe_name)) {}

PipeServer::~PipeServer() {
    Stop();
}

DWORD WINAPI PipeServer::ThreadProc(LPVOID param) {
    auto* self = static_cast<PipeServer*>(param);
    self->Run();
    return 0;
}

bool PipeServer::Start(Handler handler) {
    Stop();
    handler_ = std::move(handler);
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) {
        return false;
    }
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    return thread_ != nullptr;
}

void PipeServer::Stop() {
    if (stop_event_) {
        SetEvent(stop_event_);
    }
    if (thread_) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

void PipeServer::Run() {
    while (WaitForSingleObject(stop_event_, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            65536,
            65536,
            0,
            EveryoneSecurityAttributes());
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        std::string buffer;
        char chunk[4096];
        DWORD read = 0;
        while (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
            buffer.append(chunk, chunk + read);
            size_t pos = 0;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                buffer.erase(0, pos + 1);
                if (line.empty() || !handler_) {
                    continue;
                }
                const std::string response = handler_(line);
                if (!response.empty()) {
                    std::string out = response;
                    if (out.back() != '\n') {
                        out.push_back('\n');
                    }
                    DWORD written = 0;
                    WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

}  // namespace joyproxy
