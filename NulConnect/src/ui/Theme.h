#pragma once

#include <d2d1.h>

namespace nc::ui {

using Color = D2D1_COLOR_F;

constexpr Color Rgba(unsigned rgb, float alpha = 1.0f) {
    return Color{((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
}

Color WithAlpha(Color color, float alpha);
Color Mix(Color a, Color b, float t);

// Fluent-style color tokens. Values follow the WinUI 3 light/dark resources.
struct Palette {
    bool dark = false;
    bool translucent = false;  // Mica backdrop behind the window

    Color windowBackground;    // behind everything when not translucent
    Color paneBackground;      // navigation pane
    Color contentBackground;   // content layer
    Color cardBackground;
    Color cardBackgroundHover;
    Color cardBackgroundPressed;
    Color cardStroke;
    Color divider;
    Color flyoutBackground;
    Color flyoutStroke;

    Color textPrimary;
    Color textSecondary;
    Color textTertiary;
    Color textDisabled;
    Color textOnAccent;
    Color textOnAccentDisabled;

    Color controlFill;
    Color controlFillHover;
    Color controlFillPressed;
    Color controlFillDisabled;
    Color controlStroke;
    Color controlStrokeBottom;
    Color controlStrongStroke;
    Color controlStrongFill;
    Color inputFill;
    Color inputFillFocused;
    Color subtleHover;
    Color subtlePressed;

    Color accent;
    Color accentHover;
    Color accentPressed;
    Color accentDisabled;
    Color accentText;  // accent-colored text/links on the background

    Color success;
    Color caution;
    Color critical;
    Color neutral;
    Color successBackground;
    Color cautionBackground;
    Color criticalBackground;
    Color infoBackground;

    Color smoke;
    Color focusStroke;
    Color brand;       // NulConnect teal, used for the logo only
    Color brandDeep;
};

class Theme {
public:
    // Re-reads the system app theme, accent color and backdrop support.
    static void Refresh();
    static const Palette& Current();
    static bool IsDark();
    static bool UseMica();
    // Taskbar/notification-area theme, which can differ from the app theme.
    static bool IsTaskbarDark();

    static constexpr float ControlRadius = 4.0f;
    static constexpr float OverlayRadius = 8.0f;
};

}  // namespace nc::ui
