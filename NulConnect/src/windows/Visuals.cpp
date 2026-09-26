#include "pch.h"
#include "windows/Visuals.h"
#include "ui/Host.h"

namespace nc {

using namespace ui;

Color PhaseColor(ConnectionPhase phase) {
    const Palette& p = Theme::Current();
    switch (phase) {
    case ConnectionPhase::Connected: return p.success;
    case ConnectionPhase::Connecting:
    case ConnectionPhase::Disconnecting: return p.accent;
    case ConnectionPhase::Failed: return p.critical;
    default: return p.textTertiary;
    }
}

Icon PhaseIcon(ConnectionPhase phase) {
    switch (phase) {
    case ConnectionPhase::Connected: return Icon::ShieldCheck;
    case ConnectionPhase::Failed: return Icon::ShieldAlert;
    default: return Icon::Shield;
    }
}

void DrawBrandLogo(Canvas& canvas, const RectF& box, bool monochrome, Color mono) {
    const Palette& p = Theme::Current();
    ID2D1RenderTarget* target = canvas.Target();
    float size = std::min(Width(box), Height(box));
    float x = box.left + (Width(box) - size) / 2;
    float y = box.top + (Height(box) - size) / 2;
    auto point = [&](float px, float py) { return PointF{x + px / 1024.0f * size, y + py / 1024.0f * size}; };

    if (!monochrome) {
        // Rounded tile with the brand gradient.
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gradient[3] = {{0.0f, Rgba(0xD8FFF3)}, {0.44f, Rgba(0x7EE0DB)}, {1.0f, Rgba(0x08708D)}};
        target->CreateGradientStopCollection(gradient, 3, &stops);
        ComPtr<ID2D1LinearGradientBrush> brush;
        target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(point(168, 86), point(864, 934)), stops.Get(), &brush);
        target->FillRoundedRectangle(D2D1::RoundedRect(MakeRect(x, y, size, size), size * 0.22f, size * 0.22f), brush.Get());
    }
    ComPtr<ID2D1PathGeometry> path;
    Graphics::D2D()->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(point(278, 592), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddBezier(D2D1::BezierSegment(point(332, 498), point(430, 444), point(512, 512)));
    sink->AddBezier(D2D1::BezierSegment(point(594, 580), point(692, 526), point(746, 432)));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    Color link = monochrome ? mono : Rgba(0xF7FFFB);
    // The monochrome mark is used at notification-area size and needs
    // heavier strokes to stay legible.
    float boost = monochrome ? 1.9f : 1.0f;
    target->DrawGeometry(path.Get(), canvas.Brush(link), size * 84 / 1024.0f * boost, Graphics::RoundStroke());
    float node = size * 54 / 1024.0f * boost;
    float dot = size * 18 / 1024.0f;
    for (auto [cx, cy, core] : {std::tuple{278.0f, 592.0f, 0x13B8BFu}, std::tuple{746.0f, 432.0f, 0x087D9Du}}) {
        PointF c = point(cx, cy);
        if (monochrome) {
            canvas.FillEllipse(c, node, node, mono);
        } else {
            canvas.FillEllipse(c, node * 1.4f, node * 1.4f, Rgba(0xEAFFFA, 0.4f));
            canvas.FillEllipse(c, node, node, Rgba(0xE6FFFA));
            canvas.FillEllipse(c, dot, dot, Rgba(core));
        }
    }
    (void)p;
}

// ---- StatusGlyph ---------------------------------------------------------------------

void StatusGlyph::SetPhase(ConnectionPhase phase) {
    if (phase == phase_ && phaseStart_ != 0) return;
    from_ = phaseStart_ == 0 ? PhaseColor(phase) : PhaseColor(phase_);
    phase_ = phase;
    to_ = PhaseColor(phase);
    tint_.Jump(0);
    tint_.Set(1, Motion::Slow);
    phaseStart_ = Now();
    Invalidate();
}

void StatusGlyph::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    to_ = PhaseColor(phase_);
    float t = tint_.Value(this);
    Color color = Mix(from_, to_, t);
    PointF center{(bounds_.left + bounds_.right) / 2, (bounds_.top + bounds_.bottom) / 2};
    float radius = size_ / 2;
    double elapsed = Now() - phaseStart_;

