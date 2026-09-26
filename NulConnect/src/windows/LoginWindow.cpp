#include "pch.h"
#include "windows/LoginWindow.h"
#include "core/Dispatcher.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "ui/Controls.h"
#include "windows/IEBrowser.h"

#include <WebView2.h>
#include <wrl/event.h>

namespace nc {

using namespace ui;
using Microsoft::WRL::Callback;

namespace {
constexpr float kHeaderHeight = 72.0f;
constexpr float kMargin = 16.0f;
constexpr const wchar_t* kRuntimeDownload = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";

// Failures where no usable page could be shown. WebView2 also reports
// IsSuccess=false for HTTP 4xx responses and pages that call window.stop(),
// which are routine during SSO redirects and must not raise a warning.
bool IsNetworkFailure(COREWEBVIEW2_WEB_ERROR_STATUS status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE:
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET:
    case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT:
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED:
        return true;
    default:
        return false;
    }
}
}  // namespace

// Header (title, subtitle, cancel), an error bar and the loading indicator
// drawn around the WebView2 child window.
class LoginWindow::Chrome : public Widget {
public:
    Chrome(LoginWindow& window, const WebLoginSession& session) : window_(window) {
        title_ = Emplace<TextBlock>(session.title, TextStyle::Subtitle);
        title_->SetWrap(false);
        subtitle_ = Emplace<TextBlock>(session.subtitle, TextStyle::Caption, TextColor::Secondary);
        subtitle_->SetWrap(false);
        cancel_ = Emplace<Button>(Tr(L"Cancel"));
        cancel_->SetMinWidth(96);
        cancel_->onClick = [this] { window_.Cancel(); };
        error_ = Emplace<InfoBar>(Severity::Warning, L"", L"");
        error_->SetVisible(false);
        install_ = Emplace<Button>(Tr(L"Install WebView2 Runtime"), ButtonStyle::Accent, Icon::Download);
        install_->SetVisible(false);
        install_->onClick = [] { ShellExecuteW(nullptr, L"open", kRuntimeDownload, nullptr, nullptr, SW_SHOWNORMAL); };
        ring_ = Emplace<ProgressRing>(32.0f, 3.0f);
        loading_ = Emplace<TextBlock>(Tr(L"Loading sign-in page…"), TextStyle::Body, TextColor::Secondary);
        loading_->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    void SetLoading(bool loading) {
        ring_->SetVisible(loading);
        loading_->SetVisible(loading);
        InvalidateLayout();
    }

    void SetError(const std::wstring& message, bool runtimeMissing) {
        error_->Set(runtimeMissing ? Severity::Error : Severity::Warning, L"", message);
        error_->SetVisible(true);
        install_->SetVisible(runtimeMissing);
        if (runtimeMissing) SetLoading(false);
        InvalidateLayout();
    }

    void ClearError() {
        // The runtime-missing error and the legacy-engine notice are
        // permanent for this window.
        if (!error_->IsVisible() || install_->IsVisible()) return;
        error_->SetVisible(false);
        InvalidateLayout();
    }

    // Internet Explorer fallback: notice bar plus the install button moved
    // into the header (the browser control covers the page area).
    void SetLegacyEngine() {
        legacy_ = true;
        error_->Set(Severity::Informational, L"",
                    Tr(L"The WebView2 Runtime is not installed, so the legacy Internet Explorer engine is used. Some sign-in pages may not display correctly."));
        error_->SetVisible(true);
        install_->SetStyle(ButtonStyle::Standard);
        install_->SetVisible(true);
        InvalidateLayout();
    }

    RectF WebArea() const { return webArea_; }

    void Arrange(const RectF& rect) override {
        bounds_ = rect;
        float left = rect.left + kMargin + 44;
        float cancelWidth = cancel_->MeasureWidth();
        float buttonsLeft = rect.right - kMargin - cancelWidth;
        cancel_->Arrange(MakeRect(buttonsLeft, rect.top + 20, cancelWidth, 32));
        float installWidth = install_->MeasureWidth();
        if (legacy_) {
            buttonsLeft -= installWidth + 8;
            install_->Arrange(MakeRect(buttonsLeft, rect.top + 20, installWidth, 32));
        }
        title_->Arrange(RectF{left, rect.top + 14, buttonsLeft - 12, rect.top + 42});
        subtitle_->Arrange(RectF{left, rect.top + 42, buttonsLeft - 12, rect.top + 60});
        float y = rect.top + kHeaderHeight;
        if (error_->IsVisible()) {
            float width = Width(rect) - 2 * kMargin;
            float height = error_->Measure(width);
            error_->Arrange(MakeRect(rect.left + kMargin, y, width, height));
            y += height + 12;
        }
        webArea_ = RectF{rect.left + kMargin, y, rect.right - kMargin, rect.bottom - kMargin};
        float centerY = (webArea_.top + webArea_.bottom) / 2;
        float centerX = (webArea_.left + webArea_.right) / 2;
        ring_->Arrange(MakeRect(centerX - 16, centerY - 40, 32, 32));
        loading_->Arrange(MakeRect(webArea_.left, centerY + 4, Width(webArea_), 24));
        if (!legacy_) install_->Arrange(MakeRect(centerX - installWidth / 2, centerY - 16, installWidth, 32));
    }

    void Paint(Canvas& canvas) override {
        const Palette& p = Theme::Current();
        DrawIcon(canvas, Icon::Key, MakeRect(bounds_.left + kMargin, bounds_.top + 18, 32, 36), p.textSecondary, 28);
        canvas.FillRoundRect(webArea_, Theme::OverlayRadius, p.cardBackground);
        canvas.StrokeRoundRect(Inset(webArea_, -1, -1), Theme::OverlayRadius, p.cardStroke);
        PaintChildren(canvas);
    }

private:
    LoginWindow& window_;
    TextBlock* title_;
    TextBlock* subtitle_;
    Button* cancel_;
    InfoBar* error_;
    Button* install_;
    ProgressRing* ring_;
    TextBlock* loading_;
    RectF webArea_{};
    bool legacy_ = false;
};

LoginWindow::LoginWindow(WebLoginSession session) : session_(std::move(session)) {}

LoginWindow::~LoginWindow() {
    *alive_ = false;
    CloseWebView();
}

std::wstring LoginWindow::UserDataFolder() {
    return JoinPath(LocalDataDirectory(), L"WebView2");
}

bool LoginWindow::ClearBrowsingData() {
    std::wstring folder = UserDataFolder();
    DeleteDirectoryTree(folder);
    return GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES;
}

bool LoginWindow::Create() {
    std::wstring title = TrFormat(L"Sign in to %1$@", {session_.title});
    if (!CreateHostWindow(L"NulConnect.LoginWindow", title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr)) {
        return false;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::min(ToPixels(1000), static_cast<int>(work.right - work.left) - 40);
    int height = std::min(ToPixels(720), static_cast<int>(work.bottom - work.top) - 40);
    SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
                 width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    auto chrome = std::make_unique<Chrome>(*this, session_);
    chrome_ = chrome.get();
    SetRoot(std::move(chrome));
    InitializeWebView();
    return true;
}

void LoginWindow::Show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void LoginWindow::OnWebView2Unavailable() {
    if (StartLegacyBrowser()) return;
    ShowError(Tr(L"The Microsoft Edge WebView2 Runtime is required to sign in. Install it and try again."), true);
}

bool LoginWindow::StartLegacyBrowser() {
    if (legacy_) return true;
    chrome_->SetLegacyEngine();
    DoLayoutNow();
    RECT bounds{ToPixels(webArea_.left) + 1, ToPixels(webArea_.top) + 1, ToPixels(webArea_.right) - 1, ToPixels(webArea_.bottom) - 1};
    std::weak_ptr<bool> alive = alive_;
    IEBrowser::Callbacks callbacks;
    callbacks.shouldCapture = [this](const std::wstring& url) { return session_.policy.ShouldCapture(url); };
    callbacks.onCapture = [this, alive](const std::wstring& url) {
        if (alive.lock()) Capture(url);
    };
    callbacks.onCompleted = [this, alive](bool networkFailure, long) {
        if (!alive.lock() || captured_) return;
        chrome_->SetLoading(false);
        if (networkFailure) {
            ShowError(Tr(L"The sign-in page could not be loaded. Check the network connection and the server address."), false);
        }
    };
    legacy_ = IEBrowser::Create(hwnd_, bounds, std::move(callbacks));
    if (!legacy_) return false;
    IEBrowser* browser = legacy_.Get();
    messageFilter_ = AddMessageFilter([browser](MSG& message) { return browser->PreTranslate(message); });
    legacy_->Navigate(session_.startUrl);
    Log("[WebLogin] load start (IE) " + LoggableUrl(Narrow(session_.startUrl)));
    return true;
}

void LoginWindow::InitializeWebView() {
    // NULCONNECT_BROWSER=ie forces the legacy engine (testing).
    wchar_t forced[8] = {};
    if (GetEnvironmentVariableW(L"NULCONNECT_BROWSER", forced, 8) && _wcsicmp(forced, L"ie") == 0) {
        OnWebView2Unavailable();
        return;
    }
    std::wstring folder = UserDataFolder();
    CreateDirectoryW(folder.c_str(), nullptr);
    std::weak_ptr<bool> alive = alive_;
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, folder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, alive](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (!alive.lock()) return S_OK;
                if (FAILED(result) || !environment) {
                    Log("[WebLogin] WebView2 environment failed: 0x" + std::to_string(static_cast<unsigned long>(result)));
                    OnWebView2Unavailable();
                    return S_OK;
                }
                environment_ = environment;
                environment->CreateCoreWebView2Controller(
                    hwnd_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                               [this, alive](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                                   if (!alive.lock()) {
                                       if (controller) controller->Close();
                                       return S_OK;
                                   }
                                   if (FAILED(created) || !controller) {
                                       ShowError(Tr(L"Could not start the embedded browser."), false);
                                       return S_OK;
                                   }
                                   OnControllerCreated(controller);
                                   return S_OK;
                               })
                               .Get());
                return S_OK;
            })
            .Get());
    if (FAILED(hr)) {
        Log("[WebLogin] WebView2 unavailable: 0x" + std::to_string(static_cast<unsigned long>(hr)));
        OnWebView2Unavailable();
    }
}

