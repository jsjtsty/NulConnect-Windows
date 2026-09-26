#include "pch.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "core/Localization.h"

namespace nc::ui {

Color ResolveTextColor(TextColor color) {
    const Palette& p = Theme::Current();
    switch (color) {
    case TextColor::Primary: return p.textPrimary;
    case TextColor::Secondary: return p.textSecondary;
    case TextColor::Tertiary: return p.textTertiary;
    case TextColor::Disabled: return p.textDisabled;
    case TextColor::Accent: return p.accentText;
    case TextColor::Success: return p.success;
    case TextColor::Caution: return p.caution;
    case TextColor::Critical: return p.critical;
    case TextColor::OnAccent: return p.textOnAccent;
    }
    return p.textPrimary;
}

// ---- TextBlock -------------------------------------------------------------------

TextBlock::TextBlock(std::wstring text, TextStyle style, TextColor color)
    : text_(std::move(text)), style_(style), color_(color) {}

void TextBlock::SetText(std::wstring text) {
    if (text == text_) return;
    text_ = std::move(text);
    layout_.Reset();
    InvalidateLayout();
}

void TextBlock::SetStyle(TextStyle style) {
    style_ = style;
    layout_.Reset();
    InvalidateLayout();
}

void TextBlock::SetColor(TextColor color) {
    color_ = color;
    Invalidate();
}

void TextBlock::SetCustomColor(std::optional<Color> color) {
    customColor_ = color;
    Invalidate();
}

void TextBlock::SetAlignment(DWRITE_TEXT_ALIGNMENT alignment) {
    alignment_ = alignment;
    layout_.Reset();
    Invalidate();
}

void TextBlock::SetWrap(bool wrap) {
    wrap_ = wrap;
    layout_.Reset();
    InvalidateLayout();
}

void TextBlock::SetMaxLines(int lines) {
    maxLines_ = lines;
    layout_.Reset();
    InvalidateLayout();
}

IDWriteTextLayout* TextBlock::LayoutFor(float width) {
    if (!layout_ || layoutWidth_ != width) {
        layout_ = Graphics::Layout(text_, style_, width, wrap_, alignment_);
        layoutWidth_ = width;
    }
    return layout_.Get();
}

float TextBlock::Measure(float width) {
    if (text_.empty()) return 0;
    IDWriteTextLayout* layout = LayoutFor(width);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    if (maxLines_ > 0 && static_cast<int>(metrics.lineCount) > maxLines_) {
        return metrics.height / metrics.lineCount * maxLines_;
    }
    return std::ceil(metrics.height);
}

float TextBlock::MeasureWidth() {
    return std::ceil(Graphics::Measure(text_, style_, 0, false).width) + 1;
}

void TextBlock::Paint(Canvas& canvas) {
    if (text_.empty()) return;
    IDWriteTextLayout* layout = LayoutFor(Width(bounds_));
    Color color = customColor_ ? *customColor_ : ResolveTextColor(IsEnabled() ? color_ : TextColor::Disabled);
    if (maxLines_ > 0) {
        canvas.PushClip(bounds_);
        canvas.TextLayout(layout, PointF{bounds_.left, bounds_.top}, color);
        canvas.PopClip();
    } else {
        canvas.TextLayout(layout, PointF{bounds_.left, bounds_.top}, color);
    }
}

// ---- Button ---------------------------------------------------------------------

Button::Button(std::wstring text, ButtonStyle style, Icon icon) : text_(std::move(text)), style_(style), icon_(icon) {}

void Button::SetText(std::wstring text) {
    if (text == text_) return;
    text_ = std::move(text);
    InvalidateLayout();
}

void Button::SetIcon(Icon icon) {
    icon_ = icon;
    InvalidateLayout();
}

void Button::SetStyle(ButtonStyle style) {
    style_ = style;
    Invalidate();
}

void Button::SetLarge(bool large) {
    large_ = large;
    InvalidateLayout();
}

float Button::Measure(float) {
    return large_ ? 40.0f : 32.0f;
}

float Button::MeasureWidth() {
    if (fixedWidth_ > 0) return fixedWidth_;
    float padding = style_ == ButtonStyle::Hyperlink ? 8.0f : (large_ ? 20.0f : 12.0f);
    float width = padding * 2;
    TextStyle textStyle = large_ ? TextStyle::BodyStrong : TextStyle::Body;
    float textWidth = text_.empty() ? 0 : std::ceil(Graphics::Measure(text_, textStyle, 0, false).width);
    if (icon_ != Icon::None) {
        width += 16;
        if (!text_.empty()) width += 8;
    }
    width += textWidth;
    if (text_.empty() && icon_ != Icon::None) width = large_ ? 40.0f : 32.0f;
    return std::max(width, minWidth_);
}

HCURSOR Button::Cursor() const {
    return LoadCursorW(nullptr, style_ == ButtonStyle::Hyperlink ? IDC_HAND : IDC_ARROW);
}

void Button::OnMouseEnter() {
    hover_.Set(1);
    Invalidate();
}

void Button::OnMouseLeave() {
    hover_.Set(0);
    press_.Set(0);
    Invalidate();
}

void Button::OnMouseDown(PointF, MouseButton button) {
    if (button == MouseButton::Left) press_.Set(1, 0.05);
}

void Button::OnMouseUp(PointF point, MouseButton button) {
    press_.Set(0);
    if (button == MouseButton::Left && Contains(bounds_, point)) Click();
}

bool Button::OnKeyDown(UINT key, bool, bool) {
    if (key == VK_SPACE || key == VK_RETURN) {
        Click();
        return true;
    }
    return false;
}

void Button::Click() {
    if (!IsEnabled()) return;
    if (onClick) {
        // Copy: the handler may destroy this button (e.g. rebuilding a page).
        auto handler = onClick;
        handler();
    }
}

void Button::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    bool enabled = IsEnabled();
    float hover = hover_.Value(this);
    float press = press_.Value(this);
    RectF rect = bounds_;
    float radius = Theme::ControlRadius;
    Color fill{}, text{};
    bool border = false;

