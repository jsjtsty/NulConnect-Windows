#include "pch.h"
#include "ui/Host.h"
#include "core/Platform.h"

namespace nc::ui {

namespace {

constexpr UINT_PTR kCaretTimer = 0x4E43;
constexpr UINT_PTR kFrameTimer = 0x4E44;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmUseImmersiveDarkModeOld = 19;
constexpr DWORD kDwmSystemBackdropType = 38;

std::vector<Host*> g_hosts;

PointF FromWindow(const Widget* widget, PointF point) {
    for (const Widget* ancestor = widget->Parent(); ancestor; ancestor = ancestor->Parent()) {
        PointF offset = ancestor->ChildOffset();
        point.x += offset.x;
        point.y += offset.y;
    }
    return point;
}

bool IsAncestorOf(const Widget* ancestor, const Widget* widget) {
    for (; widget; widget = widget->Parent()) {
        if (widget == ancestor) return true;
    }
    return false;
}

}  // namespace

UINT DpiForSystem() {
    using GetDpiForSystemFn = UINT(WINAPI*)();
    static auto getDpiForSystem =
        reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (getDpiForSystem) return getDpiForSystem();
    HDC dc = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

UINT DpiForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto getDpiForWindow =
        reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && hwnd) {
        UINT dpi = getDpiForWindow(hwnd);
        if (dpi) return dpi;
    }
    // Windows 8.1: shcore!GetDpiForMonitor.
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto getDpiForMonitor =
        shcore ? reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    if (getDpiForMonitor && hwnd) {
        UINT x = 0, y = 0;
        if (SUCCEEDED(getDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), 0, &x, &y)) && x) return x;
    }
    return DpiForSystem();
}

int SystemMetricForDpi(int index, UINT dpi) {
    using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);
    static auto fn = reinterpret_cast<GetSystemMetricsForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    if (fn) return fn(index, dpi);
    return MulDiv(GetSystemMetrics(index), static_cast<int>(dpi), static_cast<int>(DpiForSystem()));
}

void EnableNonClientDpiScaling(HWND hwnd) {
    using EnableFn = BOOL(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<EnableFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "EnableNonClientDpiScaling"));
    if (fn) fn(hwnd);
}

namespace {
std::map<int, MessageFilter> g_messageFilters;
int g_nextMessageFilter = 1;
}  // namespace

int AddMessageFilter(MessageFilter filter) {
    int token = g_nextMessageFilter++;
    g_messageFilters[token] = std::move(filter);
    return token;
}

void RemoveMessageFilter(int token) {
    g_messageFilters.erase(token);
}

bool FilterMessage(MSG& message) {
    for (auto& [token, filter] : g_messageFilters) {
        if (filter && filter(message)) return true;
    }
    return false;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (!ok) GlobalFree(memory);
    }
    CloseClipboard();
    return ok;
}

