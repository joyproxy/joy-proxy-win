# JoyProxy for Windows

Windows 按进程 TCP 代理工具，通过 **WFP（Windows Filtering Platform）Connect Redirect** 将指定 exe 的出站 TCP 流量转发到 HTTP / SOCKS5 代理，无需自签内核驱动。

> 当前阶段：**概要设计**（见 [DESIGN.md](DESIGN.md)），待审核后进入详细设计与开发。

## 与 Android 版的关系

| 项目 | 说明 |
|------|------|
| [joy-proxy-android](https://github.com/joyproxy/joy-proxy-android) | 已发布，VpnService + sing-box |
| **joy-proxy-win**（本仓库） | Windows 版，WFP + 用户态 Relay |

两端共享产品理念：用户填写 HTTP/SOCKS5 代理地址，按应用粒度转发流量，最终目标接近 Proxifier。

## v1 规划功能

- HTTP / SOCKS5 代理配置（IP 或域名 + 端口，可选认证）
- 选择一个目标 `.exe`，其 TCP 出站经代理转发
- 代理连通性测试
- 连接 / 断开，断开时清理 WFP 规则

## 文档

- [DESIGN.md](DESIGN.md) — 概要设计（架构、WFP 方案、路线图、待审核决策点）

## 许可证

本项目计划与 Android 版统一采用 **GPLv3**（若后续引入 sing-box 则必须）。详见 [LICENSE](LICENSE)。