    switch (style_) {
    case ButtonStyle::Accent:
    case ButtonStyle::Danger: {
        Color base = style_ == ButtonStyle::Danger ? (p.dark ? Rgba(0xFF99A4) : Rgba(0xC42B1C)) : p.accent;
        if (!enabled) {
            fill = p.accentDisabled;
            text = p.textOnAccentDisabled;
        } else {
            fill = Mix(base, WithAlpha(base, 0.9f), hover);
            fill = Mix(fill, WithAlpha(base, 0.8f), press);
            float luminance = 0.2126f * base.r + 0.7152f * base.g + 0.0722f * base.b;
            text = luminance > 0.5f ? Rgba(0x000000) : Rgba(0xFFFFFF);
            if (press > 0) text = WithAlpha(text, 1.0f - 0.2f * press);
        }
        canvas.FillRoundRect(rect, radius, fill);
        if (enabled) {
            canvas.ControlBorder(rect, radius, Rgba(0xFFFFFF, 0.08f), Rgba(0x000000, p.dark ? 0.14f : 0.4f));
        }
        break;
    }
    case ButtonStyle::Standard:
        fill = enabled ? Mix(Mix(p.controlFill, p.controlFillHover, hover), p.controlFillPressed, press) : p.controlFillDisabled;
        text = enabled ? (press > 0.5f ? p.textSecondary : p.textPrimary) : p.textDisabled;
        if (!p.translucent && !p.dark) fill = enabled ? Mix(Mix(Rgba(0xFEFEFE), Rgba(0xF6F6F6), hover), Rgba(0xF1F1F1), press) : Rgba(0xF5F5F5);
        if (!p.translucent && p.dark) fill = enabled ? Mix(Mix(Rgba(0x2D2D2D), Rgba(0x323232), hover), Rgba(0x272727), press) : Rgba(0x2A2A2A);
        canvas.FillRoundRect(rect, radius, fill);
        border = true;
        break;
    case ButtonStyle::Subtle:
        fill = Mix(WithAlpha(p.subtleHover, 0), p.subtleHover, hover);
        fill = Mix(fill, p.subtlePressed, press);
        text = enabled ? (press > 0.5f ? p.textSecondary : p.textPrimary) : p.textDisabled;
        canvas.FillRoundRect(rect, radius, fill);
        break;
    case ButtonStyle::Hyperlink:
        fill = Mix(WithAlpha(p.subtleHover, 0), p.subtleHover, hover);
        fill = Mix(fill, p.subtlePressed, press);
        text = enabled ? (press > 0.5f ? WithAlpha(p.accentText, 0.8f) : p.accentText) : p.textDisabled;
        canvas.FillRoundRect(rect, radius, fill);
        break;
    }
    if (border) canvas.ControlBorder(rect, radius, p.controlStroke, p.controlStrokeBottom);

