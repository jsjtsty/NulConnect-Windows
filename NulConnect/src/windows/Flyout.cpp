#include "pch.h"
#include "windows/Flyout.h"
#include "core/Localization.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "windows/Visuals.h"

namespace nc {

using namespace ui;

namespace {
constexpr float kWidth = 360.0f;
constexpr float kPadding = 16.0f;
constexpr float kFooterHeight = 52.0f;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
}  // namespace

class Flyout::Content : public Widget {
public:
    Content(Flyout& flyout, AppModel& model) : flyout_(flyout), model_(model) {
        glyph_ = Emplace<StatusGlyph>(48.0f);
        phase_ = Emplace<TextBlock>(L"", TextStyle::Subtitle);
        phase_->SetWrap(false);
        message_ = Emplace<TextBlock>(L"", TextStyle::Caption, TextColor::Secondary);
        message_->SetMaxLines(2);
        details_ = Emplace<TextBlock>(L"", TextStyle::Caption, TextColor::Secondary);
        details_->SetWrap(false);
        primary_ = Emplace<Button>(L"", ButtonStyle::Accent);
        primary_->SetLarge(true);
        primary_->onClick = [this] { model_.PerformPrimaryAction(); };
        down_ = Emplace<RateTile>(Icon::ArrowDown, Tr(L"Download"), true);
        up_ = Emplace<RateTile>(Icon::ArrowUp, Tr(L"Upload"), false);
        settings_ = Emplace<Button>(L"", ButtonStyle::Subtle, Icon::Settings);
        settings_->SetTooltip(Tr(L"Settings"));
        settings_->onClick = [this] {
            flyout_.Hide();
            if (flyout_.onOpenSettings) flyout_.onOpenSettings();
        };
        open_ = Emplace<Button>(Tr(L"Open NulConnect"), ButtonStyle::Subtle, Icon::Window);
        open_->onClick = [this] {
            flyout_.Hide();
            if (flyout_.onOpenMainWindow) flyout_.onOpenMainWindow();
        };
        quit_ = Emplace<Button>(L"", ButtonStyle::Subtle, Icon::Power);
        quit_->SetTooltip(Tr(L"Quit NulConnect"));
        quit_->onClick = [this] {
            flyout_.Hide();
            if (flyout_.onQuit) flyout_.onQuit();
        };
        appear_.Jump(1);
    }

    void Refresh() {
        const auto& state = model_.connectionState();
        glyph_->SetPhase(state.phase);
        phase_->SetText(PhaseTitle(state.phase));
        std::wstring message = state.message;
        if (message.empty()) message = model_.NeedsLogin() ? Tr(L"Not logged in") : model_.ServerDisplayText();
        message_->SetText(message);
        std::wstring details = model_.RoutePresentationModeTitle() + L"  \x00B7  " + model_.ServerDisplayText();
        if (model_.EffectiveRouteMode() == RouteMode::Proxy && model_.IsProxyRunning()) {
            details += L"  \x00B7  " + model_.ProxyEndpointText();
        }
        details_->SetText(details);
        bool running = model_.IsConnectionActive();
        primary_->SetText(model_.PrimaryActionTitle());
        primary_->SetIcon(running ? Icon::Power : (model_.NeedsLogin() ? Icon::SignIn : Icon::Power));
        primary_->SetStyle(running ? ButtonStyle::Danger : ButtonStyle::Accent);
        primary_->SetEnabled(!model_.IsPrimaryActionBusy() && model_.IsLoginConfigurationReady());
        live_ = running && model_.traffic().isLive;
        down_->SetVisible(live_);
        up_->SetVisible(live_);
        if (live_) {
            down_->SetValue(FormatRate(model_.traffic().downloadBytesPerSecond));
            up_->SetValue(FormatRate(model_.traffic().uploadBytesPerSecond));
        }
    }

    float DesiredHeight() const {
        float height = kPadding + 32 + 12 + 64 + 8 + 20 + 16 + 40 + kPadding;
        if (live_) height += 60;
        return height + kFooterHeight;
    }

