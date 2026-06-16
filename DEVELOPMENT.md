# JoyProxy Windows 开发情况

> 最后更新：2026-06-16  
> 当前版本：**0.1.0**（开发中）  
> 仓库：https://github.com/joyproxy/joy-proxy-win

---

## 一、项目概述

JoyProxy Windows 将指定 `.exe` 的 **TCP 出站** 流量转发到用户配置的 HTTP / SOCKS5 代理。

**v0.1 实现说明**：经调研，WFP `ALE_CONNECT_REDIRECT` 需配套内核 callout 驱动才能将连接重定向到本地代理（微软官方架构）。为满足「无自签驱动」约束，**v0.1 采用 Winsock Hook 注入**（`JoyProxyHook.dll` + `JoyProxyService.exe` 注入器），远期 v2+ 可评估 WinDivert（预签名驱动）或自研 WFP callout。

---

## 二、技术栈

| 类别 | 选型 |
|------|------|
| UI | C# WPF (.NET 8) — 与 WinUI 3 等效桌面体验，CI 构建更简单 |
| 注入 / IPC | C++ (`JoyProxyService.exe`) |
| 网络 Hook | C++ + MinHook (`JoyProxyHook.dll`) |
| 代理 Relay / 测试 | Rust (`joyproxy_relay.dll`) |
| 构建 | Cargo + CMake + MSBuild / `build.ps1` |
| CI | GitHub Actions |

---

## 三、架构

```
JoyProxy.exe (WPF)
    │ UAC 启动 + Named Pipe
JoyProxyService.exe (Admin)
    │ 共享内存配置 + CreateRemoteThread(LoadLibrary)
JoyProxyHook.dll → hook connect/WSAConnect → SOCKS5/HTTP CONNECT
joyproxy_relay.dll → UI 侧代理连通性测试
```

---

## 四、本地构建

### 依赖

- Visual Studio 2022（Desktop development with C++）
- CMake 3.20+
- Rust stable (MSVC)
- .NET 8 SDK

### 命令

```powershell
cd C:\Users\syf0123\joy-proxy-win
.\build.ps1 -Configuration Release
```

产物目录：`artifacts\Release\`

| 文件 | 说明 |
|------|------|
| `JoyProxy.exe` | 主界面 |
| `JoyProxyService.exe` | 管理员 Helper |
| `JoyProxyHook.dll` | Winsock Hook |
| `joyproxy_relay.dll` | 代理测试 |

---

## 五、使用说明

1. 以普通用户运行 `JoyProxy.exe`
2. 填写 SOCKS5/HTTP 代理，选择目标 exe（或从运行中进程选择）
3. 点击「连接代理」→ 允许 UAC
4. **已运行的目标 exe 会被注入**；之后新启动的同路径进程也会自动注入
5. 断开时停止注入监视（已注入进程内的 Hook 仍保留直到该进程退出）

---

## 六、已知限制（v0.1）

| 项目 | 说明 |
|------|------|
| 引擎 | Winsock Hook，非 WFP Redirect |
| 协议 | 仅 TCP（connect/WSAConnect） |
| 单 exe | 单目标路径白名单 |
| 注入 | 需管理员；部分带反注入保护的程序可能失败 |
| 断开 | 无法从已注入进程卸载 Hook，需重启目标程序 |

---

## 七、路线图

- [x] v0.1 概要 / 详细设计
- [x] v0.1 Winsock Hook + SOCKS5/HTTP + WPF UI
- [ ] v0.2 多 exe、连接日志、DPAPI 密码
- [ ] v1.x WinDivert 透明模式（预签名驱动）
- [ ] v2+ WFP 规则引擎 / sing-box 核心

---

## 八、许可证

GPLv3 — 见 [LICENSE](LICENSE)