std::wstring ReadClipboardText(HWND owner) {
    std::wstring text;
    if (!OpenClipboard(owner)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

Host::Host() {
    g_hosts.push_back(this);
}

Host::~Host() {
    g_hosts.erase(std::remove(g_hosts.begin(), g_hosts.end(), this), g_hosts.end());
    hover_ = captured_ = focus_ = nullptr;
    deferredDeletes_.clear();
    overlays_.clear();
    root_.reset();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Host::CreateHostWindow(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y,
                            int width, int height, HWND owner) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(instance, className, &wc)) {
        wc = WNDCLASSEXW{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(exStyle, className, title, style, x, y, width, height, owner, nullptr, instance, this);
    if (!hwnd_) return false;
    dpi_ = static_cast<float>(DpiForWindow(hwnd_));
    ApplyWindowTheme();
    return true;
}

void Host::ApplyWindowTheme() {
    if (!hwnd_ || !IsWindows10OrGreater()) return;
    BOOL dark = Theme::IsDark() ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkModeOld, &dark, sizeof(dark));
    }
    if (SupportsMica()) {
        int backdrop = Theme::UseMica() ? 2 /* DWMSBT_MAINWINDOW */ : 1 /* DWMSBT_NONE */;
        DwmSetWindowAttribute(hwnd_, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
        MARGINS margins = Theme::UseMica() ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
    }
}

D2D1_SIZE_F Host::ClientSize() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    return D2D1::SizeF(ToDips(rect.right - rect.left), ToDips(rect.bottom - rect.top));
}

void Host::SetRoot(std::unique_ptr<Widget> root) {
    hover_ = captured_ = focus_ = nullptr;
    root_ = std::move(root);
    if (root_) root_->AttachHost(this);
    InvalidateLayout();
}

void Host::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Host::InvalidateLayout() {
    layoutDirty_ = true;
    Invalidate();
}

void Host::RequestFrame() {
    frameRequested_ = true;
}

void Host::ForgetWidget(Widget* widget) {
    if (hover_ && IsAncestorOf(widget, hover_)) hover_ = nullptr;
    if (captured_ && IsAncestorOf(widget, captured_)) {
        captured_ = nullptr;
        if (GetCapture() == hwnd_) ReleaseCapture();
    }
    if (focus_ && IsAncestorOf(widget, focus_)) {
        focus_ = nullptr;
        StopCaretBlink();
    }
}

void Host::SetFocus(Widget* widget, bool fromKeyboard) {
    keyboardFocusVisible_ = fromKeyboard;
    if (focus_ == widget) {
        Invalidate();
        return;
    }
    Widget* previous = focus_;
    focus_ = widget;
    if (previous) {
        previous->SetFocusedFlag(false);
        previous->OnFocusChanged(false);
    }
    if (focus_) {
        focus_->SetFocusedFlag(true);
        focus_->OnFocusChanged(true);
        if (fromKeyboard) focus_->BringIntoView(focus_->Bounds());
    }
    Invalidate();
}

void Host::CollectFocusable(Widget* widget, std::vector<Widget*>& out) {
    if (!widget || !widget->IsVisible() || !widget->IsEnabled()) return;
    if (widget->IsFocusable()) out.push_back(widget);
    for (auto& child : widget->Children()) CollectFocusable(child.get(), out);
}

Widget* Host::InputRoot() const {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        if (it->modal) return it->widget.get();
    }
    return root_.get();
}

void Host::MoveFocus(bool forward) {
    std::vector<Widget*> focusable;
    for (auto& overlay : overlays_) {
        if (overlay.modal) focusable.clear();
        CollectFocusable(overlay.widget.get(), focusable);
    }
    if (!HasModalOverlay()) {
        std::vector<Widget*> rootItems;
        CollectFocusable(root_.get(), rootItems);
        focusable.insert(focusable.begin(), rootItems.begin(), rootItems.end());
    }
    if (focusable.empty()) return;
    auto it = std::find(focusable.begin(), focusable.end(), focus_);
    size_t index;
    if (it == focusable.end()) {
        index = forward ? 0 : focusable.size() - 1;
    } else {
        size_t current = static_cast<size_t>(it - focusable.begin());
        index = forward ? (current + 1) % focusable.size() : (current + focusable.size() - 1) % focusable.size();
    }
    SetFocus(focusable[index], true);
}

Widget* Host::PushOverlay(std::unique_ptr<Widget> overlay, bool modal) {
    Widget* raw = overlay.get();
    overlay->AttachHost(this);
    overlays_.push_back({std::move(overlay), modal});
    if (modal && focus_) SetFocus(nullptr);
    InvalidateLayout();
    return raw;
}

void Host::RemoveOverlay(Widget* overlay) {
    auto it = std::find_if(overlays_.begin(), overlays_.end(), [overlay](const Overlay& item) { return item.widget.get() == overlay; });
    if (it == overlays_.end()) return;
    ForgetWidget(overlay);
    // Defer destruction to the next frame: this is often called from inside
    // the overlay's own event handler.
    deferredDeletes_.push_back(std::move(it->widget));
    overlays_.erase(it);
    InvalidateLayout();
}

bool Host::HasModalOverlay() const {
    return std::any_of(overlays_.begin(), overlays_.end(), [](const Overlay& item) { return item.modal; });
}

