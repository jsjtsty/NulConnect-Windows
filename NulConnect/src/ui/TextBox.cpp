#include "pch.h"
#include "ui/Controls.h"
#include "ui/Host.h"
#include "core/Localization.h"

namespace nc::ui {

namespace {

constexpr float kPaddingX = 11.0f;

bool IsWordChar(wchar_t c) {
    return iswalnum(c) || c == L'_';
}

}  // namespace

TextBox::TextBox(std::wstring text) : text_(std::move(text)) {
    caret_ = anchor_ = text_.size();
}

void TextBox::SetText(std::wstring text) {
    if (text == text_) return;
    text_ = std::move(text);
    caret_ = anchor_ = text_.size();
    scroll_ = 0;
    layout_.Reset();
    Invalidate();
}

HCURSOR TextBox::Cursor() const {
    return LoadCursorW(nullptr, IDC_IBEAM);
}

RectF TextBox::TextRect() const {
    return RectF{bounds_.left + kPaddingX, bounds_.top, bounds_.right - kPaddingX, bounds_.bottom};
}

void TextBox::EnsureLayout() {
    if (layout_ && layoutText_ == text_) return;
    layoutText_ = text_;
    layout_ = Graphics::Layout(text_, style_, 0, false);
}

float TextBox::CaretX(size_t position) {
    EnsureLayout();
    FLOAT x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestTextPosition(static_cast<UINT32>(position), FALSE, &x, &y, &metrics);
    return x;
}

size_t TextBox::PositionFromPoint(PointF point) {
    EnsureLayout();
    RectF textRect = TextRect();
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestPoint(point.x - textRect.left + scroll_, 5.0f, &trailing, &inside, &metrics);
    size_t position = metrics.textPosition + (trailing ? metrics.length : 0);
    return std::min(position, text_.size());
}

void TextBox::MoveCaret(size_t position, bool extend) {
    caret_ = std::min(position, text_.size());
    if (!extend) anchor_ = caret_;
    // Keep the caret inside the visible text area.
    float x = CaretX(caret_);
    float visible = Width(TextRect());
    if (x - scroll_ > visible - 2) scroll_ = x - visible + 2;
    if (x - scroll_ < 0) scroll_ = x;
    if (scroll_ < 0) scroll_ = 0;
    if (Host* host = GetHost()) host->StartCaretBlink();
    UpdateIme();
    Invalidate();
}

void TextBox::UpdateIme() {
    Host* host = GetHost();
    if (!host || !focused_) return;
    RectF textRect = TextRect();
    float x = textRect.left + CaretX(caret_) - scroll_;
    RectF caret = MakeRect(x, bounds_.top + 6, 1, Height(bounds_) - 12);
    host->SetImeCaret(ToWindow(caret));
}

void TextBox::DeleteSelection() {
    if (!HasSelection()) return;
    size_t start = std::min(caret_, anchor_);
    size_t end = std::max(caret_, anchor_);
    text_.erase(start, end - start);
    caret_ = anchor_ = start;
}

void TextBox::InsertText(std::wstring_view text) {
    std::wstring filtered;
    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L'\t') continue;
        if (digitsOnly_ && (c < L'0' || c > L'9')) continue;
        filtered.push_back(c);
    }
    size_t selected = HasSelection() ? std::max(caret_, anchor_) - std::min(caret_, anchor_) : 0;
    size_t room = maxLength_ > text_.size() - selected ? maxLength_ - (text_.size() - selected) : 0;
    if (filtered.size() > room) filtered.resize(room);
    if (filtered.empty() && !HasSelection()) {
        if (digitsOnly_ && !text.empty()) MessageBeep(MB_OK);
        return;
    }
    undo_ = text_;
    DeleteSelection();
    text_.insert(caret_, filtered);
    MoveCaret(caret_ + filtered.size(), false);
    Changed();
}

size_t TextBox::WordBoundary(size_t from, bool forward) const {
    size_t position = from;
    if (forward) {
        while (position < text_.size() && !IsWordChar(text_[position])) ++position;
        while (position < text_.size() && IsWordChar(text_[position])) ++position;
    } else {
        while (position > 0 && !IsWordChar(text_[position - 1])) --position;
        while (position > 0 && IsWordChar(text_[position - 1])) --position;
    }
    return position;
}