void LoginWindow::OnControllerCreated(ICoreWebView2Controller* controller) {
    controller_ = controller;
    controller_->get_CoreWebView2(&webview_);
    if (!webview_) return;
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(webview_->get_Settings(&settings))) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_IsWebMessageEnabled(FALSE);
    }
    std::weak_ptr<bool> alive = alive_;
    EventRegistrationToken token{};
    // Primary interception point. NavigationStarting fires for a server-side
    // redirect only after the network stack has already requested the
    // redirect target, which would let the browser redeem the one-time
    // ticket first. WebResourceRequested runs before the request is sent;
    // the callback request is answered locally and never leaves the machine.
    webview_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT);
    webview_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                if (!alive.lock()) return S_OK;
                ComPtr<ICoreWebView2WebResourceRequest> request;
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&uri)) || !uri) return S_OK;
                std::wstring url = uri;
                CoTaskMemFree(uri);
                if (!captured_ && !session_.policy.ShouldCapture(url)) return S_OK;
                ComPtr<ICoreWebView2WebResourceResponse> response;
                if (environment_ &&
                    SUCCEEDED(environment_->CreateWebResourceResponse(nullptr, 204, L"No Content", L"", &response))) {
                    args->put_Response(response.Get());
                }
                Capture(url);
                return S_OK;
            })
            .Get(),
        &token);
    // Fallback for navigations the filter does not see.
    webview_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (!alive.lock()) return S_OK;
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
                std::wstring url = uri;
                CoTaskMemFree(uri);
                if (captured_) {
                    args->put_Cancel(TRUE);
                    return S_OK;
                }
                if (session_.policy.ShouldCapture(url)) {
                    args->put_Cancel(TRUE);
                    Capture(url);
                    return S_OK;
                }
                Log("[WebLogin] navigate " + LoggableUrl(Narrow(url)));
                return S_OK;
            })
            .Get(),
        &token);
    webview_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                if (!alive.lock() || captured_) return S_OK;
                chrome_->SetLoading(false);
                BOOL success = TRUE;
                args->get_IsSuccess(&success);
                COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                args->get_WebErrorStatus(&status);
                int httpStatus = 0;
                ComPtr<ICoreWebView2NavigationCompletedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2)))) args2->get_HttpStatusCode(&httpStatus);
                Log("[WebLogin] navigation completed success=" + std::to_string(success) + " webError=" +
                    std::to_string(static_cast<int>(status)) + " http=" + std::to_string(httpStatus));
                if (success) {
                    // A later successful navigation supersedes an earlier failure.
                    if (chrome_) chrome_->ClearError();
                } else if (IsNetworkFailure(status)) {
                    ShowError(Tr(L"The sign-in page could not be loaded. Check the network connection and the server address."), false);
                }
                // Other failures (HTTP 4xx on an intermediate SSO hop, window.stop(),
                // aborted redirects) still render a usable page and are not errors.
                if (controller_) controller_->put_IsVisible(TRUE);
                return S_OK;
            })
            .Get(),
        &token);
    // SSO pages sometimes open a new window; keep it in this view.
    webview_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (!alive.lock()) return S_OK;
                LPWSTR uri = nullptr;
                args->put_Handled(TRUE);
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    webview_->Navigate(uri);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            })
            .Get(),
        &token);
    controller_->put_IsVisible(FALSE);
    UpdateBounds();
    webview_->Navigate(session_.startUrl.c_str());
    Log("[WebLogin] load start " + LoggableUrl(Narrow(session_.startUrl)));
}

