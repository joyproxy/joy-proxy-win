# JoyProxy Windows 详细设计

> 文档版本：v1.0  
> 日期：2026-06-16  
> 状态：**已确认决策，可进入编码**  
> 概要设计：[DESIGN.md](../DESIGN.md)

---

## 一、已确认决策

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | UI 框架 | **WinUI 3**（C#，最低 Win10 1809） |
| 2 | 语言分工 | **C++**（WFP + RedirectServer）+ **Rust**（Relay）+ **C#**（UI） |
| 3 | Helper 模型 | **v1 按需 UAC 提升**，断开即退出；v3 可选 Windows Service |
| 4 | 进程选择 | **exe 路径为主** + **运行中进程列表辅助**（最终持久化路径） |
| 5 | 许可证 | **GPLv3**（与 Android 一致） |
| 6 | 排除自身 | **默认强制排除** JoyProxy 相关进程 |
| 7 | UAC | **连接时必须 UAC**，UI 透明说明，断开释放 |

---

## 二、系统架构

### 2.1 组件图

```
┌─────────────────────────────────────────────────────────────────┐
│ JoyProxy.exe（标准用户，WinUI 3）                                 │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌───────────────┐ │
│  │ ProxyForm  │ │ ProcessPick│ │ StatusPanel│ │ ProxyTester   │ │
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘ └───────┬───────┘ │
│        └──────────────┴──────────────┴────────────────┘         │
│                              │                                   │
│                    SettingsStore / ProxyHistory                  │
│                              │                                   │
│                    ServiceLauncher（ShellExecute runas）         │
└──────────────────────────────┼──────────────────────────────────┘
                               │ Named Pipe: \\.\pipe\joyproxy-v1
                               │ JSON 行协议（LF 分隔）
┌──────────────────────────────▼──────────────────────────────────┐
│ JoyProxyService.exe（管理员，按需启动）                           │
│  ┌─────────────┐  ┌──────────────────┐  ┌─────────────────────┐ │
│  │ IpcServer   │  │ WfpEngine        │  │ RedirectTcpServer   │ │
│  │             │──│ FilterManager    │──│ 127.0.0.1:dyn_port  │ │
│  └─────────────┘  │ AppIdResolver    │  └──────────┬──────────┘ │
│                   │ RedirectHandle   │             │            │
│                   └──────────────────┘             │            │
│  ┌─────────────────────────────────────────────────▼──────────┐ │
│  │ RelayBridge（C++ FFI → Rust joyproxy_relay）                │ │
│  └────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
                    用户 HTTP / SOCKS5 代理
```

### 2.2 可执行文件

| 文件 | 权限 | 生命周期 |
|------|------|---------|
| `JoyProxy.exe` | 标准用户 | 用户打开 App 期间 |
| `JoyProxyService.exe` | Administrator（UAC） | 「连接」→「断开」或 UI 退出 |
| `joyproxy_relay.dll` | 随 Service 加载 | 同上 |

安装方式：v1 **绿色版**（zip / MSIX 均可），两 exe 同目录，无注册表服务项。

### 2.3 固定 GUID（代码内常量）

所有 WFP 对象使用稳定 GUID，便于调试与清理：

```cpp
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890} — Provider
// {B2C3D4E5-F6A7-8901-BCDE-F12345678901} — SubLayer
// {C3D4E5F6-A7B8-9012-CDEF-123456789012} — Connect Redirect V4 Filter
// {D4E5F6A7-B8C9-0123-DEF0-234567890123} — Connect Redirect V6 Filter
```

---

## 三、WFP 引擎设计（C++）

### 3.1 初始化流程

```text
WfpEngine::Start(config)
  1. FwpmEngineOpen0(nullptr, RPC_C_AUTHN_DEFAULT, nullptr, &engineHandle)
  2. FwpmTransactionBegin0(engineHandle, 0)
  3. FwpsRedirectHandleCreate0(&redirectHandle)          // 用户态重定向句柄
  4. EnsureProvider()   → FwpmProviderAdd0
  5. EnsureSubLayer()   → FwpmSubLayerAdd0（weight=0x8000，仅 JoyProxy 规则）
  6. ResolveAppId(exePath) → FwpmGetAppIdFromFileName0
  7. AddConnectRedirectFilters(appId, redirectHandle)      // V4 + V6
  8. FwpmTransactionCommit0
  9. RedirectTcpServer::Listen(127.0.0.1, dynamicPort)
 10. 将 redirectPort 写入 filter 关联 / 本地状态
```

