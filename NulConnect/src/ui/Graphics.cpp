#include "pch.h"
#include "ui/Graphics.h"
#include "core/Localization.h"
#include "core/Platform.h"

namespace nc::ui {

namespace {

ComPtr<ID2D1Factory> g_d2d;
ComPtr<IDWriteFactory> g_dwrite;
ComPtr<IWICImagingFactory> g_wic;
ComPtr<ID2D1StrokeStyle> g_roundStroke;
std::map<TextStyle, ComPtr<IDWriteTextFormat>> g_formats;
std::wstring g_family;
std::wstring g_displayFamily;

bool HasFamily(const wchar_t* name) {
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(g_dwrite->GetSystemFontCollection(&collection))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists;
}

void ChooseFamilies() {
    // CJK UI fonts give correct glyph shapes per region; Latin text is
    // covered by them as well. Segoe UI Variable is the Windows 11 UI face.
    const wchar_t* candidates[4] = {};
    switch (CurrentLanguage()) {
    case Language::ChineseSimplified: candidates[0] = L"Microsoft YaHei UI"; candidates[1] = L"Microsoft YaHei"; break;
    case Language::ChineseTraditional: candidates[0] = L"Microsoft JhengHei UI"; candidates[1] = L"Microsoft JhengHei"; break;
    case Language::Japanese: candidates[0] = L"Yu Gothic UI"; candidates[1] = L"Meiryo UI"; break;
    default: candidates[0] = L"Segoe UI Variable Text"; candidates[1] = L"Segoe UI"; break;
    }
    g_family = L"Segoe UI";
    for (const wchar_t* candidate : candidates) {
        if (candidate && HasFamily(candidate)) {
            g_family = candidate;
            break;
        }
    }
    g_displayFamily = g_family;
    if (g_family == L"Segoe UI Variable Text" && HasFamily(L"Segoe UI Variable Display")) {
        g_displayFamily = L"Segoe UI Variable Display";
    }
}

LARGE_INTEGER g_frequency{};

}  // namespace

float FontSize(TextStyle style) {
    switch (style) {
    case TextStyle::Caption: return 12.0f;
    case TextStyle::Body: return 14.0f;
    case TextStyle::BodyStrong: return 14.0f;
    case TextStyle::BodyLarge: return 18.0f;
    case TextStyle::Subtitle: return 20.0f;
    case TextStyle::Title: return 28.0f;
    case TextStyle::TitleLarge: return 40.0f;
    case TextStyle::Monospace: return 13.0f;
    }
    return 14.0f;
}

bool Graphics::Initialize() {
    QueryPerformanceFrequency(&g_frequency);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(g_dwrite.GetAddressOf())))) {
        return false;
    }
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                     D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    g_d2d->CreateStrokeStyle(props, nullptr, 0, &g_roundStroke);
    ChooseFamilies();
    return true;
}

ID2D1Factory* Graphics::D2D() { return g_d2d.Get(); }
IDWriteFactory* Graphics::DWrite() { return g_dwrite.Get(); }
IWICImagingFactory* Graphics::Wic() { return g_wic.Get(); }
ID2D1StrokeStyle* Graphics::RoundStroke() { return g_roundStroke.Get(); }

IDWriteTextFormat* Graphics::Format(TextStyle style) {
    auto it = g_formats.find(style);
    if (it != g_formats.end()) return it->second.Get();
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    const wchar_t* family = g_family.c_str();
    switch (style) {
    case TextStyle::BodyStrong:
    case TextStyle::Subtitle:
        weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        break;
    case TextStyle::Title:
    case TextStyle::TitleLarge:
        weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        family = g_displayFamily.c_str();
        break;
    case TextStyle::Monospace:
        family = L"Consolas";
        break;
    default:
        break;
    }
    ComPtr<IDWriteTextFormat> format;
    g_dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                               FontSize(style), L"", &format);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    g_formats[style] = format;
    return format.Get();
}

