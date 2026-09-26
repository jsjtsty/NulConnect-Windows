#pragma once

#include "ui/Icons.h"
#include "ui/Widget.h"

#include <functional>
#include <string>
#include <vector>

namespace nc::ui {

enum class TextColor { Primary, Secondary, Tertiary, Disabled, Accent, Success, Caution, Critical, OnAccent };
Color ResolveTextColor(TextColor color);

// ---- Text --------------------------------------------------------------------

class TextBlock : public Widget {
public:
    TextBlock(std::wstring text = {}, TextStyle style = TextStyle::Body, TextColor color = TextColor::Primary);

    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }
    void SetStyle(TextStyle style);
    void SetColor(TextColor color);
    void SetCustomColor(std::optional<Color> color);
    void SetAlignment(DWRITE_TEXT_ALIGNMENT alignment);
    void SetWrap(bool wrap);
    void SetMaxLines(int lines);

    float Measure(float width) override;
    float MeasureWidth() override;
    void Paint(Canvas& canvas) override;

private:
    IDWriteTextLayout* LayoutFor(float width);

    std::wstring text_;
    TextStyle style_;
    TextColor color_;
    std::optional<Color> customColor_;
    DWRITE_TEXT_ALIGNMENT alignment_ = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool wrap_ = true;
    int maxLines_ = 0;
    ComPtr<IDWriteTextLayout> layout_;
    float layoutWidth_ = -1;
};

// ---- Button ------------------------------------------------------------------

enum class ButtonStyle { Standard, Accent, Subtle, Danger, Hyperlink };

class Button : public Widget {
public:
    Button(std::wstring text, ButtonStyle style = ButtonStyle::Standard, Icon icon = Icon::None);

    void SetText(std::wstring text);
    void SetIcon(Icon icon);
    void SetStyle(ButtonStyle style);
    void SetLarge(bool large);
    void SetMinWidth(float width) { minWidth_ = width; }
    void SetFixedWidth(float width) { fixedWidth_ = width; }
    void SetTooltip(std::wstring tooltip) { tooltip_ = std::move(tooltip); }
    const std::wstring& Tooltip() const { return tooltip_; }
    std::function<void()> onClick;

    float Measure(float width) override;
    float MeasureWidth() override;
    void Paint(Canvas& canvas) override;
    bool IsInteractive() const override { return true; }
    bool IsFocusable() const override { return true; }
    HCURSOR Cursor() const override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown(PointF, MouseButton) override;
    void OnMouseUp(PointF point, MouseButton button) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;

private:
    void Click();

    std::wstring text_;
    std::wstring tooltip_;
    ButtonStyle style_;
    Icon icon_;
    bool large_ = false;
    float minWidth_ = 0;
    float fixedWidth_ = 0;
    Animated hover_;
    Animated press_;
};

// ---- Toggle switch -------------------------------------------------------------

class ToggleSwitch : public Widget {
public:
    explicit ToggleSwitch(bool on = false);

    void SetOn(bool on, bool animate = true);
    bool IsOn() const { return on_; }
    void SetShowStateText(bool show) { showStateText_ = show; }
    void SetDangerous(bool dangerous) { dangerous_ = dangerous; }
    std::function<void(bool)> onChange;

    float Measure(float) override { return 32; }
    float MeasureWidth() override;
    void Paint(Canvas& canvas) override;
    bool IsInteractive() const override { return true; }
    bool IsFocusable() const override { return true; }
    HCURSOR Cursor() const override;
    void OnMouseEnter() override { hover_.Set(1); Invalidate(); }
    void OnMouseLeave() override { hover_.Set(0); Invalidate(); }
    void OnMouseDown(PointF, MouseButton) override;
    void OnMouseUp(PointF point, MouseButton button) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;

private:
    void Toggle();
    RectF TrackRect() const;

    bool on_;
    bool showStateText_ = true;
    bool dangerous_ = false;
    Animated position_;
    Animated hover_;
    Animated press_;
};

// ---- Text box ----------------------------------------------------------------