void Host::SetImeCaret(const RectF& caret) {
    HIMC context = ImmGetContext(hwnd_);
    if (!context) return;
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = POINT{ToPixels(caret.left), ToPixels(caret.top)};
    ImmSetCompositionWindow(context, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = POINT{ToPixels(caret.left), ToPixels(caret.bottom)};
    candidate.rcArea = RECT{ToPixels(caret.left), ToPixels(caret.top), ToPixels(caret.right), ToPixels(caret.bottom)};
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(hwnd_, context);
}

void Host::StartCaretBlink() {
    caretVisible_ = true;
    UINT blink = GetCaretBlinkTime();
    if (blink == INFINITE || blink == 0) {
        StopCaretBlink();
        caretVisible_ = true;
        return;
    }
    caretTimer_ = SetTimer(hwnd_, kCaretTimer, blink, nullptr);
    Invalidate();
}

void Host::StopCaretBlink() {
    if (caretTimer_) {
        KillTimer(hwnd_, kCaretTimer);
        caretTimer_ = 0;
    }
    caretVisible_ = false;
}

void Host::BroadcastThemeChanged() {
    Theme::Refresh();
    for (Host* host : g_hosts) {
        host->ApplyWindowTheme();
        host->DiscardRenderTarget();
        host->OnThemeChanged();
    }
}

void Host::OnThemeChanged() {
    InvalidateLayout();
}

void Host::OnLayout(const RectF& client) {
    if (root_) root_->Arrange(client);
    for (auto& overlay : overlays_) overlay.widget->Arrange(client);
}

void Host::PaintBackground(Canvas& canvas) {
    const Palette& palette = Theme::Current();
    if (!palette.translucent) {
        D2D1_SIZE_F size = ClientSize();
        canvas.FillRect(MakeRect(0, 0, size.width, size.height), palette.windowBackground);
    }
}

void Host::DoLayout() {
    layoutDirty_ = false;
    D2D1_SIZE_F size = ClientSize();
    OnLayout(MakeRect(0, 0, size.width, size.height));
}

void Host::EnsureRenderTarget() {
    if (target_) return;
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    D2D1_ALPHA_MODE alpha = Theme::Current().translucent ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, alpha), dpi_, dpi_);
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rect.right)), static_cast<UINT32>(std::max<LONG>(1, rect.bottom))));
    if (SUCCEEDED(Graphics::D2D()->CreateHwndRenderTarget(props, hwndProps, &target_))) {
        target_->SetTextAntialiasMode(Theme::Current().translucent ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE
                                                                   : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
}

void Host::DiscardRenderTarget() {
    target_.Reset();
    Invalidate();
}

void Host::Render() {
    deferredDeletes_.clear();
    EnsureRenderTarget();
    if (!target_) return;
    if (layoutDirty_) DoLayout();
    frameRequested_ = false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        Canvas canvas(target_.Get());
        PaintBackground(canvas);
        if (root_ && root_->IsVisible()) root_->Paint(canvas);
        for (auto& overlay : overlays_) overlay.widget->Paint(canvas);
        PaintOverlay(canvas);
    }
    HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardRenderTarget();
        return;
    }
    // Animations pace themselves with a timer. Invalidating right away would
    // keep a paint message pending forever, and WM_TIMER (which drives the
    // Dispatcher's timers, such as the traffic sampling) is only delivered
    // when no paint is pending.
    if (frameRequested_ && hwnd_) SetTimer(hwnd_, kFrameTimer, 16, nullptr);
}

PointF Host::MousePoint(LPARAM lParam) const {
    return PointF{ToDips(GET_X_LPARAM(lParam)), ToDips(GET_Y_LPARAM(lParam))};
}

Widget* Host::HitTestAll(PointF point) {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        if (Widget* hit = it->widget->HitTest(point)) return hit;
        if (it->modal) return nullptr;
    }
    return root_ ? root_->HitTest(point) : nullptr;
}

void Host::UpdateHover(PointF point) {
    Widget* hit = HitTestAll(point);
    if (captured_ && hit != captured_) hit = nullptr;
    if (hit == hover_) return;
    if (hover_) {
        hover_->SetHovered(false);
        hover_->OnMouseLeave();
    }
    hover_ = hit;
    if (hover_) {
        hover_->SetHovered(true);
        hover_->OnMouseEnter();
    }
    Invalidate();
}

LRESULT Host::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    return HandleMessage(message, wParam, lParam);
}

LRESULT CALLBACK Host::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* host = static_cast<Host*>(create->lpCreateParams);
        host->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
        EnableNonClientDpiScaling(hwnd);
    }
    auto* host = reinterpret_cast<Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        host->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return host->OnMessage(message, wParam, lParam);
}

