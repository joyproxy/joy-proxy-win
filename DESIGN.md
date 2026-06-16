# JoyProxy Windows 概要设计

> 文档版本：v0.1（概要设计，待审核）  
> 日期：2026-06-16  
> 关联项目：[joy-proxy-android](https://github.com/joyproxy/joy-proxy-android)

---

## 一、背景与目标

### 1.1 背景

JoyProxy Android 已通过 `VpnService` + sing-box `libbox` 实现 HTTP/SOCKS5 代理转发，支持全局/分应用路由，无需 Root。Windows 版需要在**不安装自签内核驱动、无需驱动签名**的前提下，实现类似 **Proxifier** 的「按进程代理 TCP 流量」能力。

### 1.2 产品定位

| 维度 | Android 版 | Windows 版（本设计） |
|------|-----------|---------------------|
| 流量拦截 | TUN 虚拟网卡（VpnService） | WFP 连接重定向（Connect Redirect） |
| 代理协议 | HTTP / SOCKS5 | HTTP / SOCKS5（v1 同 Android） |
| 路由粒度 | 全局 / 白名单 / 黑名单 | v1：单 exe 白名单；后续扩展 |
| 权限 | VPN 授权（用户确认） | 管理员权限（一次性 UAC，见 §5） |
| 代理核心 | sing-box libbox | v1：自研轻量 TCP 中继；后续可引入 sing-box |

### 1.3 版本目标

**v1（MVP）**

- 用户输入 HTTP 或 SOCKS5 代理（IP/域名 + 端口，可选用户名密码）
- 用户选择一个 `.exe` 进程/应用
- 该 exe 的 **出站 TCP** 连接经上游代理转发（实现「换 IP」）
- 提供基础 UI：配置代理、选择目标程序、连接/断开、连通性测试
- 连接状态与错误提示

**远期（Proxifier 级）**

- 多进程规则（白名单/黑名单/域名规则/端口规则）
- UDP 转发（游戏、QUIC 等）
- DNS 策略（Fake-IP / DoH / 直连解析）
- 规则导入导出、连接日志、流量统计
- 开机自启、配置热重载

---

## 二、技术方案选型

### 2.1 候选方案对比

| 方案 | 原理 | 驱动/签名 | 按进程 | TCP | UDP | 推荐 |
|------|------|-----------|--------|-----|-----|------|
| **A. WFP Connect Redirect** | 内核 WFP 框架，用户态注册过滤器和重定向 | 无需自研驱动 | ✅ App ID | ✅ | ⚠️ 需额外层 | **v1 首选** |
| B. WinDivert / 自研驱动 | 内核驱动截获包 | 需 EV 签名或测试模式 | ✅ | ✅ | ✅ | ❌ 违背约束 |
| C. Winsock LSP / SPI | 注入 Winsock 栈 | 无驱动，已废弃 | ✅ | ✅ | ✅ | ❌ 兼容性差 |
| D. DLL 注入 Hook `connect()` | 用户态 Hook | 无驱动 | ✅ | ✅ | ❌ | ⚠️ 反作弊/稳定性风险 |
| E. 环境变量 `HTTP_PROXY` | 应用自行读代理 | 无 | ❌ 仅支持代理感知的 app | 部分 | ❌ | ❌ 非通用 |
| F. 系统代理（IE/WinHTTP 设置） | 修改系统代理 | 无 | ❌ 全局 | 部分 | ❌ | ❌ 非按进程 |
| G. TUN + sing-box（类似 Android） | 虚拟网卡全局路由 | 需 wintun 等 | ✅ 可配合路由 | ✅ | ✅ | ⚠️ v2+ 备选 |

**结论：v1 采用方案 A（WFP Connect Redirect）**，理由：

1. **微软官方支持**的用户态 API（`fwpmu.lib` / `fwpsk.lib`），Proxifier、Clash TUN 的 Windows 生态均围绕 WFP 或 TUN 展开。
2. **无需自研内核驱动**，不碰驱动签名与 WHQL。
3. 可在 **ALE（Application Layer Enforcement）层** 用 `FWPM_CONDITION_ALE_APP_ID` 精确匹配目标 exe。
4. 重定向到本地用户态代理后，上游 HTTP/SOCKS5 逻辑完全在用户态实现。

### 2.2 WFP 工作原理（v1 数据路径）

```
目标 exe 发起 TCP connect(dest_ip, dest_port)
        │
        ▼
┌───────────────────────────────────────┐
│  WFP 过滤器（ALE_CONNECT_REDIRECT）    │
│  条件：AppId == 目标 exe                 │
│  动作：重定向到 127.0.0.1:local_port     │
│  附带：原始目标地址（redirect context）   │
└─────────────────┬─────────────────────┘
                  │
                  ▼
┌───────────────────────────────────────┐
│  JoyProxy Redirect Service             │
│  本地 TCP 监听（127.0.0.1:local_port）  │
│  读取原始 dest + 进程信息               │
└─────────────────┬─────────────────────┘
                  │
                  ▼
┌───────────────────────────────────────┐
│  Upstream Relay                        │
│  SOCKS5 CONNECT / HTTP CONNECT 隧道    │
│  双向转发 TCP 字节流                    │
└─────────────────┬─────────────────────┘
                  │
                  ▼
           用户配置的代理服务器
                  │
                  ▼
              目标互联网地址
```

### 2.3 v1 明确不做

- UDP 转发
- 内核驱动 / WinDivert
- VMess / Shadowsocks 等协议
- 域名级规则引擎（仅进程级）
- 系统级 TUN 全局 VPN 模式

---

## 三、系统架构

### 3.1 总体架构

```
┌─────────────────────────────────────────────────────────┐
│                    JoyProxy.exe（UI）                     │
│  - 代理配置 / 历史记录 / 连通性测试                        │
│  - 进程选择器（exe 路径或运行中进程）                      │
│  - 连接状态展示                                          │
│  - 通过 IPC 控制 Service                                 │
└────────────────────────┬────────────────────────────────┘
                         │ Named Pipe / gRPC localhost
┌────────────────────────▼────────────────────────────────┐
│              JoyProxyService.exe（Elevated）             │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────┐ │
│  │ WfpEngine   │  │ RuleManager  │  │ ProcessWatcher  │ │
│  │ 过滤器注册   │  │ AppId/规则   │  │ 可选：动态 PID   │ │
│  └──────┬──────┘  └──────────────┘  └─────────────────┘ │
│         │                                                │
│  ┌──────▼──────────────────────────────────────────┐   │
│  │ RedirectServer（本地 TCP 127.0.0.1:N）           │   │
│  └──────┬──────────────────────────────────────────┘   │
│         │                                                │
│  ┌──────▼──────────────────────────────────────────┐   │
│  │ RelayPool（SOCKS5 / HTTP CONNECT 上游）          │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 3.2 进程模型

| 组件 | 运行权限 | 说明 |
|------|---------|------|
| `JoyProxy.exe` | 标准用户 | 日常 UI，无需常驻管理员 |
| `JoyProxyService.exe` | **管理员（UAC 提升）** | 注册/卸载 WFP 过滤器，仅在「连接代理」时启动 |
| Windows Service（可选，v2+） | LocalSystem / 自定义 | 开机自启、长期驻留时再考虑 |

**v1 策略**：采用 **按需提升的 Helper 进程**，而非安装 Windows Service。用户点击「连接」时 UAC 一次，断开时释放 WFP 资源并退出 Helper。这样：

- 不长期占用管理员权限
- 无需安装服务（减少「系统权限」感知）
- 实现简单，适合 MVP

### 3.3 模块职责

| 模块 | 语言建议 | 职责 |
|------|---------|------|
| **UI** | C# WinUI 3 或 WPF | 配置、进程选择、状态、调用 IPC |
| **Service / Helper** | C++ 或 Rust | WFP 引擎、过滤器生命周期、Redirect 监听 |
| **Relay** | Rust 或 Go | SOCKS5/HTTP 上游连接、连接池、超时 |
| **Common** | 共享 | 配置序列化、IPC 协议、日志 |

**语言选型倾向（待详细设计确认）**：

- **WFP 层**：C++（微软示例均为 C/C++，文档最全）或 Rust（`windows` crate + 安全封装）
- **Relay 层**：Rust（性能 + 单二进制）或 Go（与 Android sing-box 生态一致）
- **UI 层**：C# WinUI 3（现代、MSIX 友好）或 WPF（成熟稳定）

---

## 四、核心模块设计要点

### 4.1 WFP 过滤器管理（WfpEngine）

**关键 API 流程**：

1. `FwpmEngineOpen0` — 打开 WFP 引擎（需管理员）
2. `FwpmTransactionBegin0` — 事务性添加/删除，保证一致性
3. 创建 **Provider** + **SubLayer**（JoyProxy 专属，避免污染系统规则）
4. 在 `FWPM_LAYER_ALE_CONNECT_REDIRECT_V4/V6` 添加 Filter：
   - 条件：`FWPM_CONDITION_ALE_APP_ID` == 目标 exe 的 AppId 二进制
   - 动作：`FWP_ACTION_CALLOUT` 或内置 **Connect Redirect**（使用 `%SystemRoot%\System32\fwpuclnt.dll` 提供的内置 callout）
5. 注册 **Callout**（若使用自定义 callout 需驱动；v1 优先用 **内置 redirect** + 用户态 `RedirectHandle`）

> **注意**：Connect Redirect 的完整实现需配合 `FwpsRedirectHandleCreate0` 与 `connect` 层的 `FWPS_CLASSIFY_OUT_FLAG_ABSORB` 处理。详细 API 调用链在《详细设计》中展开。

**AppId 获取**：

```text
路径 C:\Games\foo.exe
  → FwpmGetAppIdFromFileName0
  → 得到 FWPM_BYTE_BLOB appId
  → 写入过滤器条件
```

**清理**：断开连接时必须删除 SubLayer/Filter/Provider，防止残留导致网络异常（Proxifier 卸载不干净是常见投诉点，需重点处理）。

### 4.2 本地 Redirect Server

- 绑定 `127.0.0.1:动态端口`（连接时分配，避免冲突）
- 每个被重定向的连接，WFP 会在 classify 时提供 **原始目标地址**
- Redirect Server 接受连接后：
  1. 通过 `GetExtendedTcpTable` / redirect metadata 获取 original dest
  2. 从 RelayPool 获取上游代理连接
  3. 建立 SOCKS5 CONNECT 或 HTTP CONNECT 到 original dest
  4. 双向 `copy` 转发

### 4.3 上游 Relay（HTTP / SOCKS5）

与 Android 版对齐的最小实现：

| 协议 | 握手 |
|------|------|
| SOCKS5 | 无认证 / 用户名密码 → CONNECT dest:port |
| HTTP | CONNECT dest:port HTTP/1.1 + Proxy-Authorization |

**v1 不做**：TLS 解密、HTTP 非 CONNECT 方法代理、链式代理。

**连通性测试**（UI 直连，不经 WFP）：

- TCP 连接代理服务器
- SOCKS5：握手 + 可选认证
- HTTP：对 `example.com:80` 发 CONNECT，期望 200

### 4.4 进程选择（v1）

两种方式（UI 均提供）：

1. **选择 exe 文件路径**（推荐）：稳定，AppId 固定，不依赖进程是否运行
2. **从运行中进程列表选择**：通过 Toolhelp32 / WMI 枚举，反查 exe 路径

v1 规则：**单 exe 白名单**，仅该程序 TCP 出站走代理，其余程序不受影响。

### 4.5 配置与持久化

对齐 Android 版数据结构：

```json
{
  "proxy": {
    "type": "socks5 | http",
    "host": "1.2.3.4",
    "port": 1080,
    "username": "",
    "password": ""
  },
  "target": {
    "exePath": "C:\\Program Files\\App\\app.exe"
  },
  "relay": {
    "localPort": 0
  }
}
```

存储位置：`%AppData%\JoyProxy\settings.json`  
代理历史：最多 20 条（与 Android 一致）。

---

## 五、权限与安全模型

### 5.1 所需权限

| 操作 | 权限 | 可否避免 |
|------|------|---------|
| 注册 WFP 过滤器 | Administrator | ❌ Windows 硬性要求 |
| 读取进程列表 | 标准用户（部分系统进程需提升） | 基本可 |
| 本地 127.0.0.1 监听 | 标准用户 | ✅ |
| 连接外部代理 | 标准用户 | ✅ |

**结论**：v1 必须在「连接代理」时 **UAC 提升一次**。这是 WFP 的硬性约束，Proxifier 同样要求管理员权限。我们在 UI 中明确说明原因，并在断开时 **立即退出 elevated helper**，不长期保持管理员会话。

### 5.2 安全考量

- 代理密码仅存本地 `%AppData%`，可选 DPAPI 加密（v1.1）
- IPC 仅接受本地 Named Pipe，校验调用方 SID
- WFP SubLayer 使用唯一 GUID，卸载时完整清理
- 开源可审计，避免闭源代理工具的信任问题

### 5.3 不做的事（降低权限攻击面）

- 不安装永久内核驱动
- 不注入第三方进程 DLL
- 不修改系统全局代理设置（v1）
- 不 Hook 系统 DLL

---

## 六、技术栈建议（概要）

| 类别 | v1 建议 | 说明 |
|------|--------|------|
| UI | **C# + WinUI 3** | 现代 Windows 原生，便于 MSIX 分发 |
| WFP Engine | **C++** 静态库 | 直接复用 Microsoft WFP 示例代码 |
| Relay | **Rust** 静态库或独立 crate | 内存安全、async IO（tokio） |
| IPC | Named Pipe + JSON / protobuf | 简单可靠 |
| 构建 | CMake + Cargo + MSBuild | CI: GitHub Actions (windows-latest) |
| 代理核心（v2+） | sing-box（Go） | 与 Android 统一，支持 DNS/UDP/规则 |

**仓库结构（规划）**：

```text
joy-proxy-win/
├── DESIGN.md              # 本文档
├── docs/
│   └── DETAILED_DESIGN.md # 详细设计（审核通过后编写）
├── src/
│   ├── ui/                # WinUI 3 应用
│   ├── service/           # WFP Helper（C++）
│   ├── relay/             # SOCKS5/HTTP 中继（Rust）
│   └── common/            # 共享协议与类型
├── .github/workflows/
│   └── build.yml
├── LICENSE                # GPLv3（若引入 sing-box 则必须；v1 自研可 MIT，建议统一 GPLv3）
└── README.md
```

---

## 七、与 Proxifier 的能力差距与路线图

```text
v1.0  MVP
  └─ 单 exe + TCP + HTTP/SOCKS5 + WFP Redirect

v1.x  体验完善
  └─ 多 exe 白名单、代理历史、DPAPI 加密、连接日志

v2.0  规则引擎
  └─ 域名/port 规则、黑名单模式、配置文件导入导出

v2.x  协议扩展
  └─ UDP（WFP DATAGRAM 或 TUN 模式）、DNS 策略

v3.0  Proxifier 级
  └─ 全局/分应用/规则链、sing-box 核心、Windows Service 可选
```

### Proxifier 功能对照（远期）

| Proxifier 功能 | v1 | 远期 |
|---------------|-----|------|
| 按进程规则 | 单 exe | 多规则 |
| TCP | ✅ | ✅ |
| UDP | ❌ | ✅ |
| SOCKS5 / HTTP | ✅ | ✅ |
| 域名规则 | ❌ | ✅ |
| 直连/阻断动作 | ❌ | ✅ |
| 连接日志 | ❌ | ✅ |
| 无需驱动签名 | ✅ | ✅ |

---

## 八、风险与应对

| 风险 | 影响 | 应对 |
|------|------|------|
| WFP API 复杂，Redirect 元数据获取失败 | 无法得知原始目标 | 参考 Microsoft `Windows Filtering Platform Sample`；充分集成测试 |
| 部分 exe 使用硬编码 IP / 自定义协议 | 代理不生效 | v1 文档说明「仅标准 TCP connect」；远期 TUN 模式兜底 |
| 反作弊游戏检测 WFP / 管理员 | 被 ban 风险 | 免责声明；不针对游戏场景做保证 |
| 过滤器卸载残留 | 用户网络异常 | `RAII` + 事务 + 进程异常退出时的 watchdog 清理 |
| IPv6 目标 | 连接失败 | v1 同步支持 V4/V6 Redirect 层 |
| 代理本身仅支持 IPv4 | 部分场景失败 | UI 提示；Relay 支持 IPv4-mapped-IPv6 |

---

## 九、测试策略（概要）

| 类型 | 内容 |
|------|------|
| 单元测试 | SOCKS5/HTTP 握手、AppId 解析、配置序列化 |
| 集成测试 | WFP 注册/卸载、Redirect 端到端（curl.exe 指定路径） |
| 手动测试 | 浏览器 exe、Steam、自定义 tcp 客户端 |
| 回归 | 断开代理后 `netsh wfp show filters` 无 JoyProxy 残留 |

---

## 十、待审核决策点

请审核时重点确认以下问题，审核通过后我将输出《详细设计》（`docs/DETAILED_DESIGN.md`）并开始编码：

1. **UI 框架**：WinUI 3 vs WPF？（建议 WinUI 3）
2. **WFP 实现语言**：C++ vs Rust？（建议 WFP 用 C++，Relay 用 Rust）
3. **Helper 模型**：按需 UAC 提升 vs 安装 Windows Service？（建议 v1 按需提升）
4. **v1 进程选择**：仅 exe 路径 vs 支持运行中进程？（建议两者都支持）
5. **许可证**：统一 GPLv3（与 Android / 未来 sing-box 一致）是否 OK？
6. **v1 是否排除本工具自身 exe 的流量**（防止环路）—— 建议默认排除 JoyProxy 相关进程
7. **是否接受「连接时必须 UAC」**—— WFP 无法绕过，需在 UI 明确告知

---

## 十一、参考资料

- [Windows Filtering Platform](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-start-page)
- [Using Classify Options](https://learn.microsoft.com/en-us/windows/win32/fwp/using-classify-options)
- [FWPM_LAYER_ALE_CONNECT_REDIRECT_V4](https://learn.microsoft.com/en-us/windows/win32/fwp/management-filtering-layer-identifiers-)
- Proxifier 产品行为参考（按进程 TCP/UDP 代理）
- JoyProxy Android：`DEVELOPMENT.md`（代理配置、DNS、分应用路由经验）

---

*本文档为概要设计，审核通过后进入详细设计与实现阶段。*
