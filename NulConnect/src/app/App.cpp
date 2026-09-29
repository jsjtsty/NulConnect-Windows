#include "pch.h"
#include "app/App.h"
#include "app/TrayIcon.h"
#include "core/Dispatcher.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "model/SystemProxy.h"
#include "ui/Graphics.h"
#include "ui/Host.h"
#include "ui/Theme.h"
#include "windows/Flyout.h"
#include "windows/IEBrowser.h"
#include "windows/LoginWindow.h"
#include "windows/MainWindow.h"

#include <delayimp.h>

namespace nc {

namespace {

constexpr const wchar_t* kMessageWindowClass = L"NulConnect.AppMessage";
constexpr const wchar_t* kInstanceMutex = L"Local\\NulStudio.NulConnect.SingleInstance";
constexpr UINT kActivateMessage = WM_APP + 30;
constexpr UINT kTrayId = 1;

enum MenuCommand : UINT {
    kMenuToggleConnection = 100,
    kMenuOpen,
    kMenuSettings,
    kMenuQuit,
};

// Lets native popup menus follow the dark app theme (Windows 10 1903+).
// uxtheme ordinals 135/136 are undocumented but stable since 1903.
void EnableDarkMenus() {
    if (GetOsVersion().major < 10 || GetOsVersion().build < 18362) return;
    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(1 /* AllowDark */);
    if (flush) flush();
}

// reatrust.dll is delay-loaded so that a missing or incompatible library
// produces a readable message instead of a loader error dialog.
bool EnsureNativeLibrary(std::wstring& error) {
    std::wstring path = JoinPath(ExecutableDirectory(), L"reatrust.dll");
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        DWORD code = GetLastError();
        error = L"reatrust.dll could not be loaded (error " + std::to_wstring(code) + L").";
        if (code == ERROR_PROC_NOT_FOUND || !IsWindows10OrGreater()) {
            error += L"\n\nThis build of the protocol library requires Windows 10 or later.";
        }
        return false;
    }
    if (FAILED(__HrLoadAllImportsForDll("reatrust.dll"))) {
        error = L"reatrust.dll is incompatible with this version of NulConnect.";
        return false;
    }
    return true;
}

}  // namespace

App::~App() = default;