    void Animate() {
        appear_.Jump(0);
        appear_.Set(1, Motion::Slow);
    }

    void Arrange(const RectF& rect) override {
        bounds_ = rect;
        float left = rect.left + kPadding;
        float right = rect.right - kPadding;
        float y = rect.top + kPadding;
        settings_->Arrange(MakeRect(right - 32, y, 32, 32));
        y += 32 + 12;
        glyph_->Arrange(MakeRect(left - 8, y, 64, 64));
        float textLeft = left + 64;
        phase_->Arrange(RectF{textLeft, y + 6, right, y + 34});
        float messageHeight = std::min(message_->Measure(right - textLeft), 32.0f);
        message_->Arrange(MakeRect(textLeft, y + 34, right - textLeft, messageHeight));
        y += 64 + 8;
        details_->Arrange(RectF{left, y, right, y + 20});
        y += 20 + 16;
        primary_->Arrange(RectF{left, y, right, y + 40});
        y += 40;
        if (live_) {
            y += 12;
            float half = (right - left - 12) / 2;
            down_->Arrange(MakeRect(left, y, half, 52));
            up_->Arrange(MakeRect(left + half + 12, y, half, 52));
            y += 48;
        }
        footer_ = RectF{rect.left, rect.bottom - kFooterHeight, rect.right, rect.bottom};
        float footerY = footer_.top + (kFooterHeight - 32) / 2;
        float openWidth = open_->MeasureWidth();
        open_->Arrange(MakeRect(left - 4, footerY, openWidth, 32));
        quit_->Arrange(MakeRect(right - 32 + 4, footerY, 32, 32));
    }

    void Paint(Canvas& canvas) override {
        const Palette& p = Theme::Current();
        float t = appear_.Value(this);
        // Footer strip, like the quick settings flyout.
        canvas.FillRect(footer_, p.dark ? Rgba(0x000000, 0.12f) : Rgba(0x000000, 0.03f));
        canvas.Line(PointF{footer_.left, footer_.top + 0.5f}, PointF{footer_.right, footer_.top + 0.5f}, p.divider);
        canvas.PushTranslate(0, 16 * (1 - t));
        ComPtr<ID2D1Layer> layer;
        canvas.Target()->CreateLayer(&layer);
        canvas.Target()->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                         D2D1::IdentityMatrix(), 0.2f + 0.8f * t),
                                   layer.Get());
        float left = bounds_.left + kPadding;
        DrawBrandLogo(canvas, MakeRect(left, bounds_.top + kPadding + 6, 20, 20));
        canvas.Text(L"NulConnect", TextStyle::BodyStrong,
                    RectF{left + 28, bounds_.top + kPadding, bounds_.right - 60, bounds_.top + kPadding + 32}, p.textPrimary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        PaintChildren(canvas);
        canvas.Target()->PopLayer();
        canvas.PopTransform();
    }

    bool OnKeyDown(UINT key, bool, bool) override {
        if (key == VK_ESCAPE) {
            flyout_.Hide();
            return true;
        }
        return false;
    }

private:
    Flyout& flyout_;
    AppModel& model_;
    StatusGlyph* glyph_;
    TextBlock* phase_;
    TextBlock* message_;
    TextBlock* details_;
    Button* primary_;
    RateTile* down_;
    RateTile* up_;
    Button* settings_;
    Button* open_;
    Button* quit_;
    RectF footer_{};
    bool live_ = false;
    Animated appear_;
};

Flyout::Flyout(AppModel& model) : model_(model) {}

Flyout::~Flyout() {
    if (subscription_) model_.Unsubscribe(subscription_);
    model_.SetTrafficObserver("flyout", false);
}

bool Flyout::Create() {
    if (!CreateHostWindow(L"NulConnect.Flyout", L"NulConnect", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 0, 0,
                          ToPixels(kWidth), ToPixels(400), nullptr)) {
        return false;
    }
    if (!IsWindows11OrGreater()) {
        // Windows 11 draws a shadow for rounded popups; older systems need
        // the classic drop shadow.
        SetClassLongPtrW(hwnd_, GCL_STYLE, GetClassLongPtrW(hwnd_, GCL_STYLE) | CS_DROPSHADOW);
    }
    auto content = std::make_unique<Content>(*this, model_);
    content_ = content.get();
    SetRoot(std::move(content));
    subscription_ = model_.Subscribe([this] { Refresh(); });
    return true;
}