ComPtr<IDWriteTextLayout> Graphics::Layout(std::wstring_view text, TextStyle style, float maxWidth, bool wrap,
                                           DWRITE_TEXT_ALIGNMENT align) {
    ComPtr<IDWriteTextLayout> layout;
    float width = maxWidth > 0 ? maxWidth : 100000.0f;
    g_dwrite->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), Format(style), width, 100000.0f, &layout);
    if (layout) {
        layout->SetWordWrapping(wrap && maxWidth > 0 ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        layout->SetTextAlignment(align);
        if (!wrap && maxWidth > 0) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            g_dwrite->CreateEllipsisTrimmingSign(Format(style), &ellipsis);
            layout->SetTrimming(&trimming, ellipsis.Get());
        }
    }
    return layout;
}

D2D1_SIZE_F Graphics::Measure(std::wstring_view text, TextStyle style, float maxWidth, bool wrap) {
    auto layout = Layout(text, style, maxWidth, wrap);
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
}

double Now() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(g_frequency.QuadPart ? g_frequency.QuadPart : 1);
}

Canvas::Canvas(ID2D1RenderTarget* target) : target_(target) {
    target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
}

ID2D1SolidColorBrush* Canvas::Brush(Color color) {
    brush_->SetColor(color);
    return brush_.Get();
}

void Canvas::FillRect(const RectF& rect, Color color) {
    target_->FillRectangle(rect, Brush(color));
}

void Canvas::FillRoundRect(const RectF& rect, float radius, Color color) {
    if (color.a <= 0) return;
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Brush(color));
}

void Canvas::StrokeRoundRect(const RectF& rect, float radius, Color color, float width) {
    if (color.a <= 0) return;
    float half = width / 2;
    target_->DrawRoundedRectangle(D2D1::RoundedRect(Inset(rect, half, half), radius, radius), Brush(color), width);
}

void Canvas::FillEllipse(PointF center, float rx, float ry, Color color) {
    target_->FillEllipse(D2D1::Ellipse(center, rx, ry), Brush(color));
}

void Canvas::StrokeEllipse(PointF center, float rx, float ry, Color color, float width) {
    target_->DrawEllipse(D2D1::Ellipse(center, rx, ry), Brush(color), width);
}

void Canvas::Line(PointF a, PointF b, Color color, float width, bool round) {
    target_->DrawLine(a, b, Brush(color), width, round ? Graphics::RoundStroke() : nullptr);
}

void Canvas::ControlBorder(const RectF& rect, float radius, Color stroke, Color bottomStroke) {
    StrokeRoundRect(rect, radius, stroke);
    // Re-stroke the bottom edge with the stronger color, clipped to the
    // lowest pixel rows.
    PushClip(RectF{rect.left, rect.bottom - radius, rect.right, rect.bottom});
    StrokeRoundRect(rect, radius, bottomStroke);
    PopClip();
}

void Canvas::Text(std::wstring_view text, TextStyle style, const RectF& rect, Color color, DWRITE_TEXT_ALIGNMENT align,
                  DWRITE_PARAGRAPH_ALIGNMENT vertical, bool wrap) {
    auto layout = Graphics::Layout(text, style, Width(rect), wrap, align);
    if (!layout) return;
    layout->SetMaxHeight(std::max(1.0f, Height(rect)));
    layout->SetParagraphAlignment(vertical);
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), Brush(color),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Canvas::TextLayout(IDWriteTextLayout* layout, PointF origin, Color color) {
    target_->DrawTextLayout(origin, layout, Brush(color));
}

void Canvas::PushClip(const RectF& rect) {
    target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::PopClip() {
    target_->PopAxisAlignedClip();
}

void Canvas::PushTranslate(float dx, float dy) {
    D2D1_MATRIX_3X2_F current;
    target_->GetTransform(&current);
    transforms_.push_back(current);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * *D2D1::Matrix3x2F::ReinterpretBaseType(&current));
}

void Canvas::PopTransform() {
    if (transforms_.empty()) return;
    target_->SetTransform(transforms_.back());
    transforms_.pop_back();
}

}  // namespace nc::ui