int App::Run(HINSTANCE, PWSTR commandLine, int) {
    std::wstring arguments = commandLine ? commandLine : L"";
    bool background = arguments.find(L"--background") != std::wstring::npos;

    bool debugInstance = false;
#ifdef _DEBUG
    debugInstance = arguments.find(L"--debug-login") != std::wstring::npos;
#endif
    HANDLE mutex = debugInstance ? nullptr : CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (!debugInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        // Bring the running instance forward instead of starting another.
        if (HWND existing = FindWindowW(kMessageWindowClass, nullptr)) {
            DWORD process = 0;
            GetWindowThreadProcessId(existing, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(existing, kActivateMessage, 0, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    // DPI awareness comes from the manifest (Per-Monitor v2 with fallbacks).
    // OLE (not just COM) is required to in-place activate the IE fallback
    // browser control.
    OleInitialize(nullptr);
    SetCurrentProcessExplicitAppUserModelID(L"NulStudio.NulConnect");
    LogInit();
    InitializeLocalization();
    Log(L"[App] starting NulConnect, Windows " + std::to_wstring(GetOsVersion().major) + L"." +
        std::to_wstring(GetOsVersion().minor) + L"." + std::to_wstring(GetOsVersion().build));

    std::wstring error;
    if (!EnsureNativeLibrary(error)) {
        MessageBoxW(nullptr, error.c_str(), L"NulConnect", MB_ICONERROR | MB_OK);
        return 1;
    }
    if (!ui::Graphics::Initialize()) {
        MessageBoxW(nullptr, L"Direct2D is not available on this system.", L"NulConnect", MB_ICONERROR | MB_OK);
        return 1;
    }
    ui::Theme::Refresh();
    EnableDarkMenus();
    Dispatcher::Initialize();

    int exitCode = 0;
    {
        App app;
        if (!app.Initialize(background)) {
            exitCode = 1;
        } else {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (ui::FilterMessage(message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            exitCode = static_cast<int>(message.wParam);
        }
    }
    Dispatcher::Shutdown();
    OleUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return exitCode;
}

bool App::Initialize(bool background) {
    CreateMessageWindow();
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(messageWindow_, taskbarCreatedMessage_, MSGFLT_ALLOW, nullptr);

    model_.onWebLoginSessionChanged = [this] { SyncLoginWindow(); };
    model_.onNotify = [this](const std::wstring& title, const std::wstring& message) {
        if (tray_) tray_->ShowNotification(title, message);
    };
    model_.clearWebLoginData = [this] { ClearWebLoginData(); };
    model_.Initialize();

    tray_ = std::make_unique<TrayIcon>(messageWindow_, kTrayId);
    tray_->Add();
    model_.Subscribe([this] {
        const auto& state = model_.connectionState();
        std::wstring tooltip = L"NulConnect\n" + PhaseTitle(state.phase);
        if (model_.IsConnectionActive()) tooltip += L" \x00B7 " + model_.ServerDisplayText();
        tray_->Update(state.phase, tooltip);
    });
    tray_->Update(ConnectionPhase::Disconnected, L"NulConnect\n" + PhaseTitle(ConnectionPhase::Disconnected));

    flyout_ = std::make_unique<Flyout>(model_);
    flyout_->Create();
    flyout_->onOpenMainWindow = [this] { ShowMainWindow(); };
    flyout_->onOpenSettings = [this] { ShowMainWindow(PageId::Connection); };
    flyout_->onQuit = [this] { Quit(); };

#ifdef _DEBUG
    // Debug builds: `--debug-login <url>` opens the sign-in window against a
    // test server with a CAS capture policy and only logs the captured URL.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--debug-login") != 0) continue;
        std::wstring url = argv[i + 1];
        auto parts = ParseUrl(url);
        auto policy = CapturePolicy::Make("auth/cas", parts ? Narrow(parts->host) : "127.0.0.1", Narrow(url));
        if (!policy) break;
        WebLoginSession session{999, {}, "debug", L"Debug sign-in", L"test server", url, *policy};
        login_ = std::make_unique<LoginWindow>(session);
        login_->onCaptured = [](const std::wstring& captured) {
            Log(L"[Debug] captured callback: " + captured);
        };
        login_->onCancel = [this] { login_.reset(); };
        if (login_->Create()) login_->Show();
    }
    if (argv) LocalFree(argv);
#endif
    main_ = std::make_unique<MainWindow>(model_);
    if (!main_->Create()) return false;
    main_->onCloseRequested = [this] { OnMainWindowCloseRequested(); };
    if (!background) ShowMainWindow();
    return true;
}

void App::CreateMessageWindow() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMessageWindowClass;
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only): it must receive
    // broadcasts such as TaskbarCreated, power and session-end messages.
    messageWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMessageWindowClass, L"NulConnect", WS_POPUP, 0, 0, 0, 0, nullptr,
                                     nullptr, wc.hInstance, this);
}

LRESULT CALLBACK App::MessageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app && app->messageWindow_ == hwnd) return app->HandleMessage(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_) {
        // Explorer restarted: the icon must be added again.
        tray_->Add();
        return 0;
    }
    switch (message) {
    case TrayIcon::kCallbackMessage:
        switch (LOWORD(lParam)) {
        case NIN_BALLOONUSERCLICK:
            // A notification about a sign-in that is waiting in the background.
            model_.PresentDeferredWebLogin();
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
            model_.PresentDeferredWebLogin();
            ToggleFlyout();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        default:
            break;
        }
        return 0;
    case kActivateMessage:
        ShowMainWindow();
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) model_.OnSystemSuspend();
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) model_.OnSystemResume();
        return TRUE;
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            ui::Host::BroadcastThemeChanged();
            tray_->Refresh();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        tray_->Refresh();
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) {
            // The process may be terminated right after this returns; undo
            // the system proxy synchronously. The helper service cleans up
            // its own routes when Windows stops it.
            SystemProxy::RestoreIfNeeded();
            tray_->Remove();
        }
        return 0;
    default:
        return DefWindowProcW(messageWindow_, message, wParam, lParam);
    }
}

