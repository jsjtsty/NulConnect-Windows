#include "pch.h"
#include "core/Localization.h"
#include "core/Str.h"

namespace nc {

namespace {

constexpr size_t kLanguageCount = 7;

struct Entry {
    const wchar_t* key;
    const wchar_t* values[kLanguageCount];
};

const Entry kEntries[] = {
#include "core/Localization.generated.inc"
};

Language g_language = Language::English;
std::unordered_map<std::wstring_view, std::wstring> g_table;
std::mutex g_missingMutex;
std::unordered_map<std::wstring, std::wstring> g_missing;

Language DetectLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    LANGID id = GetUserDefaultUILanguage();
    if (!LCIDToLocaleName(MAKELCID(id, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) {
        return Language::English;
    }
    std::wstring locale = ToLower(std::wstring(name));
    if (locale.rfind(L"zh", 0) == 0) {
        if (locale.find(L"hant") != std::wstring::npos || locale == L"zh-tw" || locale == L"zh-hk" || locale == L"zh-mo") {
            return Language::ChineseTraditional;
        }
        return Language::ChineseSimplified;
    }
    if (locale.rfind(L"ja", 0) == 0) return Language::Japanese;
    if (locale.rfind(L"de", 0) == 0) return Language::German;
    if (locale.rfind(L"fr", 0) == 0) return Language::French;
    if (locale.rfind(L"es", 0) == 0) return Language::Spanish;
    return Language::English;
}

}  // namespace

void InitializeLocalization() {
    g_language = DetectLanguage();
    size_t column = static_cast<size_t>(g_language);
    g_table.reserve(std::size(kEntries));
    for (const auto& entry : kEntries) {
        g_table.emplace(entry.key, entry.values[column]);
    }
}

Language CurrentLanguage() {
    return g_language;
}

const std::wstring& Tr(std::wstring_view key) {
    auto it = g_table.find(key);
    if (it != g_table.end()) {
        return it->second;
    }
    std::lock_guard lock(g_missingMutex);
    auto [missing, inserted] = g_missing.emplace(std::wstring(key), std::wstring(key));
    return missing->second;
}

std::wstring TrFormat(std::wstring_view key, std::initializer_list<std::wstring_view> arguments) {
    return Format(Tr(key), arguments);
}

}  // namespace nc
