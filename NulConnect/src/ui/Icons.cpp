#include "pch.h"
#include "ui/Icons.h"

namespace nc::ui {

namespace {

const char* PathData(Icon icon) {
    switch (icon) {
    case Icon::Home:
        return "M4 10.2 12 3.8l8 6.4V19.5a1 1 0 0 1-1 1h-4.5v-6h-5v6H5a1 1 0 0 1-1-1z";
    case Icon::Shield:
        return "M12 3 4.5 5.8v5.7c0 4.5 3.1 8.1 7.5 9.5 4.4-1.4 7.5-5 7.5-9.5V5.8z";
    case Icon::ShieldCheck:
        return "M12 3 4.5 5.8v5.7c0 4.5 3.1 8.1 7.5 9.5 4.4-1.4 7.5-5 7.5-9.5V5.8z M8.7 12.1l2.3 2.3 4.4-4.6";
    case Icon::ShieldAlert:
        return "M12 3 4.5 5.8v5.7c0 4.5 3.1 8.1 7.5 9.5 4.4-1.4 7.5-5 7.5-9.5V5.8z M12 8v4.5 M12 15.6v.01";
    case Icon::Globe:
        return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M3.5 9h17 M3.5 15h17 "
               "M12 3c-2.4 2.5-3.6 5.5-3.6 9s1.2 6.5 3.6 9 M12 3c2.4 2.5 3.6 5.5 3.6 9s-1.2 6.5-3.6 9";
    case Icon::Person:
        return "M12 11.5a4 4 0 1 0 0-8a4 4 0 1 0 0 8z M4.5 20.5c.9-3.7 3.8-5.8 7.5-5.8s6.6 2.1 7.5 5.8";
    case Icon::Key:
        return "M15 3.5a5.5 5.5 0 1 0 0 11a5.5 5.5 0 1 0 0-11z M11 13 3.5 20.5 M6 18l2.2 2.2 M8.4 15.6l2 2 M16.4 7.6v.01";
    case Icon::Chart:
        return "M4 20.5h16 M6.5 17v-4 M10.5 17V8 M14.5 17v-6 M18.5 17V4.5";
    case Icon::Info:
        return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 11v5.5 M12 7.7v.01";
    case Icon::Power:
        return "M12 3v8.5 M7 6.2a7.5 7.5 0 1 0 10 0";
    case Icon::Server:
        return "M5 4h14a1 1 0 0 1 1 1v4.5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z "
               "M5 13.5h14a1 1 0 0 1 1 1V19a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1v-4.5a1 1 0 0 1 1-1z M7.5 7.3v.01 M7.5 16.8v.01";
    case Icon::Lock:
        return "M6.5 10.5h11a1 1 0 0 1 1 1V19.5a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1v-8a1 1 0 0 1 1-1z "
               "M8.5 10.5V7.5a3.5 3.5 0 0 1 7 0v3 M12 14.5v2";
    case Icon::Copy:
        return "M9.5 8.5h9a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1h-9a1 1 0 0 1-1-1v-10a1 1 0 0 1 1-1z "
               "M15.5 8.5V4.5a1 1 0 0 0-1-1h-9a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3";
    case Icon::Warning:
        return "M10.3 4.4 2.9 17.6A2 2 0 0 0 4.6 20.5h14.8a2 2 0 0 0 1.7-2.9L13.7 4.4a2 2 0 0 0-3.4 0z "
               "M12 9.5v4.2 M12 16.9v.01";
    case Icon::ErrorCircle:
        return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M9.2 9.2l5.6 5.6 M14.8 9.2l-5.6 5.6";
    case Icon::CheckCircle:
        return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M8.2 12.3l2.6 2.6 5-5.2";
    case Icon::Check:
        return "M5 12.5l4.5 4.5L19 7.5";
    case Icon::Close:
        return "M6 6l12 12 M18 6 6 18";
    case Icon::ArrowUp:
        return "M12 19.5v-15 M6 10.5l6-6 6 6";
    case Icon::ArrowDown:
        return "M12 4.5v15 M6 13.5l6 6 6-6";
    case Icon::SignOut:
        return "M9.5 20.5H6a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 6 3.5h3.5 M15.5 16.5 20 12l-4.5-4.5 M20 12H9.5";
    case Icon::SignIn:
        return "M14.5 3.5H18A1.5 1.5 0 0 1 19.5 5v14a1.5 1.5 0 0 1-1.5 1.5h-3.5 M9.5 16.5 14 12 9.5 7.5 M14 12H3.5";
    case Icon::Trash:
        return "M4 6.5h16 M9.5 6.5V4.5a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1v2 M6.5 6.5l.9 13a1 1 0 0 0 1 .9h7.2a1 1 0 0 0 1-.9l.9-13 "
               "M10 10.5v6 M14 10.5v6";
    case Icon::Download:
        return "M12 3.5v12 M7 10.5l5 5 5-5 M4.5 20.5h15";
    case Icon::Refresh:
        return "M19.5 12a7.5 7.5 0 1 1-2.2-5.3 M19.5 4v4.5H15";
    case Icon::Clock:
        return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 7v5l3.5 2.2";
    case Icon::Open:
        return "M14 4h6v6 M20 4l-9 9 M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5";
    case Icon::ChevronRight:
        return "M9 5.5l6.5 6.5L9 18.5";
    case Icon::ChevronDown:
        return "M5.5 9l6.5 6.5L18.5 9";
    case Icon::Menu:
        return "M4 6.5h16 M4 12h16 M4 17.5h16";
    case Icon::Folder:
        return "M3.5 6.5a1.5 1.5 0 0 1 1.5-1.5h4.5l2 2.2H19a1.5 1.5 0 0 1 1.5 1.5V17.5A1.5 1.5 0 0 1 19 19H5a1.5 1.5 0 0 1-1.5-1.5z";
    case Icon::Plug:
        return "M9 3.5v4.5 M15 3.5v4.5 M6.5 8h11v3.5a5.5 5.5 0 0 1-11 0z M12 17v3.5";
    case Icon::Network:
        return "M12 3.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M5 15.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z "
               "M19 15.5a2.5 2.5 0 1 0 0 5a2.5 2.5 0 1 0 0-5z M12 8.5v3.5 M12 12l-5.3 4.1 M12 12l5.3 4.1";
    case Icon::Swap:
        return "M4.5 8h14 M15 4.5 18.5 8 15 11.5 M19.5 16h-14 M9 12.5 5.5 16 9 19.5";
    case Icon::Document:
        return "M6.5 3.5h7l4 4V19.5a1 1 0 0 1-1 1h-10a1 1 0 0 1-1-1v-15a1 1 0 0 1 1-1z M13.5 3.5v4h4 M8.5 12.5h7 M8.5 16h7";
    case Icon::Rocket:
        return "M12 20.5c-1.5-1.2-2.5-2.9-2.5-5V9.5C9.5 6.8 10.4 4.8 12 3.5c1.6 1.3 2.5 3.3 2.5 6V15.5c0 2.1-1 3.8-2.5 5z "
               "M9.5 12.5 6.5 15v3l3-1.5 M14.5 12.5l3 2.5v3l-3-1.5 M12 9.2v.01";
    case Icon::Window:
        return "M4.5 4.5h15a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1h-15a1 1 0 0 1-1-1v-13a1 1 0 0 1 1-1z M3.5 8.5h17";
    default:
        return nullptr;
    }
}

class PathBuilder {
public:
    explicit PathBuilder(ID2D1GeometrySink* sink) : sink_(sink) {}