void App::ShowMainWindow(PageId page) {
    model_.PresentDeferredWebLogin();
    if (flyout_) flyout_->Hide();
    main_->Show(page);
}

void App::OnMainWindowCloseRequested() {
    if (!model_.settings().closeToTray) {
        Quit();
        return;
    }
    main_->Hide();
    if (!model_.settings().trayHintShown) {
        model_.UpdateSettings([](AppSettings& s) { s.trayHintShown = true; });
        tray_->ShowNotification(Tr(L"NulConnect is still running"),
                                Tr(L"NulConnect keeps running in the notification area. Right-click the icon to quit."));
    }
}

void App::ToggleFlyout() {
    if (flyout_->IsShown()) {
        flyout_->Hide();
        return;
    }
    // A click on the icon first deactivates (and hides) the open flyout.
    if (flyout_->RecentlyHidden()) return;
    RECT anchor{};
    if (!tray_->GetRect(anchor)) {
        POINT cursor;
        GetCursorPos(&cursor);
        anchor = RECT{cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    }
    flyout_->ShowNear(anchor);
}

void App::ShowTrayMenu() {
    flyout_->Hide();
    HMENU menu = CreatePopupMenu();
    std::wstring status = PhaseTitle(model_.connectionState().phase);
    if (model_.IsConnectionActive()) status += L" \x00B7 " + model_.ServerDisplayText();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT connectFlags = MF_STRING;
    if (model_.IsPrimaryActionBusy() || !model_.IsLoginConfigurationReady()) connectFlags |= MF_GRAYED;
    std::wstring connect = model_.PrimaryActionTitle();
    AppendMenuW(menu, connectFlags, kMenuToggleConnection, connect.c_str());
    AppendMenuW(menu, MF_STRING, kMenuOpen, Tr(L"Open NulConnect").c_str());
    AppendMenuW(menu, MF_STRING, kMenuSettings, Tr(L"Settings").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, Tr(L"Quit NulConnect").c_str());
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);

    POINT cursor;
    GetCursorPos(&cursor);
    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(messageWindow_);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, flags, cursor.x, cursor.y, messageWindow_, nullptr));
    PostMessageW(messageWindow_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    switch (command) {
    case kMenuToggleConnection: model_.PerformPrimaryAction(); break;
    case kMenuOpen: ShowMainWindow(); break;
    case kMenuSettings: ShowMainWindow(PageId::Connection); break;
    case kMenuQuit: Quit(); break;
    default: break;
    }
}

void App::SyncLoginWindow() {
    const auto& session = model_.webLoginSession();
    if (!session) {
        login_.reset();
        return;
    }
    bool hidden = model_.IsWebLoginHidden();
    if (login_ && login_->SessionId() == session->id) {
        if (!hidden) login_->Show();
        return;
    }
    login_ = std::make_unique<LoginWindow>(*session);
    login_->onCaptured = [this](const std::wstring& url) {
        if (login_) ShowWindow(login_->Hwnd(), SW_HIDE);
        model_.CompleteWebLogin(url);
    };
    login_->onCancel = [this] { model_.CancelWebLogin(); };
    login_->onNeedsInteraction = [this] { model_.PresentWebLogin(); };
    if (login_->Create(hidden)) {
        if (!hidden) login_->Show();
    } else {
        login_.reset();
        model_.CancelWebLogin();
    }
}

void App::ClearWebLoginData(int attempt) {
    login_.reset();
    if (attempt == 0) IEBrowser::EndBrowserSession();
    // Browser processes exit shortly after the last WebView closes.
    if (LoginWindow::ClearBrowsingData() || attempt >= 10) return;
    Dispatcher::SetTimeout(500, [this, attempt] { ClearWebLoginData(attempt + 1); });
}

void App::Quit() {
    if (quitting_) return;
    quitting_ = true;
    Log("[App] quitting");
    if (main_) main_->Hide();
    if (flyout_) flyout_->Hide();
    login_.reset();
    model_.PrepareForExit([this] {
        tray_->Remove();
        PostQuitMessage(0);
    });
}

}  // namespace nc