### 3.2 Connect Redirect 过滤器

**层**：

- `FWPM_LAYER_ALE_CONNECT_REDIRECT_V4`
- `FWPM_LAYER_ALE_CONNECT_REDIRECT_V6`

**条件**（AND）：

| 字段 | 运算符 | 值 |
|------|--------|-----|
| `FWPM_CONDITION_ALE_APP_ID` | `FWP_MATCH_EQUAL` | 目标 exe 的 AppId blob |
| `FWPM_CONDITION_IP_PROTOCOL` | `FWP_MATCH_EQUAL` | `TCP (6)` |

**动作**：

- 使用内置 Connect Redirect 机制 + `redirectHandle`
- 重定向目标：`127.0.0.1:redirectPort`（IPv4）/ `[::1]:redirectPort`（IPv6）
- 原始目标地址由 WFP 在 redirect record 中保留

**不代理的流量**：未匹配 AppId 的连接 **Permit**，不影响其他程序。

### 3.3 卸载流程（关键路径）

```text
WfpEngine::Stop()
  1. RedirectTcpServer::Stop()        // 先停监听，拒绝新连接
  2. RelayBridge::Drain(timeout=5s)   // 等待现有 relay 结束
  3. FwpmTransactionBegin0
  4. DeleteFilters → DeleteSubLayer → DeleteProvider
  5. FwpmTransactionCommit0
  6. FwpsRedirectHandleDestroy0
  7. FwpmEngineClose0
```

**异常退出兜底**：

- Service 捕获 `CTRL_CLOSE_EVENT` / `WM_ENDSESSION`，执行 `Stop()`
- UI 启动时调用 `probe` IPC：若发现残留 WFP 对象（Provider 存在但 Service 不在），提示「清理残留规则」并执行 `ForceCleanup()`

### 3.4 RedirectTcpServer

每个被接受的本地 socket，需取得**原始目标**：

1. **主路径**：`GetAcceptExSockaddrs` + WFP redirect context / `SO_ORIGINAL_DST` 类信息  
   - 实现时对照 Microsoft 「Redirecting TCP connections」示例，使用 `FWPS_CONNECT_REQUEST0` 相关 metadata
2. **备用路径**：`GetExtendedTcpTable` 对比本地端口映射（仅 debug / fallback）

取得 `(originalIp, originalPort)` 后调用 Relay：

```cpp
RelayBridge::Relay(localSocket, originalDest, proxyConfig);
```

### 3.5 AppId 解析

```cpp
struct AppIdResolver {
    static std::vector<uint8_t> FromExePath(const std::wstring& path);
    static std::wstring NormalizePath(const std::wstring& path); // 全路径、大小写不敏感
};
```

- 用户通过进程列表选中时：`QueryFullProcessImageNameW` → 保存规范化路径
- 过滤器始终按 **exe 路径 AppId**，不按 PID（PID 仅用于 UI 展示）

---

## 四、Relay 模块设计（Rust）

### 4.1 Crate 结构

```text
src/relay/
├── Cargo.toml
├── src/
│   ├── lib.rs           # C FFI 导出
│   ├── config.rs        # ProxyConfig
│   ├── socks5.rs        # SOCKS5 客户端
│   ├── http_connect.rs  # HTTP CONNECT 客户端
│   ├── relay.rs         # 双向 copy
│   └── error.rs
└── tests/
    ├── socks5_handshake.rs
    └── http_connect.rs
```

### 4.2 公开 C FFI

