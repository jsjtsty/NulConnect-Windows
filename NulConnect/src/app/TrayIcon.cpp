#include "pch.h"
#include "app/TrayIcon.h"
#include "ui/Host.h"
#include "windows/Visuals.h"

namespace nc {

using namespace ui;

TrayIcon::TrayIcon(HWND owner, UINT id) : owner_(owner), id_(id) {}

TrayIcon::~TrayIcon() {
    Remove();
    if (icon_) DestroyIcon(icon_);
}

NOTIFYICONDATAW TrayIcon::Data() const {
    NOTIFYICONDATAW data{sizeof(data)};
    data.hWnd = owner_;
    data.uID = id_;
    return data;
}

void TrayIcon::Add() {
    if (!icon_) icon_ = Render(phase_);
    NOTIFYICONDATAW data = Data();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = icon_;
    wcsncpy_s(data.szTip, tooltip_.empty() ? L"NulConnect" : tooltip_.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void TrayIcon::Remove() {
    if (!added_) return;
    NOTIFYICONDATAW data = Data();
    Shell_NotifyIconW(NIM_DELETE, &data);
    added_ = false;
}

void TrayIcon::Update(ConnectionPhase phase, const std::wstring& tooltip) {
    bool phaseChanged = phase != phase_ || !icon_;
    if (!phaseChanged && tooltip == tooltip_) return;
    phase_ = phase;
    tooltip_ = tooltip;
    NOTIFYICONDATAW data = Data();
    data.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (phaseChanged) {
        HICON previous = icon_;
        icon_ = Render(phase_);
        data.uFlags |= NIF_ICON;
        data.hIcon = icon_;
        Shell_NotifyIconW(NIM_MODIFY, &data);
        if (previous) DestroyIcon(previous);
    }
    wcsncpy_s(data.szTip, tooltip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayIcon::Refresh() {
    HICON previous = icon_;
    icon_ = Render(phase_);
    NOTIFYICONDATAW data = Data();
    data.uFlags = NIF_ICON;
    data.hIcon = icon_;
    Shell_NotifyIconW(NIM_MODIFY, &data);
    if (previous) DestroyIcon(previous);
}

void TrayIcon::ShowNotification(const std::wstring& title, const std::wstring& message) {
    NOTIFYICONDATAW data = Data();
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_RESPECT_QUIET_TIME;
    data.hBalloonIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
    if (data.hBalloonIcon) DestroyIcon(data.hBalloonIcon);
}

bool TrayIcon::GetRect(RECT& rect) const {
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = owner_;
    identifier.uID = id_;
    return SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect));
}

HICON TrayIcon::Render(ConnectionPhase phase) const {
    UINT dpi = DpiForSystem();
    int size = SystemMetricForDpi(SM_CXSMICON, dpi);
    ComPtr<IWICBitmap> bitmap;
    if (!Graphics::Wic() || FAILED(Graphics::Wic()->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA,
                                                                  WICBitmapCacheOnLoad, &bitmap))) {
        return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    }
    ComPtr<ID2D1RenderTarget> target;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(Graphics::D2D()->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target))) {
        return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    }
    float s = static_cast<float>(size);
    bool darkTaskbar = Theme::IsTaskbarDark();
    Color glyph = darkTaskbar ? Rgba(0xFFFFFF) : Rgba(0x1A1A1A);
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        Canvas canvas(target.Get());
        // Monochrome brand mark, dimmed while disconnected.
        Color mark = phase == ConnectionPhase::Disconnected ? WithAlpha(glyph, 0.6f) : glyph;
        DrawBrandLogo(canvas, MakeRect(-s * 0.1f, -s * 0.12f, s * 1.2f, s * 1.2f), true, mark);
        Color badge{};
        bool showBadge = true;
        switch (phase) {
        case ConnectionPhase::Connected: badge = Rgba(0x3FB950); break;
        case ConnectionPhase::Connecting:
        case ConnectionPhase::Disconnecting: badge = Rgba(0xF2B600); break;
        case ConnectionPhase::Failed: badge = Rgba(0xE5484D); break;
        default: showBadge = false; break;
        }
        if (showBadge) {
            float r = s * 0.2f;
            PointF center{s - r - 0.5f, s - r - 0.5f};
            canvas.FillEllipse(center, r + s * 0.07f, r + s * 0.07f, darkTaskbar ? Rgba(0x202020) : Rgba(0xF3F3F3));
            canvas.FillEllipse(center, r, r, badge);
        }
    }
    target->EndDraw();

    // CreateIconIndirect expects straight (non-premultiplied) alpha.
    std::vector<BYTE> pixels(static_cast<size_t>(size) * size * 4);
    bitmap->CopyPixels(nullptr, size * 4, static_cast<UINT>(pixels.size()), pixels.data());
    for (size_t i = 0; i < pixels.size(); i += 4) {
        BYTE alpha = pixels[i + 3];
        if (alpha && alpha < 255) {
            for (int c = 0; c < 3; ++c) pixels[i + c] = static_cast<BYTE>(std::min(255, pixels[i + c] * 255 / alpha));
        }
    }
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    memcpy(bits, pixels.data(), pixels.size());
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace nc