    if (phase_ == ConnectionPhase::Connected) {
        // Soft expanding pulse.
        float cycle = static_cast<float>(std::fmod(elapsed, 2.4) / 2.4);
        float pulseRadius = radius + 8 * cycle;
        canvas.StrokeEllipse(center, pulseRadius, pulseRadius, WithAlpha(color, 0.35f * (1 - cycle)), 2.0f);
        RequestFrame();
    }
    canvas.FillEllipse(center, radius, radius, WithAlpha(color, p.dark ? 0.18f : 0.12f));
    canvas.StrokeEllipse(center, radius - 0.5f, radius - 0.5f, WithAlpha(color, 0.3f), 1.0f);

    if (phase_ == ConnectionPhase::Connecting || phase_ == ConnectionPhase::Disconnecting) {
        const float pi = 3.14159265f;
        float rotation = static_cast<float>(std::fmod(elapsed * 1.2, 1.0)) * 2 * pi;
        float sweep = pi * 0.6f;
        float r = radius - 1.5f;
        ComPtr<ID2D1PathGeometry> path;
        Graphics::D2D()->CreatePathGeometry(&path);
        ComPtr<ID2D1GeometrySink> sink;
        path->Open(&sink);
        sink->BeginFigure(PointF{center.x + r * std::cos(rotation), center.y + r * std::sin(rotation)}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(PointF{center.x + r * std::cos(rotation + sweep), center.y + r * std::sin(rotation + sweep)},
                                      D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        canvas.Target()->DrawGeometry(path.Get(), canvas.Brush(color), 3.0f, Graphics::RoundStroke());
        RequestFrame();
    }
    float glyph = size_ * 0.42f;
    DrawIcon(canvas, PhaseIcon(phase_), MakeRect(center.x - glyph / 2, center.y - glyph / 2, glyph, glyph), color, glyph,
             glyph / 14.0f);
}

// ---- TrafficChart ---------------------------------------------------------------------

void TrafficChart::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    RectF area = bounds_;
    for (int i = 0; i <= 3; ++i) {
        float y = std::round(area.top + Height(area) * static_cast<float>(i) / 3.0f) + 0.5f;
        canvas.Line(PointF{area.left, y}, PointF{area.right, y}, p.divider);
    }
    if (samples_.size() < 2) return;
    double peak = 1024;
    for (const auto& sample : samples_) peak = std::max({peak, sample.upload, sample.download});
    peak *= 1.15;
    const size_t capacity = 60;
    float step = Width(area) / static_cast<float>(capacity - 1);
    float startX = area.right - step * static_cast<float>(samples_.size() - 1);

    auto draw = [&](bool download, Color color) {
        ComPtr<ID2D1PathGeometry> fill, line;
        Graphics::D2D()->CreatePathGeometry(&fill);
        Graphics::D2D()->CreatePathGeometry(&line);
        ComPtr<ID2D1GeometrySink> fillSink, lineSink;
        fill->Open(&fillSink);
        line->Open(&lineSink);
        fillSink->BeginFigure(PointF{startX, area.bottom}, D2D1_FIGURE_BEGIN_FILLED);
        size_t index = 0;
        for (const auto& sample : samples_) {
            double value = download ? sample.download : sample.upload;
            PointF point{startX + step * static_cast<float>(index), area.bottom - static_cast<float>(value / peak) * Height(area)};
            fillSink->AddLine(point);
            if (index == 0) lineSink->BeginFigure(point, D2D1_FIGURE_BEGIN_HOLLOW);
            else lineSink->AddLine(point);
            ++index;
        }
        fillSink->AddLine(PointF{area.right, area.bottom});
        fillSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        lineSink->EndFigure(D2D1_FIGURE_END_OPEN);
        fillSink->Close();
        lineSink->Close();
        ComPtr<ID2D1GradientStopCollection> stops;
        D2D1_GRADIENT_STOP gradient[2] = {{0.0f, WithAlpha(color, 0.28f)}, {1.0f, WithAlpha(color, 0.0f)}};
        canvas.Target()->CreateGradientStopCollection(gradient, 2, &stops);
        ComPtr<ID2D1LinearGradientBrush> brush;
        canvas.Target()->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(PointF{0, area.top}, PointF{0, area.bottom}), stops.Get(), &brush);
        canvas.Target()->FillGeometry(fill.Get(), brush.Get());
        canvas.Target()->DrawGeometry(line.Get(), canvas.Brush(color), 1.5f, Graphics::RoundStroke());
    };
    canvas.PushClip(area);
    draw(false, p.success);
    draw(true, p.accent);
    canvas.PopClip();
}

