#pragma once

#include "ui/Graphics.h"

namespace nc::ui {

// Outline icons on a 24x24 grid, drawn as stroked geometry so they scale
// crisply at any DPI and do not depend on icon fonts (absent on Windows 7).
enum class Icon {
    None,
    Home,
    Shield,
    ShieldCheck,
    ShieldAlert,
    Globe,
    Settings,
    Person,
    Key,
    Chart,
    Info,
    Power,
    Server,
    Lock,
    Copy,
    Warning,
    ErrorCircle,
    CheckCircle,
    Check,
    Close,
    ArrowUp,
    ArrowDown,
    SignOut,
    SignIn,
    Trash,
    Download,
    Refresh,
    Clock,
    Open,
    ChevronRight,
    ChevronDown,
    Menu,
    Folder,
    Plug,
    Network,
    Swap,
    Document,
    Rocket,
    Window,
};

// Draws `icon` centered in `box`, scaled to `size` DIPs.
void DrawIcon(Canvas& canvas, Icon icon, const RectF& box, Color color, float size = 16.0f, float strokeWidth = 0);
// Adds a small filled circle in the icon grid (used for status badges).
ID2D1Geometry* IconGeometry(Icon icon);

}  // namespace nc::ui