    TextStyle textStyle = large_ ? TextStyle::BodyStrong : TextStyle::Body;
    float textWidth = text_.empty() ? 0 : std::ceil(Graphics::Measure(text_, textStyle, 0, false).width);
    float contentWidth = textWidth + (icon_ != Icon::None ? (text_.empty() ? 16.0f : 24.0f) : 0.0f);
    float x = rect.left + (Width(rect) - contentWidth) / 2;
    x = std::max(x, rect.left + 8);
    if (icon_ != Icon::None) {
        DrawIcon(canvas, icon_, MakeRect(x, rect.top, 16, Height(rect)), text, 16);
        x += text_.empty() ? 16 : 24;
    }
    if (!text_.empty()) {
        canvas.Text(text_, textStyle, RectF{x, rect.top, rect.right - 6, rect.bottom}, text, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    if (focused_ && GetHost() && GetHost()->KeyboardFocusVisible()) PaintFocusVisual(canvas, rect, radius);
}

// ---- ToggleSwitch --------------------------------------------------------------

ToggleSwitch::ToggleSwitch(bool on) : on_(on), position_(on ? 1.0f : 0.0f) {}

void ToggleSwitch::SetOn(bool on, bool animate) {
    if (on_ == on) return;
    on_ = on;
    if (animate) position_.Set(on ? 1.0f : 0.0f, Motion::Normal);
    else position_.Jump(on ? 1.0f : 0.0f);
    Invalidate();
}

float ToggleSwitch::MeasureWidth() {
    if (!showStateText_) return 40;
    float on = Graphics::Measure(Tr(L"On"), TextStyle::Body, 0, false).width;
    float off = Graphics::Measure(Tr(L"Off"), TextStyle::Body, 0, false).width;
    return std::ceil(std::max(on, off)) + 12 + 40;
}

RectF ToggleSwitch::TrackRect() const {
    float top = bounds_.top + (Height(bounds_) - 20) / 2;
    return MakeRect(bounds_.right - 40, top, 40, 20);
}

HCURSOR ToggleSwitch::Cursor() const {
    return LoadCursorW(nullptr, IDC_ARROW);
}

void ToggleSwitch::OnMouseDown(PointF, MouseButton button) {
    if (button == MouseButton::Left) press_.Set(1);
}

void ToggleSwitch::OnMouseUp(PointF point, MouseButton button) {
    press_.Set(0);
    if (button == MouseButton::Left && Contains(bounds_, point)) Toggle();
}

bool ToggleSwitch::OnKeyDown(UINT key, bool, bool) {
    if (key == VK_SPACE || key == VK_RETURN) {
        Toggle();
        return true;
    }
    return false;
}

void ToggleSwitch::Toggle() {
    if (!IsEnabled()) return;
    SetOn(!on_);
    if (onChange) {
        auto handler = onChange;
        handler(on_);
    }
}

void ToggleSwitch::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    bool enabled = IsEnabled();
    float t = position_.Value(this);
    float hover = hover_.Value(this);
    float press = press_.Value(this);
    RectF track = TrackRect();
    Color accent = dangerous_ ? p.critical : p.accent;
    Color onFill = enabled ? Mix(accent, WithAlpha(accent, 0.9f), hover) : p.accentDisabled;
    Color offFill = enabled ? Mix(Rgba(0, 0), p.subtleHover, hover) : Rgba(0, 0);
    Color offStroke = enabled ? p.controlStrongStroke : p.textDisabled;
    canvas.FillRoundRect(track, 10, Mix(offFill, onFill, t));
    if (t < 1) canvas.StrokeRoundRect(track, 10, WithAlpha(offStroke, offStroke.a * (1 - t)));

    float knobW = 12 + 2 * hover + 3 * press;
    float knobH = 12 + 2 * hover;
    float travel = Width(track) - 8 - knobW;
    float cx = track.left + 4 + knobW / 2 + travel * t;
    float cy = (track.top + track.bottom) / 2;
    Color knobOff = enabled ? p.controlStrongFill : p.textDisabled;
    Color knobOn = enabled ? (dangerous_ ? Rgba(p.dark ? 0x000000 : 0xFFFFFF) : p.textOnAccent) : p.textOnAccentDisabled;
    canvas.FillRoundRect(MakeRect(cx - knobW / 2, cy - knobH / 2, knobW, knobH), knobH / 2, Mix(knobOff, knobOn, t));

    if (showStateText_) {
        RectF textRect{bounds_.left, bounds_.top, track.left - 12, bounds_.bottom};
        canvas.Text(on_ ? Tr(L"On") : Tr(L"Off"), TextStyle::Body, textRect, enabled ? p.textPrimary : p.textDisabled,
                    DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    if (focused_ && GetHost() && GetHost()->KeyboardFocusVisible()) PaintFocusVisual(canvas, track, 10);
}

// ---- Segmented -----------------------------------------------------------------

Segmented::Segmented(std::vector<std::wstring> options) : options_(std::move(options)) {}

void Segmented::SetSelected(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(options_.size())) return;
    selected_ = index;
    if (animate) indicator_.Set(static_cast<float>(index), Motion::Normal);
    else indicator_.Jump(static_cast<float>(index));
    Invalidate();
}

float Segmented::MeasureWidth() {
    float widest = 0;
    for (const auto& option : options_) {
        widest = std::max(widest, Graphics::Measure(option, TextStyle::Body, 0, false).width);
    }
    return (std::ceil(widest) + 32) * static_cast<float>(options_.size()) + 4;
}

RectF Segmented::SegmentRect(int index) const {
    float width = (Width(bounds_) - 4) / static_cast<float>(std::max<size_t>(1, options_.size()));
    return MakeRect(bounds_.left + 2 + width * static_cast<float>(index), bounds_.top + 2, width, Height(bounds_) - 4);
}

int Segmented::IndexAt(PointF point) const {
    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        if (Contains(SegmentRect(i), point)) return i;
    }
    return -1;
}

HCURSOR Segmented::Cursor() const {
    return LoadCursorW(nullptr, IDC_ARROW);
}

void Segmented::OnMouseMove(PointF point) {
    int hot = IndexAt(point);
    if (hot != hot_) {
        hot_ = hot;
        Invalidate();
    }
}

void Segmented::OnMouseUp(PointF point, MouseButton button) {
    int index = IndexAt(point);
    if (button != MouseButton::Left || index < 0 || index == selected_ || !IsEnabled()) return;
    SetSelected(index);
    if (onChange) {
        auto handler = onChange;
        handler(index);
    }
}

bool Segmented::OnKeyDown(UINT key, bool, bool) {
    int next = selected_;
    if (key == VK_LEFT || key == VK_UP) next = std::max(0, selected_ - 1);
    else if (key == VK_RIGHT || key == VK_DOWN) next = std::min(static_cast<int>(options_.size()) - 1, selected_ + 1);
    else return false;
    if (next != selected_) {
        SetSelected(next);
        if (onChange) {
            auto handler = onChange;
            handler(next);
        }
    }
    return true;
}

void Segmented::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    bool enabled = IsEnabled();
    canvas.FillRoundRect(bounds_, Theme::ControlRadius + 1, p.dark ? Rgba(0x000000, 0.18f) : Rgba(0x000000, 0.04f));
    canvas.StrokeRoundRect(bounds_, Theme::ControlRadius + 1, p.controlStroke);
    float position = indicator_.Value(this);
    RectF first = SegmentRect(0);
    float width = Width(first);
    RectF indicator = Offset(first, width * position, 0);
    Color accent = selected_ == dangerIndex_ ? p.critical : p.accent;
    canvas.FillRoundRect(indicator, Theme::ControlRadius, enabled ? accent : p.accentDisabled);
    for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
        RectF segment = SegmentRect(i);
        if (i == hot_ && i != selected_ && enabled) canvas.FillRoundRect(segment, Theme::ControlRadius, p.subtleHover);
        float selectedWeight = std::max(0.0f, 1.0f - std::fabs(position - static_cast<float>(i)));
        Color accentText = selected_ == dangerIndex_ ? (p.dark ? Rgba(0x000000) : Rgba(0xFFFFFF)) : p.textOnAccent;
        Color text = enabled ? Mix(p.textPrimary, accentText, selectedWeight) : p.textDisabled;
        canvas.Text(options_[static_cast<size_t>(i)], i == selected_ ? TextStyle::BodyStrong : TextStyle::Body, segment, text,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }
    if (focused_ && GetHost() && GetHost()->KeyboardFocusVisible()) PaintFocusVisual(canvas, bounds_, Theme::ControlRadius);
}

