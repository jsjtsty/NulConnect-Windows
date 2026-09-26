#include "pch.h"
#include "ui/Theme.h"
#include "core/Platform.h"

namespace nc::ui {

namespace {

Palette g_palette;
bool g_taskbarDark = true;
bool g_useMica = false;

constexpr const wchar_t* kPersonalize = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

bool ReadAppsUseDark() {
    if (!IsWindows10OrGreater()) return false;
    auto value = ReadRegistryDword(HKEY_CURRENT_USER, kPersonalize, L"AppsUseLightTheme");
    return value && *value == 0;
}

bool ReadTaskbarDark() {
    if (!IsWindows10OrGreater()) return true;
    auto value = ReadRegistryDword(HKEY_CURRENT_USER, kPersonalize, L"SystemUsesLightTheme");
    return !value || *value == 0;
}

bool IsHighContrast() {
    HIGHCONTRASTW info{sizeof(info)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(info), &info, 0) && (info.dwFlags & HCF_HIGHCONTRASTON);
}

Color FromAbgr(DWORD abgr) {
    return Rgba(((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF));
}

// Windows 10+ stores eight accent shades (Light3..Dark3). Fluent uses Dark1
// for accent fills in light mode and Light2 in dark mode.
std::optional<Color> ReadAccent(bool dark) {
    auto palette = ReadRegistryBinary(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                                      L"AccentPalette");
    if (palette && palette->size() >= 32) {
        size_t index = dark ? 1 : 4;
        const BYTE* p = palette->data() + index * 4;
        return Rgba((p[0] << 16) | (p[1] << 8) | p[2]);
    }
    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
        Color base = Rgba(colorization & 0xFFFFFF);
        return dark ? Mix(base, Rgba(0xFFFFFF), 0.35f) : Mix(base, Rgba(0x000000), 0.15f);
    }
    return std::nullopt;
}

float Luminance(Color c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

void BuildLight(Palette& p) {
    p.dark = false;
    p.windowBackground = Rgba(0xF3F3F3);
    p.paneBackground = p.translucent ? Rgba(0xF3F3F3, 0.0f) : Rgba(0xF3F3F3);
    p.contentBackground = p.translucent ? Rgba(0xFFFFFF, 0.5f) : Rgba(0xF9F9F9);
    p.cardBackground = p.translucent ? Rgba(0xFFFFFF, 0.7f) : Rgba(0xFFFFFF);
    p.cardBackgroundHover = p.translucent ? Rgba(0xF9F9F9, 0.5f) : Rgba(0xF6F6F6);
    p.cardBackgroundPressed = p.translucent ? Rgba(0xF9F9F9, 0.3f) : Rgba(0xF2F2F2);
    p.cardStroke = Rgba(0x000000, 0.0578f);
    p.divider = Rgba(0x000000, 0.0803f);
    p.flyoutBackground = Rgba(0xFCFCFC);
    p.flyoutStroke = Rgba(0x000000, 0.0578f);

    p.textPrimary = Rgba(0x000000, 0.8956f);
    p.textSecondary = Rgba(0x000000, 0.6063f);
    p.textTertiary = Rgba(0x000000, 0.4458f);
    p.textDisabled = Rgba(0x000000, 0.3614f);
    p.textOnAccent = Rgba(0xFFFFFF);
    p.textOnAccentDisabled = Rgba(0xFFFFFF);

    p.controlFill = Rgba(0xFFFFFF, 0.7f);
    p.controlFillHover = Rgba(0xF9F9F9, 0.5f);
    p.controlFillPressed = Rgba(0xF9F9F9, 0.3f);
    p.controlFillDisabled = Rgba(0xF9F9F9, 0.3f);
    p.controlStroke = Rgba(0x000000, 0.0578f);
    p.controlStrokeBottom = Rgba(0x000000, 0.1622f);
    p.controlStrongStroke = Rgba(0x000000, 0.4458f);
    p.controlStrongFill = Rgba(0x000000, 0.4458f);
    p.inputFill = Rgba(0xFFFFFF, 0.7f);
    p.inputFillFocused = Rgba(0xFFFFFF);
    p.subtleHover = Rgba(0x000000, 0.0373f);
    p.subtlePressed = Rgba(0x000000, 0.0241f);

    p.accent = Rgba(0x005FB8);
    p.success = Rgba(0x0F7B0F);
    p.caution = Rgba(0x9D5D00);
    p.critical = Rgba(0xC42B1C);
    p.neutral = Rgba(0x000000, 0.4458f);
    p.successBackground = Rgba(0xDFF6DD);
    p.cautionBackground = Rgba(0xFFF4CE);
    p.criticalBackground = Rgba(0xFDE7E9);
    p.infoBackground = Rgba(0xF6F6F6, 0.9f);
    p.smoke = Rgba(0x000000, 0.3f);
    p.focusStroke = Rgba(0x000000, 0.8956f);
    p.brand = Rgba(0x0B8FA8);
    p.brandDeep = Rgba(0x08708D);
}

void BuildDark(Palette& p) {
    p.dark = true;
    p.windowBackground = Rgba(0x202020);
    p.paneBackground = p.translucent ? Rgba(0x202020, 0.0f) : Rgba(0x202020);
    p.contentBackground = p.translucent ? Rgba(0x3A3A3A, 0.3f) : Rgba(0x272727);
    p.cardBackground = p.translucent ? Rgba(0xFFFFFF, 0.0512f) : Rgba(0x2B2B2B);
    p.cardBackgroundHover = p.translucent ? Rgba(0xFFFFFF, 0.0837f) : Rgba(0x323232);
    p.cardBackgroundPressed = p.translucent ? Rgba(0xFFFFFF, 0.0326f) : Rgba(0x282828);
    p.cardStroke = Rgba(0x000000, 0.1f);
    p.divider = Rgba(0xFFFFFF, 0.0837f);
    p.flyoutBackground = Rgba(0x2C2C2C);
    p.flyoutStroke = Rgba(0x000000, 0.2f);

    p.textPrimary = Rgba(0xFFFFFF);
    p.textSecondary = Rgba(0xFFFFFF, 0.786f);
    p.textTertiary = Rgba(0xFFFFFF, 0.5442f);
    p.textDisabled = Rgba(0xFFFFFF, 0.3628f);
    p.textOnAccent = Rgba(0x000000);
    p.textOnAccentDisabled = Rgba(0xFFFFFF, 0.5302f);

    p.controlFill = Rgba(0xFFFFFF, 0.0605f);
    p.controlFillHover = Rgba(0xFFFFFF, 0.0837f);
    p.controlFillPressed = Rgba(0xFFFFFF, 0.0326f);
    p.controlFillDisabled = Rgba(0xFFFFFF, 0.0419f);
    p.controlStroke = Rgba(0xFFFFFF, 0.0698f);
    p.controlStrokeBottom = Rgba(0xFFFFFF, 0.0930f);
    p.controlStrongStroke = Rgba(0xFFFFFF, 0.5442f);
    p.controlStrongFill = Rgba(0xFFFFFF, 0.5442f);
    p.inputFill = Rgba(0xFFFFFF, 0.0605f);
    p.inputFillFocused = Rgba(0x1E1E1E, 0.7f);
    p.subtleHover = Rgba(0xFFFFFF, 0.0605f);
    p.subtlePressed = Rgba(0xFFFFFF, 0.0419f);

    p.accent = Rgba(0x60CDFF);
    p.success = Rgba(0x6CCB5F);
    p.caution = Rgba(0xFCE100);
    p.critical = Rgba(0xFF99A4);
    p.neutral = Rgba(0xFFFFFF, 0.5442f);
    p.successBackground = Rgba(0x393D1B);
    p.cautionBackground = Rgba(0x433519);
    p.criticalBackground = Rgba(0x442726);
    p.infoBackground = Rgba(0xFFFFFF, 0.0326f);
    p.smoke = Rgba(0x000000, 0.3f);
    p.focusStroke = Rgba(0xFFFFFF);
    p.brand = Rgba(0x35D9CF);
    p.brandDeep = Rgba(0x18ADC5);
}

}  // namespace

Color WithAlpha(Color color, float alpha) {
    color.a = alpha;
    return color;
}

Color Mix(Color a, Color b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

void Theme::Refresh() {
    bool dark = ReadAppsUseDark() && !IsHighContrast();
    // NULCONNECT_THEME=dark|light overrides the system setting (testing).
    wchar_t forced[16] = {};
    if (GetEnvironmentVariableW(L"NULCONNECT_THEME", forced, 16)) {
        if (_wcsicmp(forced, L"dark") == 0) dark = true;
        else if (_wcsicmp(forced, L"light") == 0) dark = false;
    }
    g_taskbarDark = ReadTaskbarDark();
    g_useMica = SupportsMica() && !IsHighContrast();
    // NULCONNECT_BACKDROP=none renders the solid (Windows 10 style) surfaces.
    wchar_t backdrop[16] = {};
    if (GetEnvironmentVariableW(L"NULCONNECT_BACKDROP", backdrop, 16) && _wcsicmp(backdrop, L"none") == 0) {
        g_useMica = false;
    }
    Palette p;
    p.translucent = g_useMica;
    if (dark) BuildDark(p);
    else BuildLight(p);
    if (auto accent = ReadAccent(dark)) {
        p.accent = *accent;
    }
    p.accentHover = WithAlpha(p.accent, 0.9f);
    p.accentPressed = WithAlpha(p.accent, 0.8f);
    p.accentDisabled = dark ? Rgba(0xFFFFFF, 0.1581f) : Rgba(0x000000, 0.2169f);
    // Text on the accent fill must stay readable for any user accent.
    p.textOnAccent = Luminance(p.accent) > 0.5f ? Rgba(0x000000) : Rgba(0xFFFFFF);
    p.accentText = dark ? Mix(p.accent, Rgba(0xFFFFFF), 0.1f) : Mix(p.accent, Rgba(0x000000), 0.1f);
    g_palette = p;
}

const Palette& Theme::Current() {
    return g_palette;
}

bool Theme::IsDark() {
    return g_palette.dark;
}

bool Theme::UseMica() {
    return g_useMica;
}

bool Theme::IsTaskbarDark() {
    return g_taskbarDark;
}

}  // namespace nc::ui
