#include "pch.h"
#include "core/Str.h"

namespace nc {

std::wstring Widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), length);
    return result;
}

std::string Narrow(std::wstring_view utf16) {
    if (utf16.empty()) {
        return {};
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string TrimChars(std::string_view value, std::string_view chars) {
    size_t begin = value.find_first_not_of(chars);
    if (begin == std::string_view::npos) {
        return {};
    }
    size_t end = value.find_last_not_of(chars);
    return std::string(value.substr(begin, end - begin + 1));
}

std::string Trim(std::string_view value) {
    return TrimChars(value, " \t\r\n");
}

std::wstring Trim(std::wstring_view value) {
    constexpr std::wstring_view whitespace = L" \t\r\n";
    size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::wstring_view::npos) {
        return {};
    }
    size_t end = value.find_last_not_of(whitespace);
    return std::wstring(value.substr(begin, end - begin + 1));
}

std::string ToLower(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::wstring ToLower(std::wstring value) {
    if (!value.empty()) {
        CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    }
    return value;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

std::vector<std::string> Split(std::string_view value, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t index = value.find(separator, start);
        if (index == std::string_view::npos) {
            parts.emplace_back(value.substr(start));
            break;
        }
        parts.emplace_back(value.substr(start, index - start));
        start = index + 1;
    }
    return parts;
}

std::wstring Format(std::wstring_view pattern, std::initializer_list<std::wstring_view> arguments) {
    std::vector<std::wstring_view> args(arguments);
    std::wstring result;
    result.reserve(pattern.size() + 32);
    size_t sequential = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        wchar_t c = pattern[i];
        if (c != L'%' || i + 1 >= pattern.size()) {
            result.push_back(c);
            continue;
        }
        wchar_t next = pattern[i + 1];
        if (next == L'@') {
            if (sequential < args.size()) {
                result.append(args[sequential]);
            }
            ++sequential;
            ++i;
            continue;
        }
        if (next == L'%') {
            result.push_back(L'%');
            ++i;
            continue;
        }
        if (next >= L'1' && next <= L'9' && i + 3 < pattern.size() && pattern[i + 2] == L'$' && pattern[i + 3] == L'@') {
            size_t index = static_cast<size_t>(next - L'1');
            if (index < args.size()) {
                result.append(args[index]);
            }
            i += 3;
            continue;
        }
        result.push_back(c);
    }
    return result;
}

static std::wstring FormatScaled(double value, const wchar_t* const* units, size_t unitCount, const wchar_t* suffix) {
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < unitCount) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64];
    if (unit == 0) {
        swprintf_s(buffer, L"%.0f %s%s", value, units[unit], suffix);
    } else if (value < 10.0) {
        swprintf_s(buffer, L"%.2f %s%s", value, units[unit], suffix);
    } else if (value < 100.0) {
        swprintf_s(buffer, L"%.1f %s%s", value, units[unit], suffix);
    } else {
        swprintf_s(buffer, L"%.0f %s%s", value, units[unit], suffix);
    }
    return buffer;
}

std::wstring FormatBytes(unsigned long long bytes) {
    static const wchar_t* const units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    return FormatScaled(static_cast<double>(bytes), units, 5, L"");
}

std::wstring FormatRate(double bytesPerSecond) {
    static const wchar_t* const units[] = {L"B", L"KB", L"MB", L"GB"};
    return FormatScaled(bytesPerSecond < 0 ? 0 : bytesPerSecond, units, 4, L"/s");
}

std::wstring FormatDuration(double seconds) {
    long long total = seconds < 0 ? 0 : static_cast<long long>(seconds);
    long long hours = total / 3600;
    long long minutes = (total % 3600) / 60;
    long long secs = total % 60;
    wchar_t buffer[64];
    if (hours > 0) {
        swprintf_s(buffer, L"%lld:%02lld:%02lld", hours, minutes, secs);
    } else {
        swprintf_s(buffer, L"%02lld:%02lld", minutes, secs);
    }
    return buffer;
}

}  // namespace nc
