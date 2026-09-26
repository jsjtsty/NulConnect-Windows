#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace nc {

enum class Language { English, ChineseSimplified, ChineseTraditional, Japanese, German, French, Spanish };

void InitializeLocalization();
Language CurrentLanguage();

// Looks up the translation for an English source string. Unknown keys are
// returned unchanged.
const std::wstring& Tr(std::wstring_view key);
std::wstring TrFormat(std::wstring_view key, std::initializer_list<std::wstring_view> arguments);

}  // namespace nc