    void Parse(const char* data) {
        p_ = data;
        char command = 0;
        while (true) {
            SkipSeparators();
            if (!*p_) break;
            if (IsCommand(*p_)) {
                command = *p_++;
            } else if (command == 'M') {
                command = 'L';
            } else if (command == 'm') {
                command = 'l';
            }
            Execute(command);
        }
        EndFigure(false);
    }

private:
    static bool IsCommand(char c) { return std::strchr("MmLlHhVvCcSsQqAaZz", c) != nullptr && c != 0; }

    void SkipSeparators() {
        while (*p_ == ' ' || *p_ == ',' || *p_ == '\n' || *p_ == '\t') ++p_;
    }

    float Number() {
        SkipSeparators();
        char* end = nullptr;
        // Handles compact forms such as "1-1" and ".5.5".
        const char* start = p_;
        std::string token;
        if (*p_ == '-' || *p_ == '+') token.push_back(*p_++);
        bool dot = false;
        while ((*p_ >= '0' && *p_ <= '9') || (*p_ == '.' && !dot)) {
            if (*p_ == '.') dot = true;
            token.push_back(*p_++);
        }
        if (token.empty() || token == "-" || token == "+") {
            p_ = start + 1;
            return 0;
        }
        return std::strtof(token.c_str(), &end);
    }

