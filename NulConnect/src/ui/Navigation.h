#pragma once

#include "ui/Controls.h"

namespace nc::ui {

// Left navigation pane in the style of the Windows 11 Settings app: an
// optional header widget (user card), a list of items with an animated
// selection pill, and footer items pinned to the bottom. Collapses to an
// icon rail when narrow.
class NavigationView : public Widget {
public:
    struct Item {
        int id;
        Icon icon;
        std::wstring text;
        bool footer = false;
    };

    NavigationView();

    void SetItems(std::vector<Item> items);
    void SetItemText(int id, std::wstring text);
    void Select(int id, bool animate = true);
    int Selected() const { return selected_; }
    void SetCompact(bool compact);
    bool IsCompact() const { return compact_; }
    float PaneWidth() const { return compact_ ? 56.0f : 288.0f; }
    // Optional content above the items (e.g. account card), expanded mode only.
    Widget* SetHeader(std::unique_ptr<Widget> header);
    std::function<void(int)> onSelect;

    void Arrange(const RectF& rect) override;
    void Paint(Canvas& canvas) override;
    Widget* HitTest(PointF point) override;
    bool IsInteractive() const override { return true; }
    bool IsFocusable() const override { return true; }
    void OnMouseMove(PointF point) override;
    void OnMouseLeave() override;
    void OnMouseUp(PointF point, MouseButton button) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;

private:
    RectF ItemRect(size_t index) const;
    int IndexAt(PointF point) const;
    int IndexOf(int id) const;

    std::vector<Item> items_;
    int selected_ = -1;
    int hot_ = -1;
    int focusIndex_ = -1;
    bool compact_ = false;
    Widget* header_ = nullptr;
    float headerHeight_ = 0;
    Animated indicatorTop_;
    Animated indicatorStretch_;
    std::vector<float> itemTops_;
};

}  // namespace nc::ui