class TextBox : public Widget {
public:
    explicit TextBox(std::wstring text = {});

    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }
    void SetPlaceholder(std::wstring placeholder) { placeholder_ = std::move(placeholder); }
    void SetDigitsOnly(bool digits) { digitsOnly_ = digits; }
    void SetMaxLength(size_t length) { maxLength_ = length; }
    void SetPreferredWidth(float width) { preferredWidth_ = width; }
    void SetMonospace(bool mono) { style_ = mono ? TextStyle::Monospace : TextStyle::Body; }
    void SetError(bool error) { error_ = error; Invalidate(); }

    std::function<void(const std::wstring&)> onChange;
    // Enter or focus loss.
    std::function<void(const std::wstring&)> onCommit;

    float Measure(float) override { return 32; }
    float MeasureWidth() override { return preferredWidth_; }
    void Paint(Canvas& canvas) override;
    bool IsInteractive() const override { return true; }
    bool IsFocusable() const override { return true; }
    HCURSOR Cursor() const override;
    void OnMouseEnter() override { Invalidate(); }
    void OnMouseLeave() override { Invalidate(); }
    void OnMouseDown(PointF point, MouseButton button) override;
    void OnMouseMove(PointF point) override;
    void OnDoubleClick(PointF point) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;
    void OnChar(wchar_t ch) override;
    void OnFocusChanged(bool focused) override;

private:
    RectF TextRect() const;
    size_t PositionFromPoint(PointF point);
    float CaretX(size_t position);
    void EnsureLayout();
    void MoveCaret(size_t position, bool extend);
    void InsertText(std::wstring_view text);
    void DeleteSelection();
    bool HasSelection() const { return caret_ != anchor_; }
    size_t WordBoundary(size_t from, bool forward) const;
    void Changed();
    void UpdateIme();
    void ShowContextMenu();

    std::wstring text_;
    std::wstring placeholder_;
    std::wstring undo_;
    TextStyle style_ = TextStyle::Body;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    float scroll_ = 0;
    float preferredWidth_ = 240;
    size_t maxLength_ = 1024;
    bool digitsOnly_ = false;
    bool error_ = false;
    bool dragging_ = false;
    ComPtr<IDWriteTextLayout> layout_;
    std::wstring layoutText_;
};

// ---- Segmented selector -----------------------------------------------------------

class Segmented : public Widget {
public:
    explicit Segmented(std::vector<std::wstring> options);

    void SetSelected(int index, bool animate = true);
    int Selected() const { return selected_; }
    void SetDangerIndex(int index) { dangerIndex_ = index; }
    std::function<void(int)> onChange;

    float Measure(float) override { return 32; }
    float MeasureWidth() override;
    void Paint(Canvas& canvas) override;
    bool IsInteractive() const override { return true; }
    bool IsFocusable() const override { return true; }
    HCURSOR Cursor() const override;
    void OnMouseMove(PointF point) override;
    void OnMouseLeave() override { hot_ = -1; Invalidate(); }
    void OnMouseUp(PointF point, MouseButton button) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;

private:
    int IndexAt(PointF point) const;
    RectF SegmentRect(int index) const;

    std::vector<std::wstring> options_;
    int selected_ = 0;
    int hot_ = -1;
    int dangerIndex_ = -1;
    Animated indicator_;
};

// ---- Layout containers ------------------------------------------------------

enum class Orientation { Vertical, Horizontal };
enum class Alignment { Start, Center, End, Stretch };

class StackPanel : public Widget {
public:
    explicit StackPanel(Orientation orientation = Orientation::Vertical, float spacing = 0);

    void SetPadding(float left, float top, float right, float bottom);
    void SetSpacing(float spacing) { spacing_ = spacing; }
    void SetAlignment(Alignment alignment) { alignment_ = alignment; }
    void SetMaxWidth(float width) { maxWidth_ = width; }

    float Measure(float width) override;
    float MeasureWidth() override;
    void Arrange(const RectF& rect) override;

private:
    Orientation orientation_;
    float spacing_;
    Alignment alignment_ = Alignment::Stretch;
    float padLeft_ = 0, padTop_ = 0, padRight_ = 0, padBottom_ = 0;
    float maxWidth_ = 0;
};

class Spacer : public Widget {
public:
    explicit Spacer(float height = 0, float width = 0) : height_(height), width_(width) {}
    float Measure(float) override { return height_; }
    float MeasureWidth() override { return width_; }

private:
    float height_;
    float width_;
};

class ScrollView : public Widget {
public:
    ScrollView();

    Widget* SetContent(std::unique_ptr<Widget> content);
    Widget* Content() const { return children_.empty() ? nullptr : children_.front().get(); }
    void ScrollTo(float offset, bool animate = true);
    float ScrollOffset() const { return scroll_.Target(); }

    void Arrange(const RectF& rect) override;
    void Paint(Canvas& canvas) override;
    Widget* HitTest(PointF point) override;
    bool IsInteractive() const override { return true; }
    PointF ChildOffset() const override;
    bool OnMouseWheel(float delta) override;
    void OnMouseEnter() override { barHover_.Set(1, Motion::Normal); Invalidate(); }
    void OnMouseLeave() override { barHover_.Set(0, Motion::Slow); Invalidate(); }
    void OnMouseDown(PointF point, MouseButton button) override;
    void OnMouseMove(PointF point) override;
    void OnMouseUp(PointF, MouseButton) override { dragging_ = false; }
    void BringIntoView(const RectF& rect) override;

private:
    float MaxScroll() const;
    RectF ThumbRect(float width) const;
    RectF BarRect() const;