    bool Flag() {
        SkipSeparators();
        char c = *p_;
        if (c == '0' || c == '1') {
            ++p_;
            return c == '1';
        }
        return Number() != 0;
    }

    void BeginIfNeeded() {
        if (!open_) {
            sink_->BeginFigure(start_, D2D1_FIGURE_BEGIN_FILLED);
            open_ = true;
        }
    }

    void EndFigure(bool closed) {
        if (open_) {
            sink_->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            open_ = false;
        }
    }

    void Execute(char command) {
        bool relative = command >= 'a' && command <= 'z';
        float ox = relative ? cur_.x : 0;
        float oy = relative ? cur_.y : 0;
        switch (command) {
        case 'M':
        case 'm': {
            EndFigure(false);
            float x = Number() + ox, y = Number() + oy;
            cur_ = start_ = D2D1::Point2F(x, y);
            lastControl_ = cur_;
            break;
        }
        case 'L':
        case 'l': {
            BeginIfNeeded();
            float x = Number() + ox, y = Number() + oy;
            cur_ = D2D1::Point2F(x, y);
            // Zero-length segments render as dots with round caps; nudge so
            // Direct2D does not drop them.
            if (x == prevX() && y == prevY()) cur_.x += 0.01f;
            sink_->AddLine(cur_);
            lastControl_ = cur_;
            break;
        }
        case 'H':
        case 'h': {
            BeginIfNeeded();
            cur_.x = Number() + ox;
            sink_->AddLine(cur_);
            lastControl_ = cur_;
            break;
        }
        case 'V':
        case 'v': {
            BeginIfNeeded();
            cur_.y = Number() + oy;
            sink_->AddLine(cur_);
            lastControl_ = cur_;
            break;
        }
        case 'C':
        case 'c': {
            BeginIfNeeded();
            D2D1_POINT_2F c1{Number() + ox, Number() + oy};
            D2D1_POINT_2F c2{Number() + ox, Number() + oy};
            D2D1_POINT_2F end{Number() + ox, Number() + oy};
            sink_->AddBezier(D2D1::BezierSegment(c1, c2, end));
            lastControl_ = c2;
            cur_ = end;
            break;
        }
        case 'S':
        case 's': {
            BeginIfNeeded();
            D2D1_POINT_2F c1{2 * cur_.x - lastControl_.x, 2 * cur_.y - lastControl_.y};
            D2D1_POINT_2F c2{Number() + ox, Number() + oy};
            D2D1_POINT_2F end{Number() + ox, Number() + oy};
            sink_->AddBezier(D2D1::BezierSegment(c1, c2, end));
            lastControl_ = c2;
            cur_ = end;
            break;
        }
        case 'Q':
        case 'q': {
            BeginIfNeeded();
            D2D1_POINT_2F c{Number() + ox, Number() + oy};
            D2D1_POINT_2F end{Number() + ox, Number() + oy};
            sink_->AddQuadraticBezier(D2D1::QuadraticBezierSegment(c, end));
            lastControl_ = c;
            cur_ = end;
            break;
        }
        case 'A':
        case 'a': {
            BeginIfNeeded();
            float rx = Number(), ry = Number(), rotation = Number();
            bool large = Flag(), sweep = Flag();
            D2D1_POINT_2F end{Number() + ox, Number() + oy};
            sink_->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(rx, ry), rotation,
                                           sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                           large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            cur_ = end;
            lastControl_ = cur_;
            break;
        }
        case 'Z':
        case 'z':
            EndFigure(true);
            cur_ = start_;
            break;
        default:
            ++p_;
            break;
        }
        prev_ = cur_;
    }