void RateTile::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    Color color = download_ ? p.accent : p.success;
    RectF circle = MakeRect(bounds_.left, bounds_.top + 8, 36, 36);
    canvas.FillEllipse(PointF{circle.left + 18, circle.top + 18}, 18, 18, WithAlpha(color, p.dark ? 0.2f : 0.12f));
    DrawIcon(canvas, icon_, circle, color, 18, 1.8f);
    RectF text{bounds_.left + 48, bounds_.top + 6, bounds_.right, bounds_.bottom};
    canvas.Text(caption_, TextStyle::Caption, RectF{text.left, text.top, text.right, text.top + 18}, p.textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    canvas.Text(value_, TextStyle::Subtitle, RectF{text.left, text.top + 16, text.right, text.bottom}, p.textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
}

// ---- AccountCard -------------------------------------------------------------------------

void AccountCard::Set(std::wstring name, std::wstring detail, bool signedIn) {
    if (name == name_ && detail == detail_ && signedIn == signedIn_) return;
    name_ = std::move(name);
    detail_ = std::move(detail);
    signedIn_ = signedIn;
    Invalidate();
}

void AccountCard::OnMouseUp(PointF point, MouseButton button) {
    if (button == MouseButton::Left && Contains(bounds_, point) && onClick) {
        auto handler = onClick;
        handler();
    }
}

void AccountCard::Paint(Canvas& canvas) {
    const Palette& p = Theme::Current();
    if (hovered_) canvas.FillRoundRect(bounds_, Theme::ControlRadius, pressed_ ? p.subtlePressed : p.subtleHover);
    float avatar = 52;
    PointF center{bounds_.left + 8 + avatar / 2, (bounds_.top + bounds_.bottom) / 2};
    if (signedIn_ && !name_.empty()) {
        canvas.FillEllipse(center, avatar / 2, avatar / 2, p.accent);
        std::wstring initial(1, static_cast<wchar_t>(towupper(name_[0])));
        canvas.Text(initial, TextStyle::Subtitle, MakeRect(center.x - avatar / 2, center.y - avatar / 2, avatar, avatar),
                    p.textOnAccent, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    } else {
        canvas.FillEllipse(center, avatar / 2, avatar / 2, p.dark ? Rgba(0xFFFFFF, 0.08f) : Rgba(0x000000, 0.06f));
        DrawIcon(canvas, Icon::Person, MakeRect(center.x - 14, center.y - 14, 28, 28), p.textSecondary, 26);
    }
    float left = bounds_.left + 8 + avatar + 12;
    canvas.Text(name_, TextStyle::BodyStrong, RectF{left, center.y - 21, bounds_.right - 4, center.y}, p.textPrimary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_FAR, false);
    canvas.Text(detail_, TextStyle::Caption, RectF{left, center.y + 1, bounds_.right - 4, center.y + 20}, p.textSecondary,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
}

std::unique_ptr<TextBlock> SectionHeader(const std::wstring& text) {
    auto block = std::make_unique<TextBlock>(text, TextStyle::BodyStrong);
    return block;
}

std::unique_ptr<TextBlock> ValueText(const std::wstring& text) {
    auto block = std::make_unique<TextBlock>(text, TextStyle::Body, TextColor::Secondary);
    block->SetWrap(false);
    return block;
}

}  // namespace nc
