#include "pch.h"
#include "ui/Navigation.h"
#include "ui/Host.h"

namespace nc::ui {

namespace {
constexpr float kItemHeight = 36.0f;
constexpr float kItemSpacing = 4.0f;
constexpr float kMargin = 4.0f;
}  // namespace

NavigationView::NavigationView() = default;

void NavigationView::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    InvalidateLayout();
}

void NavigationView::SetItemText(int id, std::wstring text) {
    int index = IndexOf(id);
    if (index >= 0) items_[static_cast<size_t>(index)].text = std::move(text);
    Invalidate();
}

int NavigationView::IndexOf(int id) const {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void NavigationView::Select(int id, bool animate) {
    int index = IndexOf(id);
    if (index < 0) return;
    selected_ = id;
    focusIndex_ = index;
    if (!itemTops_.empty()) {
        float top = itemTops_[static_cast<size_t>(index)];
        if (animate) {
            indicatorTop_.Set(top, Motion::Normal);
            indicatorStretch_.Jump(1.0f);
            indicatorStretch_.Set(0.0f, Motion::Slow);
        } else {
            indicatorTop_.Jump(top);
        }
    }
    Invalidate();
}

void NavigationView::SetCompact(bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    if (header_) header_->SetVisible(!compact);
    InvalidateLayout();
}

Widget* NavigationView::SetHeader(std::unique_ptr<Widget> header) {
    ClearChildren();
    header_ = header.get();
    header_->SetVisible(!compact_);
    AddChild(std::move(header));
    return header_;
}

void NavigationView::Arrange(const RectF& rect) {
    bounds_ = rect;
    float y = rect.top + 8;
    headerHeight_ = 0;
    if (header_ && header_->IsVisible()) {
        float width = Width(rect) - 2 * 12;
        headerHeight_ = header_->Measure(width);
        header_->Arrange(MakeRect(rect.left + 12, y, width, headerHeight_));
        y += headerHeight_ + 12;
    }
    itemTops_.assign(items_.size(), 0);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].footer) continue;
        itemTops_[i] = y;
        y += kItemHeight + kItemSpacing;
    }
    float bottom = rect.bottom - 8;
    for (size_t i = items_.size(); i-- > 0;) {
        if (!items_[i].footer) continue;
        bottom -= kItemHeight;
        itemTops_[i] = bottom;
        bottom -= kItemSpacing;
    }
    int index = IndexOf(selected_);
    if (index >= 0) indicatorTop_.Jump(itemTops_[static_cast<size_t>(index)]);
}

RectF NavigationView::ItemRect(size_t index) const {
    return RectF{bounds_.left + kMargin, itemTops_[index], bounds_.right - kMargin, itemTops_[index] + kItemHeight};
}

int NavigationView::IndexAt(PointF point) const {
    for (size_t i = 0; i < items_.size() && i < itemTops_.size(); ++i) {
        if (Contains(ItemRect(i), point)) return static_cast<int>(i);
    }
    return -1;
}

Widget* NavigationView::HitTest(PointF point) {
    if (!visible_ || !Contains(bounds_, point)) return nullptr;
    if (header_ && header_->IsVisible()) {
        if (Widget* hit = header_->HitTest(point)) return hit;
    }
    return IndexAt(point) >= 0 ? this : nullptr;
}

void NavigationView::OnMouseMove(PointF point) {
    int hot = IndexAt(point);
    if (hot != hot_) {
        hot_ = hot;
        Invalidate();
    }
}

void NavigationView::OnMouseLeave() {
    hot_ = -1;
    Invalidate();
}

void NavigationView::OnMouseUp(PointF point, MouseButton button) {
    int index = IndexAt(point);
    if (button != MouseButton::Left || index < 0) return;
    int id = items_[static_cast<size_t>(index)].id;
    if (id != selected_) {
        Select(id);
        if (onSelect) {
            auto handler = onSelect;
            handler(id);
        }
    }
}

bool NavigationView::OnKeyDown(UINT key, bool, bool) {
    if (items_.empty()) return false;
    if (focusIndex_ < 0) focusIndex_ = std::max(0, IndexOf(selected_));
    if (key == VK_UP || key == VK_DOWN) {
        int count = static_cast<int>(items_.size());
        focusIndex_ = (focusIndex_ + (key == VK_DOWN ? 1 : count - 1)) % count;
        Invalidate();
        return true;
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        int id = items_[static_cast<size_t>(focusIndex_)].id;
        if (id != selected_) {
            Select(id);
            if (onSelect) {
                auto handler = onSelect;
                handler(id);
            }
        }
        return true;
    }
    return false;
}

void NavigationView::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    if (!p.translucent) canvas.FillRect(bounds_, p.paneBackground);
    PaintChildren(canvas);
    bool keyboard = focused_ && GetHost() && GetHost()->KeyboardFocusVisible();
    for (size_t i = 0; i < items_.size() && i < itemTops_.size(); ++i) {
        RectF item = ItemRect(i);
        bool selected = items_[i].id == selected_;
        if (selected) canvas.FillRoundRect(item, Theme::ControlRadius, p.subtleHover);
        else if (static_cast<int>(i) == hot_) canvas.FillRoundRect(item, Theme::ControlRadius, pressed_ ? p.subtlePressed : p.subtleHover);
        DrawIcon(canvas, items_[i].icon, MakeRect(item.left + 12, item.top, 20, kItemHeight), p.textPrimary, 16);
        if (!compact_) {
            canvas.Text(items_[i].text, TextStyle::Body, RectF{item.left + 44, item.top, item.right - 8, item.bottom}, p.textPrimary,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
        if (keyboard && static_cast<int>(i) == focusIndex_) PaintFocusVisual(canvas, item, Theme::ControlRadius);
    }
    if (IndexOf(selected_) >= 0) {
        // Selection pill: slides between items and briefly stretches.
        float top = indicatorTop_.Value(this);
        float stretch = indicatorStretch_.Value(this);
        float height = 16 + 8 * stretch;
        float y = top + (kItemHeight - height) / 2;
        canvas.FillRoundRect(MakeRect(bounds_.left + kMargin, y, 3, height), 1.5f, p.accent);
    }
}

}  // namespace nc::ui