// ---- StackPanel ----------------------------------------------------------------

StackPanel::StackPanel(Orientation orientation, float spacing) : orientation_(orientation), spacing_(spacing) {}

void StackPanel::SetPadding(float left, float top, float right, float bottom) {
    padLeft_ = left;
    padTop_ = top;
    padRight_ = right;
    padBottom_ = bottom;
}

float StackPanel::Measure(float width) {
    float inner = width - padLeft_ - padRight_;
    if (maxWidth_ > 0) inner = std::min(inner, maxWidth_);
    float total = 0;
    int count = 0;
    if (orientation_ == Orientation::Vertical) {
        for (auto& child : children_) {
            if (!child->IsVisible()) continue;
            total += child->Measure(inner);
            ++count;
        }
        if (count > 1) total += spacing_ * static_cast<float>(count - 1);
    } else {
        for (auto& child : children_) {
            if (!child->IsVisible()) continue;
            float childWidth = child->MeasureWidth();
            total = std::max(total, child->Measure(childWidth > 0 ? childWidth : inner));
        }
    }
    return total + padTop_ + padBottom_;
}

float StackPanel::MeasureWidth() {
    float total = 0;
    int count = 0;
    for (auto& child : children_) {
        if (!child->IsVisible()) continue;
        float w = child->MeasureWidth();
        if (orientation_ == Orientation::Horizontal) {
            total += w;
            ++count;
        } else {
            total = std::max(total, w);
        }
    }
    if (orientation_ == Orientation::Horizontal && count > 1) total += spacing_ * static_cast<float>(count - 1);
    return total + padLeft_ + padRight_;
}