void TextBox::Changed() {
    layout_.Reset();
    if (onChange) {
        auto handler = onChange;
        handler(text_);
    }
    Invalidate();
}

void TextBox::OnMouseDown(PointF point, MouseButton button) {
    if (button == MouseButton::Right) {
        ShowContextMenu();
        return;
    }
    bool extend = GetKeyState(VK_SHIFT) < 0;
    MoveCaret(PositionFromPoint(point), extend);
    dragging_ = true;
}

void TextBox::OnMouseMove(PointF point) {
    if (!dragging_ || !pressed_) {
        dragging_ = false;
        return;
    }
    MoveCaret(PositionFromPoint(point), true);
}

void TextBox::OnDoubleClick(PointF point) {
    size_t position = PositionFromPoint(point);
    anchor_ = WordBoundary(position, false);
    caret_ = WordBoundary(anchor_, true);
    if (anchor_ == caret_) {
        anchor_ = 0;
        caret_ = text_.size();
    }
    Invalidate();
}

bool TextBox::OnKeyDown(UINT key, bool shift, bool ctrl) {
    switch (key) {
    case VK_LEFT:
        if (HasSelection() && !shift) MoveCaret(std::min(caret_, anchor_), false);
        else if (caret_ > 0) MoveCaret(ctrl ? WordBoundary(caret_, false) : caret_ - 1, shift);
        return true;
    case VK_RIGHT:
        if (HasSelection() && !shift) MoveCaret(std::max(caret_, anchor_), false);
        else if (caret_ < text_.size()) MoveCaret(ctrl ? WordBoundary(caret_, true) : caret_ + 1, shift);
        return true;
    case VK_HOME:
        MoveCaret(0, shift);
        return true;
    case VK_END:
        MoveCaret(text_.size(), shift);
        return true;
    case VK_BACK:
        if (HasSelection()) {
            undo_ = text_;
            DeleteSelection();
        } else if (caret_ > 0) {
            undo_ = text_;
            size_t start = ctrl ? WordBoundary(caret_, false) : caret_ - 1;
            text_.erase(start, caret_ - start);
            caret_ = anchor_ = start;
        } else {
            return true;
        }
        MoveCaret(caret_, false);
        Changed();
        return true;
    case VK_DELETE:
        if (HasSelection()) {
            undo_ = text_;
            DeleteSelection();
        } else if (caret_ < text_.size()) {
            undo_ = text_;
            size_t end = ctrl ? WordBoundary(caret_, true) : caret_ + 1;
            text_.erase(caret_, end - caret_);
        } else {
            return true;
        }
        MoveCaret(caret_, false);
        Changed();
        return true;
    case VK_RETURN:
        if (onCommit) {
            auto handler = onCommit;
            handler(text_);
        }
        return true;
    case VK_ESCAPE:
        return false;
    default:
        break;
    }
    if (!ctrl) return false;
    switch (key) {
    case 'A':
        anchor_ = 0;
        caret_ = text_.size();
        Invalidate();
        return true;
    case 'C':
    case 'X':
        if (HasSelection()) {
            size_t start = std::min(caret_, anchor_);
            CopyTextToClipboard(GetHost()->Hwnd(), text_.substr(start, std::max(caret_, anchor_) - start));
            if (key == 'X') {
                undo_ = text_;
                DeleteSelection();
                MoveCaret(caret_, false);
                Changed();
            }
        }
        return true;
    case 'V':
        InsertText(ReadClipboardText(GetHost()->Hwnd()));
        return true;
    case 'Z':
        if (undo_ != text_) {
            std::swap(undo_, text_);
            MoveCaret(text_.size(), false);
            Changed();
        }
        return true;
    default:
        return false;
    }
}

void TextBox::OnChar(wchar_t ch) {
    if (ch < 0x20) return;
    if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0) return;
    InsertText(std::wstring_view(&ch, 1));
}

void TextBox::OnFocusChanged(bool focused) {
    Host* host = GetHost();
    if (focused) {
        if (host) host->StartCaretBlink();
        if (host && host->KeyboardFocusVisible()) {
            anchor_ = 0;
            caret_ = text_.size();
        }
        UpdateIme();
    } else {
        if (host) host->StopCaretBlink();
        anchor_ = caret_;
        scroll_ = 0;
        if (onCommit) {
            auto handler = onCommit;
            handler(text_);
        }
    }
    Invalidate();
}