```rust
/// 初始化 runtime（Service 启动时调用一次）
#[no_mangle]
pub extern "C" fn joyproxy_relay_init() -> i32;

/// 阻塞式 relay，直到任一方向关闭或出错
#[no_mangle]
pub extern "C" fn joyproxy_relay_tcp(
    local_socket: usize,          // SOCKET cast
    dest_host: *const c_char,     // UTF-8
    dest_port: u16,
    proxy_type: u8,               // 0=socks5, 1=http
    proxy_host: *const c_char,
    proxy_port: u16,
    proxy_user: *const c_char,    // 可空
    proxy_pass: *const c_char,    // 可空
) -> i32;

/// 测试代理连通性（不经过 WFP）
#[no_mangle]
pub extern "C" fn joyproxy_relay_test_proxy(...) -> i32;

#[no_mangle]
pub extern "C" fn joyproxy_relay_shutdown();
```

### 4.3 SOCKS5 流程

```text
1. TCP connect → proxy_host:proxy_port
2. Client greeting（无 auth / username-password）
3. Server method selection
4. [若需认证] username/password subnegotiation
5. CONNECT dest_host:dest_port
6. Reply OK → 双向 relay（tokio::io::copy_bidirectional 或 blocking std::thread）
```

### 4.4 HTTP CONNECT 流程

```text
CONNECT dest_host:dest_port HTTP/1.1\r\n
Host: dest_host:dest_port\r\n
[Proxy-Authorization: Basic ...]\r\n
\r\n
→ 期望 HTTP/1.x 200
→ 双向 relay raw TCP
```

### 4.5 线程模型

- v1：**每连接一个 thread pool 任务**（实现简单，Windows socket 阻塞 IO）
- 连接上限：默认 256 并发（可配置），超出拒绝并记日志
- v1.1 可改 tokio runtime 统一调度

---

## 五、IPC 协议（Named Pipe）

### 5.1 管道名

```text
\\.\pipe\joyproxy-v1
```

- 仅 **JoyProxyService** 创建（管理员）
- **JoyProxy.exe** 连接
- SDDL：仅当前用户 SID + LocalSystem 可读写（防止其他用户劫持）

### 5.2 消息格式

**JSON Lines**：一行一条消息，UTF-8，`\n` 结尾。

#### UI → Service

```json
{"type":"ping","id":1}
{"type":"start","id":2,"payload":{...SessionConfig...}}
{"type":"stop","id":3}
{"type":"status","id":4}
{"type":"force_cleanup","id":5}
```

#### Service → UI

```json
{"type":"pong","id":1}
{"type":"started","id":2,"payload":{"redirectPort":45678}}
{"type":"stopped","id":3}
{"type":"error","id":2,"code":"WFP_FILTER_FAILED","message":"..."}
{"type":"status","id":4,"payload":{"state":"running","activeConnections":3}}
{"type":"event","event":"connection","payload":{"dest":"93.184.216.34:443"}}
```

### 5.3 SessionConfig Schema

```json
{
  "version": 1,
  "proxy": {
    "type": "socks5",
    "host": "1.2.3.4",
    "port": 1080,
    "username": "",
    "password": ""
  },
  "target": {
    "exePath": "C:\\Program Files\\App\\app.exe"
  },
  "options": {
    "maxConnections": 256,
    "excludeSelf": true
  }
}
```

### 5.4 Service 启动方式

UI 通过 **UAC 提升** 启动 Service：

```csharp
Process.Start(new ProcessStartInfo {
    FileName = "JoyProxyService.exe",
    Arguments = $"--pipe joyproxy-v1 --parent-pid {Process.GetCurrentProcess().Id}",
    Verb = "runas",
    UseShellExecute = true
});
```

- `--parent-pid`：父进程退出时 Service 自动 stop（Watchdog thread `WaitForSingleObject`）
- 单实例 Mutex：`Global\JoyProxyService_v1`

---

## 六、UI 设计（WinUI 3）

### 6.1 页面结构

| 页面/控件 | 功能 |
|-----------|------|
| **HomePage** | 代理类型、地址、端口、用户名、密码 |
| **ProxyHistoryCombo** | 最近 20 条，下拉快选（对齐 Android） |
| **ProcessPicker** | 「浏览 exe」+「从运行中选择」 |
| **ConnectButton** | 未连接：启动 UAC + start IPC；已连接：stop |
| **StatusBar** | 状态：未连接 / 连接中 / 已连接 / 失败原因 |
| **TestProxyButton** | 调用 `joyproxy_relay_test_proxy`（不经 WFP） |
| **UacNotice** | 首次连接前说明管理员权限用途 |