void StackPanel::Arrange(const RectF& rect) {
    bounds_ = rect;
    float left = rect.left + padLeft_;
    float right = rect.right - padRight_;
    float inner = right - left;
    if (maxWidth_ > 0 && inner > maxWidth_) {
        left += (inner - maxWidth_) / 2;
        inner = maxWidth_;
        right = left + inner;
    }
    if (orientation_ == Orientation::Vertical) {
        float y = rect.top + padTop_;
        for (auto& child : children_) {
            if (!child->IsVisible()) continue;
            float height = child->Measure(inner);
            float x = left;
            float width = inner;
            if (alignment_ != Alignment::Stretch) {
                float desired = child->MeasureWidth();
                if (desired > 0 && desired < inner) {
                    width = desired;
                    if (alignment_ == Alignment::Center) x = left + (inner - desired) / 2;
                    else if (alignment_ == Alignment::End) x = right - desired;
                }
            }
            child->Arrange(MakeRect(x, y, width, height));
            y += height + spacing_;
        }
        return;
    }
    // Horizontal: fixed-width children take their width; zero-width ones
    // share the remainder.
    float fixed = 0;
    int flexible = 0;
    int count = 0;
    for (auto& child : children_) {
        if (!child->IsVisible()) continue;
        float w = child->MeasureWidth();
        if (w > 0) fixed += w;
        else ++flexible;
        ++count;
    }
    float gaps = count > 1 ? spacing_ * static_cast<float>(count - 1) : 0;
    float flexWidth = flexible ? std::max(0.0f, (inner - fixed - gaps) / static_cast<float>(flexible)) : 0;
    float used = fixed + gaps + flexWidth * static_cast<float>(flexible);
    float x = left;
    if (!flexible && alignment_ == Alignment::End) x = right - used;
    else if (!flexible && alignment_ == Alignment::Center) x = left + (inner - used) / 2;
    float innerHeight = Height(rect) - padTop_ - padBottom_;
    for (auto& child : children_) {
        if (!child->IsVisible()) continue;
        float w = child->MeasureWidth();
        if (w <= 0) w = flexWidth;
        float h = std::min(innerHeight, child->Measure(w));
        float y = rect.top + padTop_ + (innerHeight - h) / 2;
        child->Arrange(MakeRect(x, y, w, h));
        x += w + spacing_;
    }
}

// ---- ScrollView ----------------------------------------------------------------

ScrollView::ScrollView() = default;

Widget* ScrollView::SetContent(std::unique_ptr<Widget> content) {
    ClearChildren();
    Widget* raw = content.get();
    AddChild(std::move(content));
    return raw;
}

float ScrollView::MaxScroll() const {
    return std::max(0.0f, contentHeight_ - Height(bounds_));
}

PointF ScrollView::ChildOffset() const {
    return PointF{0, std::round(scroll_.Value(const_cast<ScrollView*>(this)))};
}

void ScrollView::ScrollTo(float offset, bool animate) {
    offset = std::clamp(offset, 0.0f, MaxScroll());
    if (animate) scroll_.Set(offset, Motion::Normal);
    else scroll_.Jump(offset);
    Invalidate();
}

void ScrollView::Arrange(const RectF& rect) {
    bounds_ = rect;
    Widget* content = Content();
    if (!content) return;
    float width = Width(rect);
    contentHeight_ = content->Measure(width);
    content->Arrange(MakeRect(rect.left, rect.top, width, contentHeight_));
    float max = MaxScroll();
    if (scroll_.Target() > max) scroll_.Jump(max);
}

bool ScrollView::OnMouseWheel(float delta) {
    if (MaxScroll() <= 0) return false;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    float step = lines == WHEEL_PAGESCROLL ? Height(bounds_) : 16.0f * static_cast<float>(std::max<UINT>(1, lines)) * 1.6f;
    ScrollTo(scroll_.Target() - delta * step);
    barHover_.Set(1, Motion::Normal);
    return true;
}

RectF ScrollView::BarRect() const {
    return RectF{bounds_.right - 12, bounds_.top + 2, bounds_.right - 2, bounds_.bottom - 2};
}

RectF ScrollView::ThumbRect(float width) const {
    RectF bar = BarRect();
    float track = Height(bar);
    float viewport = Height(bounds_);
    float thumb = std::max(24.0f, track * viewport / std::max(viewport, contentHeight_));
    float max = MaxScroll();
    float position = max > 0 ? scroll_.Value(nullptr) / max : 0;
    float top = bar.top + (track - thumb) * position;
    return RectF{bar.right - 4 - width / 2 - 1, top, bar.right - 4 + width / 2 - 1, top + thumb};
}