void Flyout::ApplyWindowTheme() {
    if (!hwnd_) return;
    if (IsWindows10OrGreater()) {
        BOOL dark = Theme::IsDark();
        DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    }
    if (IsWindows11OrGreater()) {
        int corner = 2;  // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference, &corner, sizeof(corner));
    }
    if (SupportsMica()) {
        int backdrop = Theme::UseMica() ? 3 /* DWMSBT_TRANSIENTWINDOW (acrylic) */ : 1;
        DwmSetWindowAttribute(hwnd_, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
        MARGINS margins = Theme::UseMica() ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
    }
}

void Flyout::Refresh() {
    if (!content_ || !IsShown()) return;
    float before = content_->DesiredHeight();
    content_->Refresh();
    if (content_->DesiredHeight() != before) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        int height = ToPixels(content_->DesiredHeight());
        SetWindowPos(hwnd_, nullptr, rect.left, rect.bottom - height, rect.right - rect.left, height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateLayout();
}

D2D1_SIZE_F Flyout::DesiredSize() {
    content_->Refresh();
    return D2D1::SizeF(kWidth, content_->DesiredHeight());
}

void Flyout::ShowNear(const RECT& anchor) {
    ApplyWindowTheme();
    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    // DPI of the target monitor decides the pixel size.
    UINT dpi = 96;
    {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static auto getDpi = shcore ? reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
        UINT y = 0;
        if (!getDpi || FAILED(getDpi(monitor, 0, &dpi, &y))) dpi = DpiForSystem();
    }
    float scale = dpi / 96.0f;
    D2D1_SIZE_F size = DesiredSize();
    int width = static_cast<int>(size.width * scale);
    int height = static_cast<int>(size.height * scale);
    int margin = static_cast<int>(12 * scale);
    RECT work = info.rcWork;
    APPBARDATA bar{sizeof(bar)};
    UINT edge = ABE_BOTTOM;
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar)) edge = bar.uEdge;
    int x = work.right - width - margin;
    int y = work.bottom - height - margin;
    if (edge == ABE_LEFT) x = work.left + margin;
    if (edge == ABE_TOP) y = work.top + margin;
    if (edge == ABE_LEFT || edge == ABE_RIGHT) {
        y = std::clamp(static_cast<int>(anchor.bottom) - height, static_cast<int>(work.top) + margin,
                       static_cast<int>(work.bottom) - height - margin);
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_);
    content_->Animate();
    model_.SetTrafficObserver("flyout", true);
    InvalidateLayout();
}

void Flyout::Hide() {
    if (!IsShown()) return;
    hiddenAt_ = GetTickCount64();
    SetFocus(nullptr);
    ShowWindow(hwnd_, SW_HIDE);
    model_.SetTrafficObserver("flyout", false);
}

bool Flyout::IsShown() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

bool Flyout::RecentlyHidden() const {
    return GetTickCount64() - hiddenAt_ < 300;
}

void Flyout::PaintBackground(Canvas& canvas) {
    const Palette& p = Theme::Current();
    D2D1_SIZE_F size = ClientSize();
    RectF rect = MakeRect(0, 0, size.width, size.height);
    if (p.translucent) {
        // Light tint over the system acrylic for legibility.
        canvas.FillRect(rect, p.dark ? Rgba(0x202020, 0.35f) : Rgba(0xF3F3F3, 0.45f));
    } else {
        canvas.FillRect(rect, p.flyoutBackground);
        canvas.StrokeRoundRect(rect, 0, p.dark ? Rgba(0x000000, 0.4f) : Rgba(0x000000, 0.15f));
    }
}

LRESULT Flyout::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) Hide();
        break;
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_DPICHANGED:
        break;
    default:
        break;
    }
    return Host::OnMessage(message, wParam, lParam);
}

}  // namespace nc
