#include "proxy_connector.h"

#include <MSWSock.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace joyproxy {

static bool RecvExact(SOCKET s, char* buf, int len) {
    int got = 0;
    while (got < len) {
        const int n = recv(s, buf + got, len - got, 0);
        if (n <= 0) {
            return false;
        }
        got += n;
    }
    return true;
}

static bool SendAll(SOCKET s, const void* data, int len) {
    const char* p = static_cast<const char*>(data);
    int sent = 0;
    while (sent < len) {
        const int n = send(s, p + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

bool ProxyConnector::ResolveDest(const sockaddr* dest, int dest_len, std::string& host, uint16_t& port) {
    if (!dest || dest_len <= 0) {
        return false;
    }
    if (dest->sa_family == AF_INET && dest_len >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(dest);
        char buf[INET_ADDRSTRLEN] = {};
        if (!InetNtopA(AF_INET, const_cast<IN_ADDR*>(&in->sin_addr), buf, sizeof(buf))) {
            return false;
        }
        host = buf;
        port = ntohs(in->sin_port);
        return true;
    }
    if (dest->sa_family == AF_INET6 && dest_len >= static_cast<int>(sizeof(sockaddr_in6))) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(dest);
        char buf[INET6_ADDRSTRLEN] = {};
        if (!InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&in6->sin6_addr), buf, sizeof(buf))) {
            return false;
        }
        host = buf;
        port = ntohs(in6->sin6_port);
        return true;
    }
    return false;
}

bool ProxyConnector::ConnectToProxy(SOCKET s, const HookConfig& cfg) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char port_buf[16];
    _snprintf_s(port_buf, sizeof(port_buf), "%hu", cfg.proxy_port);
    addrinfo* result = nullptr;
    if (getaddrinfo(cfg.proxy_host, port_buf, &hints, &result) != 0 || !result) {
        return false;
    }
    bool ok = false;
    for (addrinfo* p = result; p; p = p->ai_next) {
        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            ok = true;
            break;
        }
    }
    freeaddrinfo(result);
    return ok;
}

bool ProxyConnector::ConnectSocks5(SOCKET s, const std::string& host, uint16_t port, const HookConfig& cfg) {
    if (!ConnectToProxy(s, cfg)) {
        return false;
    }

    const bool has_auth = cfg.proxy_user[0] != '\0';
    unsigned char greeting[3] = {0x05, has_auth ? 2u : 1u, has_auth ? 0x02u : 0x00u};
    if (!SendAll(s, greeting, 2 + greeting[1])) {
        return false;
    }

    unsigned char method_resp[2] = {};
    if (!RecvExact(s, reinterpret_cast<char*>(method_resp), 2) || method_resp[0] != 0x05) {
        return false;
    }
    if (method_resp[1] == 0xFF) {
        return false;
    }
    if (method_resp[1] == 0x02) {
        std::string user = cfg.proxy_user;
        std::string pass = cfg.proxy_pass;
        if (user.size() > 255 || pass.size() > 255) {
            return false;
        }
        std::vector<unsigned char> auth;
        auth.push_back(0x01);
        auth.push_back(static_cast<unsigned char>(user.size()));
        auth.insert(auth.end(), user.begin(), user.end());
        auth.push_back(static_cast<unsigned char>(pass.size()));
        auth.insert(auth.end(), pass.begin(), pass.end());
        if (!SendAll(s, auth.data(), static_cast<int>(auth.size()))) {
            return false;
        }
        unsigned char auth_resp[2] = {};
        if (!RecvExact(s, reinterpret_cast<char*>(auth_resp), 2) || auth_resp[1] != 0x00) {
            return false;
        }
    }

    std::vector<unsigned char> req;
    req.push_back(0x05);
    req.push_back(0x01);
    req.push_back(0x00);
    in_addr ipv4{};
    if (InetPtonA(AF_INET, host.c_str(), &ipv4) == 1) {
        req.push_back(0x01);
        const auto* b = reinterpret_cast<const unsigned char*>(&ipv4.S_un.S_addr);
        req.insert(req.end(), b, b + 4);
    } else {
        in6_addr ipv6{};
        if (InetPtonA(AF_INET6, host.c_str(), &ipv6) == 1) {
            req.push_back(0x04);
            const auto* b = reinterpret_cast<const unsigned char*>(&ipv6);
            req.insert(req.end(), b, b + 16);
        } else {
            if (host.size() > 255) {
                return false;
            }
            req.push_back(0x03);
            req.push_back(static_cast<unsigned char>(host.size()));
            req.insert(req.end(), host.begin(), host.end());
        }
    }
    const uint16_t be_port = htons(port);
    const auto* pb = reinterpret_cast<const unsigned char*>(&be_port);
    req.insert(req.end(), pb, pb + 2);
    if (!SendAll(s, req.data(), static_cast<int>(req.size()))) {
        return false;
    }

    unsigned char header[4] = {};
    if (!RecvExact(s, reinterpret_cast<char*>(header), 4) || header[1] != 0x00) {
        return false;
    }
    int extra = 0;
    switch (header[3]) {
        case 0x01:
            extra = 6;
            break;
        case 0x03: {
            unsigned char len = 0;
            if (!RecvExact(s, reinterpret_cast<char*>(&len), 1)) {
                return false;
            }
            extra = len + 2;
            break;
        }
        case 0x04:
            extra = 18;
            break;
        default:
            return false;
    }
    if (extra > 0) {
        std::vector<char> skip(extra);
        if (!RecvExact(s, skip.data(), extra)) {
            return false;
        }
    }
    return true;
}