### 6.2 状态机

```text
Disconnected → Connecting → Connected → Disconnecting → Disconnected
                  ↓              ↓
                Error          Error
```

- `Connecting`：等待 UAC、Service 启动、IPC start 响应
- `Connected`：显示目标 exe、代理摘要、活跃连接数（可选 v1.1）
- 修改代理或目标 exe 后，需 **断开重连**（与 Android 一致）

### 6.3 持久化

路径：`%AppData%\JoyProxy\settings.json`

```json
{
  "lastProxy": { "type": "socks5", "host": "...", "port": 1080, "username": "", "password": "" },
  "lastTargetExe": "C:\\...\\app.exe",
  "proxyHistory": [ /* 最多 20 条 SavedProxy */ ]
}
```

密码 v1 明文存储；v1.1 用 **DPAPI**（`ProtectedData.Protect`）加密。

### 6.4 进程选择器

**Browse exe**：

- `FileOpenPicker`，Filter `.exe`

**Running processes**：

- `CreateToolhelp32Snapshot` / WMI `Win32_Process`
- 显示：`进程名 (PID) — 完整路径`
- 选中 → 保存 `ExecutablePath`
- 过滤：System、Idle、JoyProxy 自身

---

## 七、配置与类型（Common）

### 7.1 C# / JSON 共享模型

```csharp
public enum ProxyType { Socks5, Http }

public record ProxyEndpoint(
    ProxyType Type,
    string Host,
    int Port,
    string? Username,
    string? Password
);

public record TargetApp(string ExePath);

public record SessionConfig(
    int Version,
    ProxyEndpoint Proxy,
    TargetApp Target,
    SessionOptions Options
);
```

C++ / Rust 侧各自实现等价 struct，IPC 传 JSON。

### 7.2 输入校验

| 字段 | 规则 |
|------|------|
| host | 非空；域名或 IPv4/IPv6 |
| port | 1–65535 |
| exePath | 存在、扩展名 `.exe`、可 `FwpmGetAppIdFromFileName0` |
| proxy type | `socks5` \| `http` |

粘贴 `ip:port` / `[ipv6]:port` 自动拆分（对齐 Android v1.0.8）。

---

## 八、错误码

| Code | 说明 | UI 处理 |
|------|------|---------|
| `UAC_DENIED` | 用户拒绝 UAC | 提示需管理员授权 |
| `SERVICE_START_FAILED` | Helper 未启动 | 检查杀毒/权限 |
| `IPC_TIMEOUT` | 管道无响应 | 重试 / 重启 Service |
| `WFP_OPEN_FAILED` | 非管理员 | 同上 |
| `WFP_FILTER_FAILED` | 规则添加失败 | 显示 HRESULT |
| `APPID_RESOLVE_FAILED` | exe 路径无效 | 重新选择 |
| `RELAY_PROXY_AUTH_FAILED` | 代理认证失败 | 检查账号密码 |
| `RELAY_PROXY_CONNECT_FAILED` | 无法连代理 | 检查地址 |
| `RELAY_DEST_FAILED` | 代理无法连目标 | 显示 dest |
| `ALREADY_RUNNING` | 已有 Session | 先断开 |

---

## 九、构建系统

### 9.1 依赖

| 工具 | 版本 |
|------|------|
| Visual Studio 2022 | Desktop C++、WinUI 3、.NET 8 |
| Rust | stable + x86_64-pc-windows-msvc |
| CMake | 3.20+ |
| Windows SDK | 10.0.22621+ |

链接库：`fwpuclnt.lib`、`ws2_32.lib`、`rpcrt4.lib`

### 9.2 目录结构

```text
joy-proxy-win/
├── DESIGN.md
├── docs/
│   └── DETAILED_DESIGN.md      # 本文档
├── src/
│   ├── ui/
│   │   └── JoyProxy/           # WinUI 3 解决方案
│   ├── service/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── wfp/
│   │   │   ├── WfpEngine.cpp
│   │   │   ├── AppIdResolver.cpp
│   │   │   └── RedirectTcpServer.cpp
│   │   └── ipc/
│   │       └── PipeServer.cpp
│   ├── relay/
│   │   ├── Cargo.toml
│   │   └── src/...
│   └── common/
│       └── schemas/
│           └── session_config.schema.json
├── .github/workflows/
│   └── build.yml
├── deploy.bat
├── LICENSE
└── README.md
```

