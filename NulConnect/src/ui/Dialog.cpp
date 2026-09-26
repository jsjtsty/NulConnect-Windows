#include "pch.h"
#include "ui/Controls.h"
#include "ui/Host.h"

namespace nc::ui {

namespace {

// A modal ContentDialog: smoke layer over the window, centered dialog with a
// title, message and a footer of up to three buttons.
class ContentDialog : public Widget {
public:
    ContentDialog(DialogOptions options, std::function<void(DialogResult)> onResult)
        : options_(std::move(options)), onResult_(std::move(onResult)) {
        title_ = Emplace<TextBlock>(options_.title, TextStyle::Subtitle);
        message_ = Emplace<TextBlock>(options_.message, TextStyle::Body);
        footer_ = Emplace<StackPanel>(Orientation::Horizontal, 8.0f);
        if (!options_.primaryText.empty()) {
            primary_ = footer_->Emplace<Button>(options_.primaryText,
                                                options_.primaryIsDanger ? ButtonStyle::Danger : ButtonStyle::Accent);
            primary_->onClick = [this] { Close(DialogResult::Primary); };
        }
        if (!options_.secondaryText.empty()) {
            auto* secondary = footer_->Emplace<Button>(options_.secondaryText);
            secondary->onClick = [this] { Close(DialogResult::Secondary); };
        }
        if (!options_.closeText.empty()) {
            auto* close = footer_->Emplace<Button>(options_.closeText);
            close->onClick = [this] { Close(DialogResult::None); };
            closeButton_ = close;
        }
        appear_.Jump(0);
        appear_.Set(1, Motion::Normal);
    }

    void Arrange(const RectF& rect) override {
        bounds_ = rect;
        float width = std::clamp(Width(rect) - 48, 320.0f, 448.0f);
        float inner = width - 48;
        float titleHeight = title_->Measure(inner);
        float messageHeight = std::min(message_->Measure(inner), Height(rect) * 0.6f);
        float bodyHeight = 24 + titleHeight + 12 + messageHeight + 24;
        float footerHeight = 80;
        float height = bodyHeight + footerHeight;
        float left = rect.left + (Width(rect) - width) / 2;
        float top = rect.top + (Height(rect) - height) / 2;
        dialog_ = MakeRect(left, top, width, height);
        title_->Arrange(MakeRect(left + 24, top + 24, inner, titleHeight));
        message_->Arrange(MakeRect(left + 24, top + 24 + titleHeight + 12, inner, messageHeight));
        // Buttons share the footer width equally, as in WinUI.
        size_t count = footer_->Children().size();
        float gap = 8;
        float buttonWidth = count ? (inner - gap * static_cast<float>(count - 1)) / static_cast<float>(count) : 0;
        for (auto& child : footer_->Children()) static_cast<Button*>(child.get())->SetFixedWidth(buttonWidth);
        footer_->Arrange(MakeRect(left + 24, top + bodyHeight + 24, inner, 32));
        if (!focusedOnce_) {
            focusedOnce_ = true;
            // Default button takes focus (Enter activates it); the focus
            // rectangle appears only once the keyboard is used.
            if (Host* host = GetHost()) host->SetFocus(primary_ ? primary_ : closeButton_, false);
        }
    }

    Widget* HitTest(PointF point) override {
        if (!Contains(bounds_, point)) return nullptr;
        if (Widget* hit = Widget::HitTest(point); hit && hit != this) return hit;
        return this;  // swallow clicks on the smoke layer
    }
    bool IsInteractive() const override { return true; }

    bool OnKeyDown(UINT key, bool, bool) override {
        if (key == VK_ESCAPE) {
            Close(DialogResult::None);
            return true;
        }
        return false;
    }

    void Paint(Canvas& canvas) override {
        const Palette& p = Theme::Current();
        float t = appear_.Value(this);
        canvas.FillRect(bounds_, WithAlpha(p.smoke, p.smoke.a * t));
        D2D1_MATRIX_3X2_F previous;
        canvas.Target()->GetTransform(&previous);
        float scale = 1.05f - 0.05f * t;
        PointF center{(dialog_.left + dialog_.right) / 2, (dialog_.top + dialog_.bottom) / 2};
        canvas.Target()->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) *
                                      *D2D1::Matrix3x2F::ReinterpretBaseType(&previous));
        ComPtr<ID2D1Layer> layer;
        canvas.Target()->CreateLayer(&layer);
        canvas.Target()->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                         D2D1::IdentityMatrix(), t),
                                   layer.Get());
        // Soft shadow approximation: many faint, slightly offset layers.
        for (int i = 1; i <= 16; ++i) {
            float spread = static_cast<float>(i) * 1.5f;
            canvas.FillRoundRect(Offset(Inset(dialog_, -spread, -spread), 0, spread * 0.6f), Theme::OverlayRadius + spread,
                                 Rgba(0x000000, p.dark ? 0.03f : 0.012f));
        }
        Color surface = p.dark ? Rgba(0x2B2B2B) : Rgba(0xFFFFFF);
        canvas.FillRoundRect(dialog_, Theme::OverlayRadius, surface);
        RectF footer{dialog_.left, dialog_.bottom - 80, dialog_.right, dialog_.bottom};
        canvas.PushClip(footer);
        canvas.FillRoundRect(dialog_, Theme::OverlayRadius, p.dark ? Rgba(0x202020) : Rgba(0xF3F3F3));
        canvas.PopClip();
        canvas.Line(PointF{footer.left, footer.top}, PointF{footer.right, footer.top}, p.divider);
        canvas.StrokeRoundRect(dialog_, Theme::OverlayRadius, p.dark ? Rgba(0x000000, 0.35f) : Rgba(0x000000, 0.1f));
        PaintChildren(canvas);
        canvas.Target()->PopLayer();
        canvas.Target()->SetTransform(previous);
    }

private:
    void Close(DialogResult result) {
        if (closed_) return;
        closed_ = true;
        auto callback = std::move(onResult_);
        Host* host = GetHost();
        if (host) host->RemoveOverlay(this);
        if (callback) callback(result);
    }

    DialogOptions options_;
    std::function<void(DialogResult)> onResult_;
    TextBlock* title_ = nullptr;
    TextBlock* message_ = nullptr;
    StackPanel* footer_ = nullptr;
    Button* primary_ = nullptr;
    Button* closeButton_ = nullptr;
    RectF dialog_{};
    Animated appear_;
    bool closed_ = false;
    bool focusedOnce_ = false;
};

}  // namespace

void ShowContentDialog(Host* host, DialogOptions options, std::function<void(DialogResult)> onResult) {
    if (!host) return;
    host->PushOverlay(std::make_unique<ContentDialog>(std::move(options), std::move(onResult)), true);
}

}  // namespace nc::ui