void TextBox::ShowContextMenu() {
    Host* host = GetHost();
    if (!host) return;
    HMENU menu = CreatePopupMenu();
    bool selection = HasSelection();
    AppendMenuW(menu, selection ? MF_STRING : MF_STRING | MF_GRAYED, 1, Tr(L"Cut").c_str());
    AppendMenuW(menu, selection ? MF_STRING : MF_STRING | MF_GRAYED, 2, Tr(L"Copy").c_str());
    AppendMenuW(menu, IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_STRING : MF_STRING | MF_GRAYED, 3, Tr(L"Paste").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, Tr(L"Select All").c_str());
    POINT cursor;
    GetCursorPos(&cursor);
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, host->Hwnd(), nullptr);
    DestroyMenu(menu);
    switch (command) {
    case 1: OnKeyDown('X', false, true); break;
    case 2: OnKeyDown('C', false, true); break;
    case 3: OnKeyDown('V', false, true); break;
    case 4: OnKeyDown('A', false, true); break;
    default: break;
    }
    Invalidate();
}

void TextBox::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    bool enabled = IsEnabled();
    RectF rect = bounds_;
    float radius = Theme::ControlRadius;
    Color fill = !enabled ? p.controlFillDisabled : focused_ ? p.inputFillFocused : hovered_ ? p.controlFillHover : p.inputFill;
    if (!p.translucent) {
        fill = p.dark ? (focused_ ? Rgba(0x1F1F1F) : hovered_ ? Rgba(0x323232) : Rgba(0x2D2D2D))
                      : (focused_ ? Rgba(0xFFFFFF) : hovered_ ? Rgba(0xF6F6F6) : Rgba(0xFBFBFB));
    }
    canvas.FillRoundRect(rect, radius, fill);
    canvas.StrokeRoundRect(rect, radius, p.controlStroke);
    // Bottom stroke: 1px strong stroke, 2px accent while focused.
    canvas.PushClip(RectF{rect.left, rect.bottom - 2, rect.right, rect.bottom});
    Color bottom = error_ ? p.critical : focused_ ? p.accent : p.controlStrongStroke;
    float thickness = focused_ || error_ ? 2.0f : 1.0f;
    canvas.PushClip(RectF{rect.left, rect.bottom - thickness, rect.right, rect.bottom});
    canvas.FillRoundRect(rect, radius, enabled ? bottom : p.controlStroke);
    canvas.PopClip();
    canvas.PopClip();

    RectF textRect = TextRect();
    canvas.PushClip(Inset(textRect, -1, 0));
    if (text_.empty()) {
        if (!placeholder_.empty() && !focused_) {
            canvas.Text(placeholder_, style_, textRect, enabled ? p.textSecondary : p.textDisabled,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
    } else {
        EnsureLayout();
        DWRITE_TEXT_METRICS metrics{};
        layout_->GetMetrics(&metrics);
        float top = rect.top + (Height(rect) - metrics.height) / 2;
        if (HasSelection() && focused_) {
            size_t start = std::min(caret_, anchor_);
            size_t end = std::max(caret_, anchor_);
            float x1 = CaretX(start), x2 = CaretX(end);
            canvas.FillRect(RectF{textRect.left + x1 - scroll_, top, textRect.left + x2 - scroll_, top + metrics.height},
                            WithAlpha(p.accent, 0.85f));
            // Selected text drawn in the on-accent color on top.
            canvas.TextLayout(layout_.Get(), PointF{textRect.left - scroll_, top}, enabled ? p.textPrimary : p.textDisabled);
            canvas.PushClip(RectF{textRect.left + x1 - scroll_, top, textRect.left + x2 - scroll_, top + metrics.height});
            canvas.TextLayout(layout_.Get(), PointF{textRect.left - scroll_, top}, p.textOnAccent);
            canvas.PopClip();
        } else {
            canvas.TextLayout(layout_.Get(), PointF{textRect.left - scroll_, top}, enabled ? p.textPrimary : p.textDisabled);
        }
    }
    if (focused_ && GetHost() && GetHost()->CaretVisible() && !HasSelection()) {
        float x = std::round(textRect.left + (text_.empty() ? 0 : CaretX(caret_)) - scroll_);
        canvas.FillRect(RectF{x, rect.top + 8, x + 1, rect.bottom - 8}, p.textPrimary);
    }
    canvas.PopClip();
}

}  // namespace nc::ui