    float prevX() const { return prev_.x; }
    float prevY() const { return prev_.y; }

    ID2D1GeometrySink* sink_;
    const char* p_ = nullptr;
    bool open_ = false;
    D2D1_POINT_2F cur_{};
    D2D1_POINT_2F prev_{};
    D2D1_POINT_2F start_{};
    D2D1_POINT_2F lastControl_{};
};

ComPtr<ID2D1Geometry> BuildGear() {
    // Eight rounded teeth around a ring, built procedurally.
    ComPtr<ID2D1PathGeometry> path;
    Graphics::D2D()->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    const float cx = 12, cy = 12, outer = 9.2f, inner = 7.0f;
    const int teeth = 8;
    const float pi = 3.14159265f;
    for (int i = 0; i < teeth * 4; ++i) {
        float angle = (i / static_cast<float>(teeth * 4)) * 2 * pi - pi / 2;
        int phase = i % 4;
        float radius = (phase == 1 || phase == 2) ? outer : inner;
        D2D1_POINT_2F point{cx + radius * std::cos(angle), cy + radius * std::sin(angle)};
        if (i == 0) sink->BeginFigure(point, D2D1_FIGURE_BEGIN_FILLED);
        else sink->AddLine(point);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->BeginFigure(D2D1::Point2F(cx + 3, cy), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx - 3, cy), D2D1::SizeF(3, 3), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + 3, cy), D2D1::SizeF(3, 3), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return path;
}

std::map<Icon, ComPtr<ID2D1Geometry>> g_geometries;

}  // namespace

ID2D1Geometry* IconGeometry(Icon icon) {
    auto it = g_geometries.find(icon);
    if (it != g_geometries.end()) return it->second.Get();
    ComPtr<ID2D1Geometry> geometry;
    if (icon == Icon::Settings) {
        geometry = BuildGear();
    } else if (const char* data = PathData(icon)) {
        ComPtr<ID2D1PathGeometry> path;
        Graphics::D2D()->CreatePathGeometry(&path);
        ComPtr<ID2D1GeometrySink> sink;
        path->Open(&sink);
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        PathBuilder(sink.Get()).Parse(data);
        sink->Close();
        geometry = path;
    }
    g_geometries[icon] = geometry;
    return geometry.Get();
}

void DrawIcon(Canvas& canvas, Icon icon, const RectF& box, Color color, float size, float strokeWidth) {
    ID2D1Geometry* geometry = IconGeometry(icon);
    if (!geometry) return;
    ID2D1RenderTarget* target = canvas.Target();
    float scale = size / 24.0f;
    float x = box.left + (Width(box) - size) / 2;
    float y = box.top + (Height(box) - size) / 2;
    D2D1_MATRIX_3X2_F previous;
    target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) *
                         *D2D1::Matrix3x2F::ReinterpretBaseType(&previous));
    // Keep strokes about 1 DIP wide at 16 DIPs, slightly heavier when small.
    float width = strokeWidth > 0 ? strokeWidth : (size <= 16.0f ? 1.25f : (size <= 24 ? 1.5f : 1.75f));
    target->DrawGeometry(geometry, canvas.Brush(color), width / scale, Graphics::RoundStroke());
    target->SetTransform(previous);
}

}  // namespace nc::ui
