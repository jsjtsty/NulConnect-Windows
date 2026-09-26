#pragma once

#include "ui/Theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace nc::ui {

using Microsoft::WRL::ComPtr;
using RectF = D2D1_RECT_F;
using PointF = D2D1_POINT_2F;

inline RectF MakeRect(float x, float y, float w, float h) { return RectF{x, y, x + w, y + h}; }
inline float Width(const RectF& r) { return r.right - r.left; }
inline float Height(const RectF& r) { return r.bottom - r.top; }
inline bool Contains(const RectF& r, PointF p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
inline RectF Inset(const RectF& r, float dx, float dy) { return RectF{r.left + dx, r.top + dy, r.right - dx, r.bottom - dy}; }
inline RectF Offset(const RectF& r, float dx, float dy) { return RectF{r.left + dx, r.top + dy, r.right + dx, r.bottom + dy}; }

enum class TextStyle {
    Caption,      // 12
    Body,         // 14
    BodyStrong,   // 14 semibold
    BodyLarge,    // 18
    Subtitle,     // 20 semibold
    Title,        // 28 semibold
    TitleLarge,   // 40 semibold
    Monospace,    // 13
};

float FontSize(TextStyle style);

class Graphics {
public:
    static bool Initialize();
    static ID2D1Factory* D2D();
    static IDWriteFactory* DWrite();
    static IWICImagingFactory* Wic();

    static IDWriteTextFormat* Format(TextStyle style);
    // A text layout for `text`. `maxWidth` <= 0 means unbounded (no wrap).
    static ComPtr<IDWriteTextLayout> Layout(std::wstring_view text, TextStyle style, float maxWidth, bool wrap = true,
                                            DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    static D2D1_SIZE_F Measure(std::wstring_view text, TextStyle style, float maxWidth = 0, bool wrap = true);
    static ID2D1StrokeStyle* RoundStroke();
};

// Monotonic seconds, for animations.
double Now();

// Thin drawing helper over a render target with a reusable solid brush.
class Canvas {
public:
    explicit Canvas(ID2D1RenderTarget* target);

    ID2D1RenderTarget* Target() const { return target_; }
    ID2D1SolidColorBrush* Brush(Color color);

    void FillRect(const RectF& rect, Color color);
    void FillRoundRect(const RectF& rect, float radius, Color color);
    void StrokeRoundRect(const RectF& rect, float radius, Color color, float width = 1.0f);
    void FillEllipse(PointF center, float rx, float ry, Color color);
    void StrokeEllipse(PointF center, float rx, float ry, Color color, float width = 1.0f);
    void Line(PointF a, PointF b, Color color, float width = 1.0f, bool round = false);
    // Draws a control border whose bottom edge is darker (Fluent elevation).
    void ControlBorder(const RectF& rect, float radius, Color stroke, Color bottomStroke);

    void Text(std::wstring_view text, TextStyle style, const RectF& rect, Color color,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT vertical = DWRITE_PARAGRAPH_ALIGNMENT_NEAR, bool wrap = true);
    void TextLayout(IDWriteTextLayout* layout, PointF origin, Color color);

    void PushClip(const RectF& rect);
    void PopClip();
    void PushTranslate(float dx, float dy);
    void PopTransform();

private:
    ID2D1RenderTarget* target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<D2D1_MATRIX_3X2_F> transforms_;
};

}  // namespace nc::ui
