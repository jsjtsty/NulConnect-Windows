#include "pch.h"
#include "ui/Widget.h"
#include "ui/Host.h"

namespace nc::ui {

namespace {

float EaseOut(float t) {
    // Approximates the Fluent "decelerate" curve cubic-bezier(0, 0, 0, 1).
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv * inv;
}

}  // namespace

void Animated::Set(float target, double duration) {
    if (target == to_) return;
    from_ = Value(nullptr);
    to_ = target;
    start_ = Now();
    duration_ = duration;
}

void Animated::Jump(float value) {
    from_ = to_ = value;
    duration_ = 0;
}

float Animated::Value(Widget* requester) {
    if (duration_ <= 0) return to_;
    double elapsed = Now() - start_;
    if (elapsed >= duration_) {
        duration_ = 0;
        from_ = to_;
        return to_;
    }
    if (requester) requester->RequestFrame();
    float t = static_cast<float>(elapsed / duration_);
    return from_ + (to_ - from_) * EaseOut(t);
}

Widget::~Widget() {
    // Children first, so the host forgets them while the tree is intact.
    children_.clear();
    if (Host* host = GetHost()) {
        host->ForgetWidget(this);
    }
}

void Widget::AddChild(std::unique_ptr<Widget> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
    InvalidateLayout();
}

void Widget::ClearChildren() {
    children_.clear();
    InvalidateLayout();
}

Host* Widget::GetHost() const {
    const Widget* widget = this;
    while (widget) {
        if (widget->host_) return widget->host_;
        widget = widget->parent_;
    }
    return nullptr;
}

float Widget::Measure(float width) {
    float height = 0;
    for (auto& child : children_) {
        if (child->IsVisible()) height = std::max(height, child->Measure(width));
    }
    return height;
}

void Widget::Arrange(const RectF& rect) {
    bounds_ = rect;
    for (auto& child : children_) {
        child->Arrange(rect);
    }
}

void Widget::SetVisible(bool visible) {
    if (visible_ == visible) return;
    visible_ = visible;
    InvalidateLayout();
}

void Widget::SetEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    Invalidate();
}

bool Widget::IsEnabled() const {
    for (const Widget* widget = this; widget; widget = widget->parent_) {
        if (!widget->enabled_) return false;
    }
    return true;
}

void Widget::Paint(Canvas& canvas) {
    PaintChildren(canvas);
}

void Widget::PaintChildren(Canvas& canvas) {
    PointF offset = ChildOffset();
    bool translated = offset.x != 0 || offset.y != 0;
    if (translated) canvas.PushTranslate(-offset.x, -offset.y);
    for (auto& child : children_) {
        if (child->IsVisible()) child->Paint(canvas);
    }
    if (translated) canvas.PopTransform();
}

Widget* Widget::HitTest(PointF point) {
    if (!visible_ || !Contains(bounds_, point)) return nullptr;
    PointF offset = ChildOffset();
    PointF childPoint{point.x + offset.x, point.y + offset.y};
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(childPoint)) return hit;
    }
    return IsInteractive() ? this : nullptr;
}

HCURSOR Widget::Cursor() const {
    return LoadCursorW(nullptr, IDC_ARROW);
}

void Widget::BringIntoView(const RectF& rect) {
    if (parent_) {
        PointF offset = parent_->ChildOffset();
        parent_->BringIntoView(Offset(rect, -offset.x, -offset.y));
    }
}

PointF Widget::ToWindow(PointF point) const {
    for (const Widget* ancestor = parent_; ancestor; ancestor = ancestor->parent_) {
        PointF offset = ancestor->ChildOffset();
        point.x -= offset.x;
        point.y -= offset.y;
    }
    return point;
}

RectF Widget::ToWindow(const RectF& rect) const {
    PointF topLeft = ToWindow(PointF{rect.left, rect.top});
    return MakeRect(topLeft.x, topLeft.y, Width(rect), Height(rect));
}

void Widget::Invalidate() {
    if (Host* host = GetHost()) host->Invalidate();
}

void Widget::InvalidateLayout() {
    if (Host* host = GetHost()) host->InvalidateLayout();
}

void Widget::RequestFrame() {
    if (Host* host = GetHost()) host->RequestFrame();
}

void Widget::Focus() {
    if (Host* host = GetHost()) host->SetFocus(this);
}

void PaintFocusVisual(Canvas& canvas, const RectF& rect, float radius) {
    const Palette& palette = Theme::Current();
    canvas.StrokeRoundRect(Inset(rect, -3, -3), radius + 3, palette.focusStroke, 2.0f);
    canvas.StrokeRoundRect(Inset(rect, -1, -1), radius + 1, palette.dark ? Rgba(0x000000, 0.7f) : Rgba(0xFFFFFF, 0.7f), 1.0f);
}

}  // namespace nc::ui