Widget* ScrollView::HitTest(PointF point) {
    if (!visible_ || !Contains(bounds_, point)) return nullptr;
    if (MaxScroll() > 0 && Contains(BarRect(), point)) return this;
    PointF offset = ChildOffset();
    PointF childPoint{point.x + offset.x, point.y + offset.y};
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (Widget* hit = (*it)->HitTest(childPoint)) return hit;
    }
    return nullptr;
}

void ScrollView::OnMouseDown(PointF point, MouseButton button) {
    if (button != MouseButton::Left || MaxScroll() <= 0) return;
    RectF thumb = ThumbRect(6);
    if (point.y >= thumb.top && point.y <= thumb.bottom) {
        dragging_ = true;
        dragOrigin_ = point.y;
        dragScroll_ = scroll_.Target();
    } else {
        ScrollTo(scroll_.Target() + (point.y < thumb.top ? -1 : 1) * Height(bounds_) * 0.9f);
    }
}

void ScrollView::OnMouseMove(PointF point) {
    if (!dragging_) return;
    RectF bar = BarRect();
    float thumb = Height(ThumbRect(6));
    float track = Height(bar) - thumb;
    if (track <= 0) return;
    ScrollTo(dragScroll_ + (point.y - dragOrigin_) / track * MaxScroll(), false);
}

void ScrollView::BringIntoView(const RectF& rect) {
    float top = scroll_.Target();
    float viewTop = bounds_.top + top;
    float viewBottom = viewTop + Height(bounds_);
    if (rect.top - 12 < viewTop) ScrollTo(rect.top - 12 - bounds_.top);
    else if (rect.bottom + 12 > viewBottom) ScrollTo(rect.bottom + 12 - bounds_.top - Height(bounds_));
    Widget::BringIntoView(rect);
}

void ScrollView::Paint(Canvas& canvas) {
    canvas.PushClip(bounds_);
    PaintChildren(canvas);
    canvas.PopClip();
    if (MaxScroll() <= 0) return;
    const Palette& p = Theme::Current();
    float hover = barHover_.Value(this);
    if (hovered_ || dragging_) hover = 1;
    float width = 2 + 4 * hover;
    if (hover > 0.5f) {
        canvas.FillRoundRect(BarRect(), 5, WithAlpha(p.dark ? Rgba(0x2C2C2C) : Rgba(0xF9F9F9), 0.8f * hover));
    }
    RectF thumb = ThumbRect(width);
    canvas.FillRoundRect(thumb, width / 2, WithAlpha(p.controlStrongFill, p.controlStrongFill.a * (0.6f + 0.4f * hover)));
}

// ---- SettingsCard -----------------------------------------------------------------

SettingsCard::SettingsCard(Icon icon, std::wstring title, std::wstring description)
    : icon_(icon), title_(std::move(title)), description_(std::move(description)) {}

void SettingsCard::SetTitle(std::wstring title) {
    if (title == title_) return;
    title_ = std::move(title);
    InvalidateLayout();
}

void SettingsCard::SetDescription(std::wstring description) {
    if (description == description_) return;
    description_ = std::move(description);
    InvalidateLayout();
}

void SettingsCard::SetClickable(bool clickable, Icon trailingGlyph) {
    clickable_ = clickable;
    trailingGlyph_ = trailingGlyph;
    Invalidate();
}

HCURSOR SettingsCard::Cursor() const {
    return LoadCursorW(nullptr, IDC_ARROW);
}

SettingsCard::LayoutInfo SettingsCard::ComputeLayout(float width) {
    LayoutInfo info{};
    float left = icon_ != Icon::None ? 52.0f : 16.0f;
    Widget* trailing = Trailing();
    float trailingWidth = 0;
    if (trailing && trailing->IsVisible()) trailingWidth = trailing->MeasureWidth();
    if (clickable_) trailingWidth += (trailingWidth > 0 ? 12 : 0) + 16;
    float available = width - left - 16;
    info.textLeft = left;
    info.trailingWidth = trailingWidth;
    // Stack the control under the text when it would squeeze the text.
    info.stacked = trailing && trailing->IsVisible() && trailingWidth > available * 0.55f;
    info.textWidth = info.stacked ? available : std::max(40.0f, available - (trailingWidth > 0 ? trailingWidth + 16 : 0));
    return info;
}

