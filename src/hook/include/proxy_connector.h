#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <joyproxy/config.h>
#include <string>

namespace joyproxy {

class ProxyConnector {
public:
    static bool ConnectThroughProxy(
        SOCKET client_socket,
        const sockaddr* dest,
        int dest_len,
        const HookConfig& cfg);

private:
    static bool ConnectSocks5(SOCKET s, const std::string& host, uint16_t port, const HookConfig& cfg);
    static bool ConnectHttp(SOCKET s, const std::string& host, uint16_t port, const HookConfig& cfg);
    static bool ConnectToProxy(SOCKET s, const HookConfig& cfg);
    static bool ResolveDest(const sockaddr* dest, int dest_len, std::string& host, uint16_t& port);
};

}  // namespace joyproxy