void LoginWindow::Capture(const std::wstring& url) {
    if (captured_) return;
    captured_ = true;
    finished_ = true;
    Log("[WebLogin] captured callback " + LoggableUrl(Narrow(url)));
    if (webview_) webview_->Stop();
    if (legacy_) legacy_->Stop();
    if (onCaptured) {
        auto handler = onCaptured;
        // Defer: the handler closes this window.
        Dispatcher::Post([handler, url] { handler(url); });
    }
}

void LoginWindow::Cancel() {
    if (finished_) return;
    finished_ = true;
    if (onCancel) {
        auto handler = onCancel;
        Dispatcher::Post([handler] { handler(); });
    }
}

void LoginWindow::ShowError(const std::wstring& message, bool runtimeMissing) {
    if (chrome_) chrome_->SetError(message, runtimeMissing);
    if (runtimeMissing && controller_) controller_->put_IsVisible(FALSE);
    InvalidateLayout();
}

void LoginWindow::CloseWebView() {
    if (controller_) {
        controller_->Close();
        controller_.Reset();
    }
    webview_.Reset();
    if (messageFilter_) {
        RemoveMessageFilter(messageFilter_);
        messageFilter_ = 0;
    }
    if (legacy_) {
        legacy_->Close();
        legacy_.Reset();
    }
}

void LoginWindow::OnLayout(const RectF& client) {
    Host::OnLayout(client);
    if (chrome_) webArea_ = chrome_->WebArea();
    UpdateBounds();
}

void LoginWindow::UpdateBounds() {
    RECT bounds{ToPixels(webArea_.left) + 1, ToPixels(webArea_.top) + 1, ToPixels(webArea_.right) - 1, ToPixels(webArea_.bottom) - 1};
    if (controller_) controller_->put_Bounds(bounds);
    if (legacy_) legacy_->SetBounds(bounds);
}

void LoginWindow::DoLayoutNow() {
    OnLayout(MakeRect(0, 0, ClientSize().width, ClientSize().height));
}

LRESULT LoginWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE:
        Cancel();
        return 0;
    case WM_MOVE:
    case WM_MOVING:
        if (controller_) controller_->NotifyParentWindowPositionChanged();
        break;
    default:
        break;
    }
    return Host::OnMessage(message, wParam, lParam);
}

}  // namespace nc
