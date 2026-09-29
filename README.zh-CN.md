# NulConnect for Windows

[![License: AGPL v3](https://img.shields.io/badge/License-AGPLv3-blue.svg)](LICENSE)

[English](README.md) | 简体中文

NulConnect for Windows 是 **深信服 aTrust**（零信任接入服务）的第三方开源 Windows 客户端。它提供原生桌面界面，用来登录 aTrust 服务端、管理会话，并通过本地代理或全局隧道访问服务端发布的内部资源。本项目是 macOS 版 [NulConnect](https://github.com/jsjtsty/NulConnect) 的 Windows 移植。

> **声明：** 本项目为非官方项目，与深信服科技无隶属、认可或支持关系。“aTrust”“深信服”是其各自所有者的商标。请仅用于你有权访问的服务。

## 下载

在 [Releases](https://github.com/jsjtsty/NulConnect-Windows/releases) 获取最新版本：

- `NulConnect-<版本>-setup.exe`：安装包。安装到 `%ProgramFiles%\NulConnect` 并注册特权辅助服务，之后使用 VPN 模式不需要再次授权。
- `NulConnect-<版本>-win-x64.zip`：免安装版。首次使用时由应用安装辅助服务（会弹出一次管理员授权）。
- `SHA256SUMS.txt`：上述文件的校验和。

程序没有代码签名，首次运行时 Windows SmartScreen 可能会给出警告。

## 功能

- 原生 Win32 应用：使用 Direct2D 和 DirectWrite 自绘 Fluent 风格控件，不依赖 .NET、WinUI 或 Windows App SDK
- Windows 11 上的 Mica 和亚克力背景，浅色和深色主题，支持每显示器 DPI
- 密码、短信和 Web 单点登录（Microsoft Edge WebView2，无 WebView2 时回退到 Internet Explorer 引擎）
- 用 DPAPI 保护的会话持久化与恢复
- 本地代理模式，可选自动设置 Windows 系统代理：可代理全部流量，也可通过 PAC 脚本只把内网资源交给代理
- VPN/TUN 模式，全局接管流量：基于 Wintun，通过 NRPT 实现按域名的 DNS，并用 fake-IP DNS 支持按域名匹配的资源
- 仅隧道传输 IPv4，IPv6 数据包会被丢弃而不是走隧道
- 服务端单点登录无需输入即可完成时的静默重新登录、启动时自动连接，以及清理上次异常退出遗留的 VPN 状态
- 通知区域图标和快捷浮窗
- 诊断日志开关，可一键把日志导出为 zip
- 提供安装包和免安装版
- 支持英文、简体中文、繁体中文、日文、德文、法文和西班牙文

应用把 aTrust 的协议、认证、资源和传输交给 Rust 库 [libreatrust](https://github.com/jsjtsty/libreatrust) 处理，特权平台操作（TUN 适配器、路由和 DNS）由以 Windows 服务运行的 [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper) 完成。

## 系统要求

- Windows 10 或更高版本，x64。不支持 Windows 7。
- Web 登录需要 [Microsoft Edge WebView2 运行时](https://developer.microsoft.com/microsoft-edge/webview2/)；没有时应用会回退到 Internet Explorer 引擎。
- 安装辅助服务需要管理员授权，VPN 模式依赖它。

构建需要安装了 **使用 C++ 的桌面开发** 工作负载的 Visual Studio 2026（MSVC v145 工具集和 Windows 10/11 SDK），并需要联网下载预编译依赖。

## 本地构建

```powershell
git clone https://github.com/jsjtsty/NulConnect-Windows.git
cd NulConnect-Windows
powershell -ExecutionPolicy Bypass -File tools\update-dependencies.ps1
msbuild NulConnect.slnx /p:Configuration=Release /p:Platform=x64
```

也可以在 Visual Studio 中打开 `NulConnect.slnx`，首次构建会自动下载缺失的依赖。输出（包括 `reatrust.dll`、`nulconnect-helper.exe` 和 `wintun.dll`）位于 `build\x64\<配置>\`。

应用和 Rust 组件都静态链接 C 运行库，因此不需要安装 Visual C++ Redistributable。

## 打包

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

编译 Release 配置，并在 `dist\` 生成两个文件：[Inno Setup 6](https://jrsoftware.org/isinfo.php) 安装包（`installer\NulConnect.iss`）和免安装 zip。加 `-SkipBuild` 可以直接打包已有的构建结果。发布版本使用本地构建，并通过 `gh` 命令行创建 Release。

## 依赖

预编译依赖存放在被 Git 忽略的 `lib\` 目录，由 `tools\update-dependencies.ps1` 下载：

| 依赖 | 版本 | 来源 |
|---|---|---|
| [libreatrust](https://github.com/jsjtsty/libreatrust) | v0.3.5 | GitHub Releases |
| [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper) | v0.3.3 | GitHub Releases |
| [Wintun](https://www.wintun.net/) | 0.14.1 | wintun.net（固定 SHA-256） |
| [WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2) | 1.0.4191.47 | NuGet（固定 SHA-256） |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | GitHub Releases（固定 SHA-256） |

测试其他兼容版本的 Rust 组件：

```powershell
powershell -ExecutionPolicy Bypass -File tools\update-dependencies.ps1 -LibreatrustVersion v0.3.5 -HelperVersion v0.3.3
```

也可以用环境变量 `LIBREATRUST_VERSION` 和 `NULCONNECT_HELPER_VERSION`。加 `-Force` 会重新下载。

## 仓库结构

```
NulConnect.slnx            Visual Studio 解决方案
NulConnect/
  res/                     图标、清单、版本资源、Windows 专用字符串
  src/app/                 入口、单实例、通知区域图标
  src/atr/                 libreatrust C API 的 C++ 封装
  src/core/                字符串、日志、调度器、本地化、平台辅助
  src/model/               应用状态机、存储、认证、代理、辅助服务客户端、隧道
  src/ui/                  Direct2D 渲染、主题、控件
  src/windows/             主窗口、页面、浮窗、登录窗口
installer/                 Inno Setup 脚本和中文语言文件
tools/
  update-dependencies.ps1  下载 lib\
  package.ps1              生成安装包和免安装 zip
  gen-localization.py      从 macOS 应用的 .strings 文件生成字符串表
  check-strings.py         列出代码中用到但字符串表里缺少的字符串
  make-icon.ps1            用 macOS 图标素材生成 .ico
  fake-cas-server.py       测试登录回调捕获的本地服务
  capture-window.ps1       把窗口截图保存为 PNG（界面检查）
  post-click.ps1           向窗口发送点击（界面检查）
```

`gen-localization.py` 和 `make-icon.ps1` 需要读取 [macOS 应用](https://github.com/jsjtsty/NulConnect) 的检出目录。Windows 专用字符串在 `NulConnect/res/strings-windows.json`。

## 数据位置

- 设置、配置和 DPAPI 保护的会话：`%APPDATA%\NulConnect`
- 日志：`%LOCALAPPDATA%\NulConnect\Logs`（应用和库）、`%ProgramData%\NulConnect\Logs`（辅助服务）
- 已安装的辅助服务：`%ProgramFiles%\NulConnect\Helper`

## 相关项目

- [NulConnect](https://github.com/jsjtsty/NulConnect)：macOS 应用
- [libreatrust](https://github.com/jsjtsty/libreatrust)：协议和传输库
- [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper)：特权辅助程序

## 许可证

NulConnect for Windows 使用 GNU Affero General Public License v3.0 许可，见 [LICENSE](LICENSE)。

第三方组件保留各自的许可证：Wintun（预编译二进制许可）、WebView2 SDK（微软 BSD 风格许可）和 nlohmann/json（MIT）。