    float contentHeight_ = 0;
    mutable Animated scroll_;
    Animated barHover_;
    bool dragging_ = false;
    float dragOrigin_ = 0;
    float dragScroll_ = 0;
};

// ---- Cards -------------------------------------------------------------------

// A Windows 11 Settings-style card: icon, title, description and an optional
// trailing control. Clickable cards show a chevron and hover feedback.
class SettingsCard : public Widget {
public:
    SettingsCard(Icon icon, std::wstring title, std::wstring description = {});

    void SetTitle(std::wstring title);
    void SetDescription(std::wstring description);
    void SetIcon(Icon icon) { icon_ = icon; Invalidate(); }
    void SetTitleColor(TextColor color) { titleColor_ = color; Invalidate(); }
    void SetDescriptionColor(TextColor color) { descriptionColor_ = color; Invalidate(); }
    template <typename T>
    T* SetTrailing(std::unique_ptr<T> widget) {
        T* raw = widget.get();
        ClearChildren();
        AddChild(std::move(widget));
        return raw;
    }
    Widget* Trailing() const { return children_.empty() ? nullptr : children_.front().get(); }
    void SetClickable(bool clickable, Icon trailingGlyph = Icon::ChevronRight);
    std::function<void()> onClick;

    float Measure(float width) override;
    void Arrange(const RectF& rect) override;
    void Paint(Canvas& canvas) override;
    Widget* HitTest(PointF point) override;
    bool IsInteractive() const override { return clickable_; }
    bool IsFocusable() const override { return clickable_; }
    HCURSOR Cursor() const override;
    void OnMouseEnter() override { hover_.Set(1); Invalidate(); }
    void OnMouseLeave() override { hover_.Set(0); Invalidate(); }
    void OnMouseUp(PointF point, MouseButton button) override;
    bool OnKeyDown(UINT key, bool shift, bool ctrl) override;

private:
    struct LayoutInfo {
        float textLeft;
        float textWidth;
        float trailingWidth;
        bool stacked;
    };
    LayoutInfo ComputeLayout(float width);

    Icon icon_;
    std::wstring title_;
    std::wstring description_;
    TextColor titleColor_ = TextColor::Primary;
    TextColor descriptionColor_ = TextColor::Secondary;
    bool clickable_ = false;
    Icon trailingGlyph_ = Icon::ChevronRight;
    Animated hover_;
    float titleHeight_ = 0;
    float descriptionHeight_ = 0;
    LayoutInfo layout_{};
};

// Plain container card with padding; hosts arbitrary content.
class Card : public Widget {
public:
    explicit Card(float padding = 16) : padding_(padding) {}
    Widget* SetContent(std::unique_ptr<Widget> content);
    float Measure(float width) override;
    void Arrange(const RectF& rect) override;
    void Paint(Canvas& canvas) override;

private:
    float padding_;
};

// ---- Status ------------------------------------------------------------------

enum class Severity { Informational, Success, Warning, Error };

class InfoBar : public Widget {
public:
    InfoBar(Severity severity, std::wstring title, std::wstring message);

    void Set(Severity severity, std::wstring title, std::wstring message);
    void SetClosable(bool closable);
    std::function<void()> onClose;

    float Measure(float width) override;
    void Arrange(const RectF& rect) override;
    void Paint(Canvas& canvas) override;

private:
    float TextWidth(float width) const;

    Severity severity_;
    std::wstring title_;
    std::wstring message_;
    Button* closeButton_ = nullptr;
};

class ProgressRing : public Widget {
public:
    explicit ProgressRing(float size = 32, float thickness = 3) : size_(size), thickness_(thickness) {}
    void SetColor(std::optional<Color> color) { color_ = color; }
    float Measure(float) override { return size_; }
    float MeasureWidth() override { return size_; }
    void Paint(Canvas& canvas) override;

private:
    float size_;
    float thickness_;
    std::optional<Color> color_;
};

// ---- Dialog ------------------------------------------------------------------

enum class DialogResult { None, Primary, Secondary };

struct DialogOptions {
    std::wstring title;
    std::wstring message;
    std::wstring primaryText;
    std::wstring secondaryText;
    std::wstring closeText;
    bool primaryIsDanger = false;
};

// Shows a WinUI-style ContentDialog as a modal overlay on `host`.
void ShowContentDialog(Host* host, DialogOptions options, std::function<void(DialogResult)> onResult);

}  // namespace nc::ui
