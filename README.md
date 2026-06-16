# JoyProxy for Windows

Windows 按进程 TCP 代理工具：向指定 `.exe` 注入 Winsock Hook，将 TCP 连接转发到 HTTP / SOCKS5 代理。**v0.1 无需自签驱动**。

> Android 版：[joy-proxy-android](https://github.com/joyproxy/joy-proxy-android)

## 功能（v0.1）

- HTTP / SOCKS5 代理配置、历史记录（20 条）、连通性测试
- 选择目标 `.exe`（浏览或从运行中进程选择）
- 连接时 UAC 提升，自动注入已运行 / 新启动的目标进程
- 断开停止监视（已注入进程需重启方可完全移除 Hook）

## 快速开始

```powershell
git clone https://github.com/joyproxy/joy-proxy-win.git
cd joy-proxy-win
.\build.ps1 -Configuration Release
.\artifacts\Release\JoyProxy.exe
```

## 文档

- [DESIGN.md](DESIGN.md) — 概要设计
- [docs/DETAILED_DESIGN.md](docs/DETAILED_DESIGN.md) — 详细设计
- [DEVELOPMENT.md](DEVELOPMENT.md) — 开发说明与 v0.1 引擎说明

## 技术栈

| 组件 | 技术 |
|------|------|
| UI | C# WPF (.NET 8) |
| Service / Injector | C++ |
| Hook | C++ + MinHook |
| Relay / 代理测试 | Rust |

## 许可证

GPLv3 — 见 [LICENSE](LICENSE)
