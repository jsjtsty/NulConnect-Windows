#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace nc {

std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view utf16);

std::string Trim(std::string_view value);
std::wstring Trim(std::wstring_view value);
std::string TrimChars(std::string_view value, std::string_view chars);
std::string ToLower(std::string value);
std::wstring ToLower(std::wstring value);
bool StartsWith(std::string_view value, std::string_view prefix);
bool EndsWith(std::string_view value, std::string_view suffix);
bool Contains(std::string_view value, std::string_view needle);
std::vector<std::string> Split(std::string_view value, char separator);

// Replaces "%1$@", "%2$@", ... and sequential "%@" placeholders, which is the
// placeholder syntax of the shared localization tables.
std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> arguments);

std::wstring FormatBytes(unsigned long long bytes);
std::wstring FormatRate(double bytesPerSecond);
std::wstring FormatDuration(double seconds);

}  // namespace nc
