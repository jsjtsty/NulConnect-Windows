# NulConnect for Windows

[![License: AGPL v3](https://img.shields.io/badge/License-AGPLv3-blue.svg)](LICENSE)

NulConnect for Windows is a native Windows client for compatible secure access services. It is a port of the macOS [NulConnect](https://github.com/jsjtsty/NulConnect) app and provides the same authentication, session management, proxy access, and system-wide tunnel access.

> **Status:** early development (0.1.0). Sign-in and proxy mode are working. The privileged helper, VPN mode, and the installer are still being tested.

## Features

- Native Win32 application: Direct2D and DirectWrite with custom-drawn Fluent-style controls, no .NET, WinUI, or Windows App SDK
- Mica and acrylic backdrops on Windows 11, light and dark themes, per-monitor DPI awareness
- Password, SMS, and web-based single sign-on (Microsoft Edge WebView2, with an Internet Explorer engine fallback)
- Persistent session storage protected with DPAPI, and session resumption
- Local proxy mode with optional Windows system-proxy integration, either for all traffic or through a PAC script that sends only intranet resources to the proxy
- VPN/TUN mode for system-wide traffic routing, based on Wintun and per-domain DNS through NRPT
- Silent re-login when the portal's single sign-on can finish without input, connect on launch, and cleanup of VPN state left by a crashed session
- Notification-area icon with a quick-access flyout
- English, Simplified Chinese, Traditional Chinese, Japanese, German, French, and Spanish

The application delegates protocol, authentication, resource, and transport operations to the [libreatrust](https://github.com/jsjtsty/libreatrust) Rust library. Privileged platform operations (the TUN adapter, routes, and DNS) are handled by [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper), which runs as a Windows service.

## Requirements

- Windows 10 or later, x64. Windows 7 support is planned.
- [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) for web sign-in. Without it, the app falls back to the Internet Explorer engine.
- Administrator authorization to install the helper, which VPN mode needs.

Building requires Visual Studio 2026 with the **Desktop development with C++** workload (MSVC v145 toolset and a Windows 10/11 SDK), plus network access to download the prebuilt dependencies.

## Build locally

```powershell
git clone https://github.com/jsjtsty/NulConnect-Windows.git
cd NulConnect-Windows
powershell -ExecutionPolicy Bypass -File tools\update-dependencies.ps1
msbuild NulConnect.slnx /p:Configuration=Release /p:Platform=x64
```

You can also open `NulConnect.slnx` in Visual Studio. The first build downloads any missing dependencies automatically. The output, including `reatrust.dll`, `nulconnect-helper.exe` and `wintun.dll`, is placed in `build\x64\<Configuration>\`.

Both the app and the Rust components link the C runtime statically, so the Visual C++ Redistributable is not required.

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

Builds the Release configuration and writes two files to `dist\`: an [Inno Setup 6](https://jrsoftware.org/isinfo.php) installer (`installer\NulConnect.iss`; installs to `%ProgramFiles%\NulConnect` and registers the helper service) and a portable zip (the app installs the helper on first use). Use `-SkipBuild` to package an existing build.

## Dependencies

The prebuilt dependencies are stored under `lib\`, which is ignored by Git. `tools\update-dependencies.ps1` downloads them:

| Dependency | Version | Source |
|---|---|---|
| [libreatrust](https://github.com/jsjtsty/libreatrust) | v0.3.5 | GitHub Releases |
| [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper) | v0.3.3 | GitHub Releases |
| [Wintun](https://www.wintun.net/) | 0.14.1 | wintun.net (SHA-256 pinned) |
| [WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2) | 1.0.4191.47 | NuGet (SHA-256 pinned) |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | GitHub Releases (SHA-256 pinned) |

To test another compatible release of the Rust components:

```powershell
powershell -ExecutionPolicy Bypass -File tools\update-dependencies.ps1 -LibreatrustVersion v0.3.5 -HelperVersion v0.3.3
```

The `LIBREATRUST_VERSION` and `NULCONNECT_HELPER_VERSION` environment variables work too. Pass `-Force` to download again.

## Repository layout

```
NulConnect.slnx            Visual Studio solution
NulConnect/
  res/                     icon, manifest, version resource, Windows strings
  src/app/                 entry point, single instance, notification-area icon
  src/atr/                 C++ wrapper of the libreatrust C API
  src/core/                strings, logging, dispatcher, localization, platform helpers
  src/model/               app state machine, stores, auth, proxy, helper client, tunnel
  src/ui/                  Direct2D rendering, theme, widgets and controls
  src/windows/             main window, pages, flyout, sign-in window
installer/                 Inno Setup script and Chinese language files
tools/
  update-dependencies.ps1  downloads lib\
  package.ps1              builds the installer and the portable zip
  gen-localization.py      regenerates the string table from the macOS app's .strings files
  check-strings.py         lists strings used in code but missing from the table
  make-icon.ps1            builds the .ico from the macOS icon artwork
  fake-cas-server.py       local server for testing sign-in callback capture
  capture-window.ps1       captures a window to PNG (UI checks)
  post-click.ps1           sends a click to a window (UI checks)
```

`gen-localization.py` and `make-icon.ps1` read from a checkout of the [macOS app](https://github.com/jsjtsty/NulConnect). Windows-specific strings live in `NulConnect/res/strings-windows.json`.

## Data locations

- Settings, profile and the DPAPI-protected session: `%APPDATA%\NulConnect`
- Logs: `%LOCALAPPDATA%\NulConnect\Logs` (app and library), `%ProgramData%\NulConnect\Logs` (helper)
- Installed helper: `%ProgramFiles%\NulConnect\Helper`

## Related projects

- [NulConnect](https://github.com/jsjtsty/NulConnect): the macOS app
- [libreatrust](https://github.com/jsjtsty/libreatrust): protocol and transport library
- [nulconnect-helper](https://github.com/jsjtsty/nulconnect-helper): privileged helper

## License

NulConnect for Windows is licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE).

Third-party components keep their own licenses: Wintun (prebuilt binaries license), the WebView2 SDK (BSD-style Microsoft license) and nlohmann/json (MIT).
