#pragma once

#include "model/Types.h"

#include <string>

namespace nc {

// Notification area icon. The glyph is rendered at runtime so it follows the
// taskbar theme, the DPI and the connection state.
class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 20;

    TrayIcon(HWND owner, UINT id);
    ~TrayIcon();

    void Add();
    void Remove();
    void Update(ConnectionPhase phase, const std::wstring& tooltip);
    void Refresh();  // taskbar theme or DPI changed
    void ShowNotification(const std::wstring& title, const std::wstring& message);
    bool GetRect(RECT& rect) const;

private:
    HICON Render(ConnectionPhase phase) const;
    NOTIFYICONDATAW Data() const;

    HWND owner_;
    UINT id_;
    HICON icon_ = nullptr;
    ConnectionPhase phase_ = ConnectionPhase::Disconnected;
    std::wstring tooltip_;
    bool added_ = false;
};

}  // namespace nc