float SettingsCard::Measure(float width) {
    layout_ = ComputeLayout(width);
    titleHeight_ = title_.empty() ? 0 : Graphics::Measure(title_, TextStyle::Body, layout_.textWidth).height;
    descriptionHeight_ = description_.empty() ? 0 : Graphics::Measure(description_, TextStyle::Caption, layout_.textWidth).height;
    float textHeight = titleHeight_ + descriptionHeight_;
    Widget* trailing = Trailing();
    float trailingHeight = trailing && trailing->IsVisible() ? trailing->Measure(layout_.trailingWidth) : 0;
    float minHeight = description_.empty() ? 48.0f : 68.0f;
    if (layout_.stacked) return std::max(minHeight, 13 + textHeight + 10 + trailingHeight + 13);
    return std::max(minHeight, std::max(textHeight, trailingHeight) + 26);
}

void SettingsCard::Arrange(const RectF& rect) {
    bounds_ = rect;
    Measure(Width(rect));
    Widget* trailing = Trailing();
    if (!trailing) return;
    float controlWidth = trailing->MeasureWidth();
    float height = trailing->Measure(controlWidth);
    float right = rect.right - 16 - (clickable_ ? 28 : 0);
    if (layout_.stacked) {
        // A control moved below the text takes the full text column.
        float width = layout_.textWidth;
        float y = rect.bottom - 13 - height;
        trailing->Arrange(MakeRect(rect.left + layout_.textLeft, y, width, height));
    } else {
        float y = rect.top + (Height(rect) - height) / 2;
        trailing->Arrange(MakeRect(right - controlWidth, y, controlWidth, height));
    }
}

Widget* SettingsCard::HitTest(PointF point) {
    if (!visible_ || !Contains(bounds_, point)) return nullptr;
    if (Widget* trailing = Trailing()) {
        if (Widget* hit = trailing->HitTest(point)) return hit;
    }
    return clickable_ ? this : nullptr;
}

void SettingsCard::OnMouseUp(PointF point, MouseButton button) {
    if (button == MouseButton::Left && Contains(bounds_, point) && onClick && IsEnabled()) {
        auto handler = onClick;
        handler();
    }
}

bool SettingsCard::OnKeyDown(UINT key, bool, bool) {
    if ((key == VK_SPACE || key == VK_RETURN) && clickable_ && onClick && IsEnabled()) {
        auto handler = onClick;
        handler();
        return true;
    }
    return false;
}

void SettingsCard::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    bool enabled = IsEnabled();
    float hover = clickable_ && enabled ? hover_.Value(this) : 0;
    Color fill = Mix(p.cardBackground, p.cardBackgroundHover, hover);
    if (clickable_ && pressed_ && enabled) fill = p.cardBackgroundPressed;
    canvas.FillRoundRect(bounds_, Theme::ControlRadius, fill);
    canvas.StrokeRoundRect(bounds_, Theme::ControlRadius, p.cardStroke);

    float textTop;
    float textBlock = titleHeight_ + descriptionHeight_;
    if (layout_.stacked) textTop = bounds_.top + 13;
    else textTop = bounds_.top + (Height(bounds_) - textBlock) / 2;
    if (icon_ != Icon::None) {
        float iconCenter = layout_.stacked ? textTop + titleHeight_ / 2 : (bounds_.top + bounds_.bottom) / 2;
        DrawIcon(canvas, icon_, MakeRect(bounds_.left + 16, iconCenter - 10, 20, 20),
                 enabled ? ResolveTextColor(titleColor_) : p.textDisabled, 20);
    }
    float left = bounds_.left + layout_.textLeft;
    if (!title_.empty()) {
        canvas.Text(title_, TextStyle::Body, MakeRect(left, textTop, layout_.textWidth, titleHeight_ + 1),
                    enabled ? ResolveTextColor(titleColor_) : p.textDisabled);
    }
    if (!description_.empty()) {
        canvas.Text(description_, TextStyle::Caption, MakeRect(left, textTop + titleHeight_, layout_.textWidth, descriptionHeight_ + 1),
                    enabled ? ResolveTextColor(descriptionColor_) : p.textDisabled);
    }
    if (clickable_) {
        DrawIcon(canvas, trailingGlyph_, RectF{bounds_.right - 44, bounds_.top, bounds_.right - 12, bounds_.bottom},
                 enabled ? p.textSecondary : p.textDisabled, 16);
    }
    PaintChildren(canvas);
    if (focused_ && GetHost() && GetHost()->KeyboardFocusVisible()) PaintFocusVisual(canvas, bounds_, Theme::ControlRadius);
}

// ---- Card ---------------------------------------------------------------------

Widget* Card::SetContent(std::unique_ptr<Widget> content) {
    ClearChildren();
    Widget* raw = content.get();
    AddChild(std::move(content));
    return raw;
}

