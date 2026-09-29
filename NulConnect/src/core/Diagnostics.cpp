#include "pch.h"
#include "core/Diagnostics.h"

#include <filesystem>

#include "core/Error.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

namespace nc {

namespace fs = std::filesystem;

namespace {

fs::path KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) result = raw;
    CoTaskMemFree(raw);
    return result;
}

void CopyFiles(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to, ec);
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) fs::copy_file(it->path(), to / it->path().filename(), fs::copy_options::overwrite_existing, ec);
    }
}

// Runs the tar.exe that ships with Windows 10 (bsdtar), which writes zip.
bool RunTar(const std::wstring& zipPath, const std::wstring& folder) {
    std::wstring tar = JoinPath(KnownFolderPath(FOLDERID_System).wstring(), L"tar.exe");
    std::wstring commandLine = L"\"" + tar + L"\" -a -c -f \"" + zipPath + L"\" -C \"" + folder + L"\" .";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tar.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) {
        return false;
    }
    DWORD exitCode = 1;
    if (WaitForSingleObject(process.hProcess, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return exitCode == 0;
}

}  // namespace

std::wstring AppLogDirectory() {
    return JoinPath(LocalDataDirectory(), L"Logs");
}

std::wstring HelperLogDirectory() {
    return (KnownFolderPath(FOLDERID_ProgramData) / L"NulConnect" / L"Logs").wstring();
}

std::wstring ExportDiagnostics(const std::string& summary) {
    fs::path desktop = KnownFolderPath(FOLDERID_Desktop);
    if (desktop.empty()) throw AppError(ErrorKind::Generic, Tr(L"Could not find the desktop folder."));
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    fs::path zip = desktop / (std::wstring(L"NulConnect-diagnostics-") + stamp + L".zip");

    fs::path staging = fs::temp_directory_path() / (std::wstring(L"nulconnect-diag-") + stamp);
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    CopyFiles(AppLogDirectory(), staging / L"app");
    CopyFiles(HelperLogDirectory(), staging / L"helper");
    HANDLE info = CreateFileW((staging / L"info.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (info != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(info, summary.data(), static_cast<DWORD>(summary.size()), &written, nullptr);
        CloseHandle(info);
    }
    bool ok = RunTar(zip.wstring(), staging.wstring());
    fs::remove_all(staging, ec);
    if (!ok) throw AppError(ErrorKind::Generic, Tr(L"Could not create the diagnostics archive."));
    Log("[Diagnostics] exported " + Narrow(zip.wstring()));
    return zip.wstring();
}

}  // namespace nc
