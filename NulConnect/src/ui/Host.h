#pragma once

#include "ui/Widget.h"

#include <memory>
#include <string>
#include <vector>

namespace nc::ui {

// A top-level window rendered with Direct2D that hosts a widget tree plus
// optional overlays (dialogs, dropdowns). Input arrives in physical pixels
// and is converted to DIPs; layout and painting use DIPs throughout.
class Host {
public:
    Host();
    virtual ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    HWND Hwnd() const { return hwnd_; }
    float Dpi() const { return dpi_; }
    float Scale() const { return dpi_ / 96.0f; }
    D2D1_SIZE_F ClientSize() const;

    void SetRoot(std::unique_ptr<Widget> root);
    Widget* Root() const { return root_.get(); }

    void Invalidate();
    void InvalidateLayout();
    void RequestFrame();

    void SetFocus(Widget* widget, bool fromKeyboard = false);
    Widget* FocusedWidget() const { return focus_; }
    bool KeyboardFocusVisible() const { return keyboardFocusVisible_; }
    void MoveFocus(bool forward);

    // Overlays are painted above the root and receive input first. A modal
    // overlay blocks input to everything below it.
    Widget* PushOverlay(std::unique_ptr<Widget> overlay, bool modal);
    void RemoveOverlay(Widget* overlay);
    bool HasModalOverlay() const;

    // Text input support.
    void SetImeCaret(const RectF& caretInWindow);
    void StartCaretBlink();
    void StopCaretBlink();
    bool CaretVisible() const { return caretVisible_; }

    // Called when a widget is about to be destroyed.
    void ForgetWidget(Widget* widget);

    // Physical pixels <-> DIPs.
    int ToPixels(float dips) const { return static_cast<int>(std::lround(dips * Scale())); }
    float ToDips(int pixels) const { return pixels / Scale(); }

    static void BroadcastThemeChanged();

protected:
    bool CreateHostWindow(const wchar_t* className, const wchar_t* title, DWORD style, DWORD exStyle, int x, int y,
                          int width, int height, HWND owner);
    virtual LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);
    virtual void OnLayout(const RectF& client);
    virtual void PaintBackground(Canvas& canvas);
    virtual void PaintOverlay(Canvas&) {}
    virtual void OnThemeChanged();
    virtual void OnDpiChanged() {}
    // Escape when no overlay handled it.
    virtual void OnEscape() {}
    // Minimum client size in DIPs.
    virtual D2D1_SIZE_F MinimumSize() const { return D2D1::SizeF(0, 0); }
    // Dark title bar and backdrop material (Mica for main windows).
    virtual void ApplyWindowTheme();

    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();
    void EnsureRenderTarget();
    void DiscardRenderTarget();
    void DoLayout();
    PointF MousePoint(LPARAM lParam) const;
    Widget* HitTestAll(PointF point);
    Widget* InputRoot() const;
    void UpdateHover(PointF point);
    void CollectFocusable(Widget* widget, std::vector<Widget*>& out);

    ComPtr<ID2D1HwndRenderTarget> target_;
    std::unique_ptr<Widget> root_;
    struct Overlay {
        std::unique_ptr<Widget> widget;
        bool modal;
    };
    std::vector<Overlay> overlays_;
    std::vector<std::unique_ptr<Widget>> deferredDeletes_;
    Widget* hover_ = nullptr;
    Widget* captured_ = nullptr;
    Widget* focus_ = nullptr;
    float dpi_ = 96.0f;
    bool layoutDirty_ = true;
    bool frameRequested_ = false;
    bool keyboardFocusVisible_ = false;
    bool trackingMouse_ = false;
    bool caretVisible_ = true;
    UINT_PTR caretTimer_ = 0;
};

// Per-monitor DPI helpers that degrade gracefully on Windows 7/8.
UINT DpiForWindow(HWND hwnd);
UINT DpiForSystem();
int SystemMetricForDpi(int index, UINT dpi);
void EnableNonClientDpiScaling(HWND hwnd);

// Message pre-translation hooks run by the application message loop before
// TranslateMessage, e.g. for hosted ActiveX controls' keyboard accelerators.
using MessageFilter = std::function<bool(MSG&)>;
int AddMessageFilter(MessageFilter filter);
void RemoveMessageFilter(int token);
bool FilterMessage(MSG& message);

// Clipboard helpers.
bool CopyTextToClipboard(HWND owner, const std::wstring& text);
std::wstring ReadClipboardText(HWND owner);

}  // namespace nc::ui