float Card::Measure(float width) {
    float inner = 0;
    for (auto& child : children_) {
        if (child->IsVisible()) inner = std::max(inner, child->Measure(width - 2 * padding_));
    }
    return inner + 2 * padding_;
}

void Card::Arrange(const RectF& rect) {
    bounds_ = rect;
    for (auto& child : children_) child->Arrange(Inset(rect, padding_, padding_));
}

void Card::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    canvas.FillRoundRect(bounds_, Theme::OverlayRadius, p.cardBackground);
    canvas.StrokeRoundRect(bounds_, Theme::OverlayRadius, p.cardStroke);
    PaintChildren(canvas);
}

// ---- InfoBar ------------------------------------------------------------------

InfoBar::InfoBar(Severity severity, std::wstring title, std::wstring message)
    : severity_(severity), title_(std::move(title)), message_(std::move(message)) {}

void InfoBar::Set(Severity severity, std::wstring title, std::wstring message) {
    severity_ = severity;
    title_ = std::move(title);
    message_ = std::move(message);
    InvalidateLayout();
}

void InfoBar::SetClosable(bool closable) {
    if (closable && !closeButton_) {
        closeButton_ = Emplace<Button>(L"", ButtonStyle::Subtle, Icon::Close);
        closeButton_->SetTooltip(Tr(L"Close"));
        closeButton_->onClick = [this] {
            if (onClose) {
                auto handler = onClose;
                handler();
            }
        };
    } else if (!closable && closeButton_) {
        ClearChildren();
        closeButton_ = nullptr;
    }
}

float InfoBar::TextWidth(float width) const {
    return width - 48 - 16 - (closeButton_ ? 40.0f : 0.0f);
}

float InfoBar::Measure(float width) {
    std::wstring text = title_.empty() ? message_ : title_ + L"  " + message_;
    float height = Graphics::Measure(text, TextStyle::Body, TextWidth(width)).height;
    return std::max(48.0f, height + 28);
}

void InfoBar::Arrange(const RectF& rect) {
    bounds_ = rect;
    if (closeButton_) closeButton_->Arrange(MakeRect(rect.right - 40, rect.top + 8, 32, 32));
}

void InfoBar::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    Color background{}, iconColor{};
    Icon icon = Icon::Info;
    switch (severity_) {
    case Severity::Informational: background = p.infoBackground; iconColor = p.accent; icon = Icon::Info; break;
    case Severity::Success: background = p.successBackground; iconColor = p.success; icon = Icon::CheckCircle; break;
    case Severity::Warning: background = p.cautionBackground; iconColor = p.caution; icon = Icon::Warning; break;
    case Severity::Error: background = p.criticalBackground; iconColor = p.critical; icon = Icon::ErrorCircle; break;
    }
    canvas.FillRoundRect(bounds_, Theme::ControlRadius, background);
    canvas.StrokeRoundRect(bounds_, Theme::ControlRadius, p.cardStroke);
    DrawIcon(canvas, icon, MakeRect(bounds_.left + 14, bounds_.top + 14, 20, 20), iconColor, 18);

    float left = bounds_.left + 48;
    float width = TextWidth(Width(bounds_));
    std::wstring text = title_.empty() ? message_ : title_ + L"  " + message_;
    auto layout = Graphics::Layout(text, TextStyle::Body, width);
    if (!title_.empty()) {
        DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(title_.size())};
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    float top = bounds_.top + std::max(14.0f, (Height(bounds_) - metrics.height) / 2);
    canvas.TextLayout(layout.Get(), PointF{left, top}, p.textPrimary);
    PaintChildren(canvas);
}

// ---- ProgressRing ---------------------------------------------------------------

void ProgressRing::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    RequestFrame();
    float radius = (size_ - thickness_) / 2;
    PointF center{(bounds_.left + bounds_.right) / 2, (bounds_.top + bounds_.bottom) / 2};
    double t = Now();
    const float pi = 3.14159265f;
    float rotation = static_cast<float>(std::fmod(t * 1.4, 1.0)) * 2 * pi;
    float phase = static_cast<float>(std::fmod(t * 0.7, 1.0));
    float sweep = (0.12f + 0.6f * (0.5f - 0.5f * std::cos(phase * 2 * pi))) * 2 * pi;
    float start = rotation;
    float end = rotation + sweep;
    ComPtr<ID2D1PathGeometry> path;
    Graphics::D2D()->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(PointF{center.x + radius * std::cos(start), center.y + radius * std::sin(start)}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(PointF{center.x + radius * std::cos(end), center.y + radius * std::sin(end)},
                                  D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  sweep > pi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    canvas.Target()->DrawGeometry(path.Get(), canvas.Brush(color_ ? *color_ : p.accent), thickness_, Graphics::RoundStroke());
}

}  // namespace nc::ui