bool ProxyConnector::ConnectHttp(SOCKET s, const std::string& host, uint16_t port, const HookConfig& cfg) {
    if (!ConnectToProxy(s, cfg)) {
        return false;
    }
    std::string req = "CONNECT " + host + ":" + std::to_string(port) + " HTTP/1.1\r\nHost: " + host + ":" +
                      std::to_string(port) + "\r\n";
    if (cfg.proxy_user[0] != '\0') {
        std::string creds = std::string(cfg.proxy_user) + ":" + cfg.proxy_pass;
        std::string encoded;
        // minimal base64 for proxy auth
        static const char* tbl =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const unsigned char* in = reinterpret_cast<const unsigned char*>(creds.data());
        size_t len = creds.size();
        for (size_t i = 0; i < len; i += 3) {
            const int b0 = in[i];
            const int b1 = (i + 1 < len) ? in[i + 1] : 0;
            const int b2 = (i + 2 < len) ? in[i + 2] : 0;
            encoded.push_back(tbl[b0 >> 2]);
            encoded.push_back(tbl[((b0 & 3) << 4) | (b1 >> 4)]);
            encoded.push_back((i + 1 < len) ? tbl[((b1 & 15) << 2) | (b2 >> 6)] : '=');
            encoded.push_back((i + 2 < len) ? tbl[b2 & 63] : '=');
        }
        req += "Proxy-Authorization: Basic " + encoded + "\r\n";
    }
    req += "\r\n";
    if (!SendAll(s, req.data(), static_cast<int>(req.size()))) {
        return false;
    }
    char buf[1024] = {};
    const int n = recv(s, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        return false;
    }
    buf[n] = '\0';
    if (strstr(buf, " 407 ") != nullptr) {
        return false;
    }
    if (strstr(buf, " 200 ") == nullptr) {
        return false;
    }
    return true;
}

bool ProxyConnector::ConnectThroughProxy(
    SOCKET client_socket,
    const sockaddr* dest,
    int dest_len,
    const HookConfig& cfg) {
    std::string host;
    uint16_t port = 0;
    if (!ResolveDest(dest, dest_len, host, port)) {
        return false;
    }
    if (cfg.proxy_type == ProxyType::Socks5) {
        return ConnectSocks5(client_socket, host, port, cfg);
    }
    return ConnectHttp(client_socket, host, port, cfg);
}

}  // namespace joyproxy