LRESULT Host::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        Render();
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        if (target_) {
            RECT rect{};
            GetClientRect(hwnd_, &rect);
            target_->Resize(D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rect.right)),
                                        static_cast<UINT32>(std::max<LONG>(1, rect.bottom))));
        }
        InvalidateLayout();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(wParam));
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        DiscardRenderTarget();
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        OnDpiChanged();
        InvalidateLayout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        D2D1_SIZE_F minimum = MinimumSize();
        if (minimum.width > 0 && hwnd_) {
            RECT rect{0, 0, ToPixels(minimum.width), ToPixels(minimum.height)};
            AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE,
                               static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)));
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            BroadcastThemeChanged();
        }
        break;
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
    case WM_SYSCOLORCHANGE:
        BroadcastThemeChanged();
        break;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            trackingMouse_ = true;
        }
        PointF point = MousePoint(lParam);
        UpdateHover(point);
        if (Widget* target = captured_ ? captured_ : hover_) {
            target->OnMouseMove(FromWindow(target, point));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (!captured_ && hover_) {
            hover_->SetHovered(false);
            hover_->OnMouseLeave();
            hover_ = nullptr;
            Invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        PointF point = MousePoint(lParam);
        UpdateHover(point);
        Widget* target = hover_;
        MouseButton button = message == WM_RBUTTONDOWN ? MouseButton::Right : MouseButton::Left;
        if (button == MouseButton::Left) {
            SetFocus(target && target->IsFocusable() && target->IsEnabled() ? target : nullptr);
        }
        if (target && target->IsEnabled()) {
            captured_ = target;
            SetCapture(hwnd_);
            target->SetPressed(true);
            if (message == WM_LBUTTONDBLCLK) target->OnDoubleClick(FromWindow(target, point));
            target->OnMouseDown(FromWindow(target, point), button);
            Invalidate();
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        PointF point = MousePoint(lParam);
        Widget* target = captured_;
        captured_ = nullptr;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (target) {
            target->SetPressed(false);
            target->OnMouseUp(FromWindow(target, point), message == WM_RBUTTONUP ? MouseButton::Right : MouseButton::Left);
        }
        UpdateHover(point);
        Invalidate();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (captured_ && reinterpret_cast<HWND>(lParam) != hwnd_) {
            captured_->SetPressed(false);
            captured_->OnCaptureLost();
            captured_ = nullptr;
            Invalidate();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &screen);
        PointF point{ToDips(screen.x), ToDips(screen.y)};
        float delta = GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA);
        for (Widget* widget = HitTestAll(point); widget; widget = widget->Parent()) {
            if (widget->OnMouseWheel(delta)) break;
        }
        // Scroll containers are not interactive themselves; route to the
        // deepest widget under the cursor, including non-interactive ones.
        if (!HitTestAll(point)) {
            std::function<Widget*(Widget*, PointF)> deepest = [&](Widget* widget, PointF p) -> Widget* {
                if (!widget || !widget->IsVisible() || !Contains(widget->Bounds(), p)) return nullptr;
                PointF offset = widget->ChildOffset();
                PointF child{p.x + offset.x, p.y + offset.y};
                for (auto it = widget->Children().rbegin(); it != widget->Children().rend(); ++it) {
                    if (Widget* hit = deepest(it->get(), child)) return hit;
                }
                return widget;
            };
            Widget* start = InputRoot();
            for (Widget* widget = deepest(start, point); widget; widget = widget->Parent()) {
                if (widget->OnMouseWheel(delta)) break;
            }
        }
        UpdateHover(point);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(hover_ ? hover_->Cursor() : LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        bool shift = GetKeyState(VK_SHIFT) < 0;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        UINT key = static_cast<UINT>(wParam);
        if (key == VK_TAB && !ctrl) {
            MoveFocus(!shift);
            return 0;
        }
        for (Widget* widget = focus_; widget; widget = widget->Parent()) {
            if (widget->OnKeyDown(key, shift, ctrl)) {
                if (key != VK_SHIFT && key != VK_CONTROL) Invalidate();
                return 0;
            }
        }
        if (key == VK_ESCAPE) {
            for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
                if (it->widget->OnKeyDown(key, shift, ctrl)) return 0;
            }
            OnEscape();
            return 0;
        }
        if (message == WM_SYSKEYDOWN) break;
        return 0;
    }
    case WM_CHAR:
        if (focus_ && wParam >= 0x20 && wParam != 0x7F) {
            focus_->OnChar(static_cast<wchar_t>(wParam));
            Invalidate();
        }
        return 0;
    case WM_IME_STARTCOMPOSITION:
        break;
    case WM_TIMER:
        if (wParam == kFrameTimer) {
            KillTimer(hwnd_, kFrameTimer);
            Invalidate();
            return 0;
        }
        if (wParam == kCaretTimer) {
            caretVisible_ = !caretVisible_;
            Invalidate();
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (focus_ && caretTimer_) {
            StopCaretBlink();
            Invalidate();
        }
        break;
    case WM_SETFOCUS:
        if (focus_) focus_->OnFocusChanged(true);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace nc::ui
