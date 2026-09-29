#include "pch.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "libreatrust.h"

namespace nc {

namespace {

std::mutex g_logMutex;
std::wstring g_logPath;
constexpr long long kMaxLogBytes = 4LL * 1024 * 1024;
std::atomic<bool> g_mirrorToLibrary{false};

}  // namespace

void LogInit() {
    std::lock_guard lock(g_logMutex);
    std::wstring dir = JoinPath(LocalDataDirectory(), L"Logs");
    CreateDirectoryW(dir.c_str(), nullptr);
    g_logPath = JoinPath(dir, L"NulConnect.log");
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &data)) {
        long long size = (static_cast<long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        if (size > kMaxLogBytes) {
            MoveFileExW(g_logPath.c_str(), JoinPath(dir, L"NulConnect.old.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

std::wstring LogFilePath() {
    std::lock_guard lock(g_logMutex);
    return g_logPath;
}

void SetDiagnosticLogging(bool enabled) {
    atr_set_verbose_logging(enabled);
    g_mirrorToLibrary = enabled;
}

void Log(std::string_view message) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char prefix[64];
    sprintf_s(prefix, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] ", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId());
    std::string line = prefix;
    line.append(message);
    line += "\r\n";
    OutputDebugStringW(Widen(line).c_str());
    if (g_mirrorToLibrary) atr_log_write(("[NulConnect] " + std::string(message)).c_str());
    std::lock_guard lock(g_logMutex);
    if (g_logPath.empty()) return;
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

void Log(std::wstring_view message) {
    Log(std::string_view(Narrow(message)));
}

std::string LoggableUrl(std::string_view url) {
    size_t cut = url.find_first_of("?#");
    return std::string(cut == std::string_view::npos ? url : url.substr(0, cut));
}

}  // namespace nc
