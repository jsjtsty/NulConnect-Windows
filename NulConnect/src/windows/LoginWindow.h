#pragma once

#include "model/Types.h"
#include "ui/Host.h"

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

namespace nc {

class IEBrowser;

// Single sign-on window. Hosts Microsoft Edge WebView2, loads the portal's
// login page and captures the SSO callback navigation before it reaches the
// network (the ticket/code must only be redeemed by libreatrust).
class LoginWindow : public ui::Host {
public:
    explicit LoginWindow(WebLoginSession session);
    ~LoginWindow() override;

    bool Create();
    void Show();
    uint64_t SessionId() const { return session_.id; }

    // Called once with the captured callback URL.
    std::function<void(const std::wstring&)> onCaptured;
    std::function<void()> onCancel;

    static std::wstring UserDataFolder();
    // Removes cookies and cache of the embedded browser (sign-out). Returns
    // false while browser processes still hold the files.
    static bool ClearBrowsingData();

protected:
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnLayout(const ui::RectF& client) override;
    void OnEscape() override { Cancel(); }
    D2D1_SIZE_F MinimumSize() const override { return D2D1::SizeF(480, 400); }

private:
    class Chrome;
    void InitializeWebView();
    void OnControllerCreated(ICoreWebView2Controller* controller);
    // Falls back to the Internet Explorer engine; false when unavailable.
    bool StartLegacyBrowser();
    void OnWebView2Unavailable();
    void Capture(const std::wstring& url);
    void Cancel();
    void ShowError(const std::wstring& message, bool runtimeMissing);
    void UpdateBounds();
    void DoLayoutNow();
    void CloseWebView();

    WebLoginSession session_;
    Chrome* chrome_ = nullptr;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<IEBrowser> legacy_;
    int messageFilter_ = 0;
    bool captured_ = false;
    bool finished_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    ui::RectF webArea_{};
};

}  // namespace nc