### 9.3 构建顺序

```text
1. cargo build --release -p joyproxy_relay   → joyproxy_relay.dll
2. cmake --build src/service                 → JoyProxyService.exe
3. dotnet build src/ui/JoyProxy              → JoyProxy.exe
4. 复制 dll + exe 到 artifacts/Release/
```

### 9.4 CI（GitHub Actions）

```yaml
# windows-latest
# rustup + VS Build Tools + Windows App SDK
# 产物：JoyProxy-win-x64.zip（两个 exe + relay dll）
# Release tag 时上传
```

Git 推送经代理：`deploy.bat`（`http://sg-xx.edge.joyproxy.com:10000`）。

---

## 十、实现里程碑

### M1 — 基础骨架（约 3 天）

- [ ] WinUI 3 空壳 + 设置页 UI
- [ ] Rust relay：SOCKS5 + HTTP CONNECT 单元测试
- [ ] C FFI 集成测试（test_proxy）

### M2 — WFP 核心（约 5 天）

- [ ] WfpEngine 注册/卸载 V4+V6
- [ ] RedirectTcpServer + 原始目标解析
- [ ] 端到端：指定 `curl.exe` 路径，走 SOCKS5 出网

### M3 — IPC + 生命周期（约 3 天）

- [ ] Named Pipe 协议
- [ ] UAC 启动 Service + parent-pid watchdog
- [ ] 断开/异常清理 + force_cleanup

### M4 — UI 完整流程（约 3 天）

- [ ] 进程选择器、代理历史、连通性测试
- [ ] 状态机、错误提示、UAC 说明
- [ ] settings.json 持久化

### M5 — 打包与发布（约 2 天）

- [ ] CI build.yml
- [ ] 手动测试矩阵
- [ ] v1.0.0 GitHub Release

**v1.0 交付标准**：

1. 选择 exe + 填写 SOCKS5/HTTP 代理 → 连接成功
2. 该 exe 的 TCP 流量经代理（验证 IP 变化）
3. 其他 exe 不受影响
4. 断开后 WFP 无残留
5. JoyProxy 自身流量不走 WFP

---

## 十一、测试计划

### 11.1 单元测试

| 模块 | 用例 |
|------|------|
| Relay SOCKS5 | 无 auth / 有 auth / 拒绝 |
| Relay HTTP | CONNECT 200 / 407 |
| AppIdResolver | 路径规范化、不存在文件 |
| JSON IPC | 序列化/反序列化 SessionConfig |

### 11.2 集成测试

```powershell
# 1. 启动 JoyProxy，目标 curl.exe，代理 sg-xx.edge.joyproxy.com:10000
# 2. 验证
curl.exe https://api.ipify.org
# 3. 断开
# 4. 检查
netsh wfp show filters | findstr JoyProxy   # 应无输出
```

### 11.3 手动场景

- 目标：Chrome、Steam、自定义 tcp 客户端
- 代理：HTTP、SOCKS5、带密码
- 目标地址：IPv4、IPv6 站点
- 拒绝 UAC、杀 Service 进程、注销 Windows

---

## 十二、安全与合规

- 首次运行 EULA：说明需管理员权限、不保证游戏/反作弊场景
- 日志：`%AppData%\JoyProxy\logs\service.log`，不记录 proxy 密码
- 开源 GPLv3，WFP 规则仅本地生效

---

## 十三、后续版本预留接口

| 接口 | v2+ 用途 |
|------|---------|
| `SessionConfig.rules[]` | 多 exe / 域名 / 端口规则 |
| `WfpEngine::UpdateFilters()` | 热重载规则 |
| Windows Service 安装器 | 开机自启 |
| sing-box FFI | 统一 DNS/UDP/TUN |

---

*下一步：按 M1 里程碑开始编码。*
