#pragma once

#include "ui/Graphics.h"

#include <functional>
#include <memory>
#include <vector>

namespace nc::ui {

class Host;
class Widget;

// Fluent motion durations, in seconds.
namespace Motion {
constexpr double Fast = 0.083;
constexpr double Normal = 0.167;
constexpr double Slow = 0.25;
}  // namespace Motion

// A value that eases toward a target. Reading it while in motion asks the
// owning widget's host for another frame.
class Animated {
public:
    explicit Animated(float value = 0) : from_(value), to_(value) {}

    void Set(float target, double duration = Motion::Fast);
    void Jump(float value);
    float Value(Widget* requester);
    float Target() const { return to_; }

private:
    float from_;
    float to_;
    double start_ = 0;
    double duration_ = 0;
};

enum class MouseButton { Left, Right };

class Widget {
public:
    Widget() = default;
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // ---- Tree -----------------------------------------------------------
    template <typename T>
    T* Add(std::unique_ptr<T> child) {
        T* raw = child.get();
        AddChild(std::move(child));
        return raw;
    }
    template <typename T, typename... Args>
    T* Emplace(Args&&... args) {
        return Add(std::make_unique<T>(std::forward<Args>(args)...));
    }
    void AddChild(std::unique_ptr<Widget> child);
    void ClearChildren();
    const std::vector<std::unique_ptr<Widget>>& Children() const { return children_; }
    Widget* Parent() const { return parent_; }
    Host* GetHost() const;
    void AttachHost(Host* host) { host_ = host; }

    // ---- Layout ---------------------------------------------------------
    // Preferred height when laid out at `width`.
    virtual float Measure(float width);
    // Preferred intrinsic width, for widgets placed in rows.
    virtual float MeasureWidth() { return 0; }
    virtual void Arrange(const RectF& rect);
    const RectF& Bounds() const { return bounds_; }

    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // ---- Painting -------------------------------------------------------
    virtual void Paint(Canvas& canvas);
    void PaintChildren(Canvas& canvas);

    // ---- Input ----------------------------------------------------------
    virtual Widget* HitTest(PointF point);
    virtual bool IsInteractive() const { return false; }
    virtual bool IsFocusable() const { return false; }
    virtual HCURSOR Cursor() const;
    virtual void OnMouseEnter() {}
    virtual void OnMouseLeave() {}
    virtual void OnMouseMove(PointF) {}
    virtual void OnMouseDown(PointF, MouseButton) {}
    virtual void OnMouseUp(PointF, MouseButton) {}
    virtual void OnDoubleClick(PointF) {}
    // Returns true when handled; unhandled events bubble to the parent.
    virtual bool OnMouseWheel(float) { return false; }
    virtual bool OnKeyDown(UINT, bool, bool) { return false; }
    virtual void OnChar(wchar_t) {}
    virtual void OnFocusChanged(bool) {}
    virtual void OnCaptureLost() {}
    // Offset applied to children (scrolling), in this widget's space.
    virtual PointF ChildOffset() const { return PointF{0, 0}; }
    // Scrolls ancestors so that `rect` (in this widget's space) is visible.
    virtual void BringIntoView(const RectF& rect);

    PointF ToWindow(PointF point) const;
    RectF ToWindow(const RectF& rect) const;

    bool IsHovered() const { return hovered_; }
    bool IsPressed() const { return pressed_; }
    bool HasFocus() const { return focused_; }
    void SetHovered(bool value) { hovered_ = value; }
    void SetPressed(bool value) { pressed_ = value; }
    void SetFocusedFlag(bool value) { focused_ = value; }

    void Invalidate();
    void InvalidateLayout();
    void RequestFrame();
    void Focus();

protected:
    RectF bounds_{};
    Widget* parent_ = nullptr;
    Host* host_ = nullptr;
    std::vector<std::unique_ptr<Widget>> children_;
    bool visible_ = true;
    bool enabled_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;
};

// Draws the Fluent keyboard focus rectangle around `rect`.
void PaintFocusVisual(Canvas& canvas, const RectF& rect, float radius);

}  // namespace nc::ui
