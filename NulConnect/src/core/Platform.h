#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nc {

// ---- Paths ---------------------------------------------------------------

std::wstring ExecutableDirectory();
std::wstring ExecutablePath();
// %APPDATA%\NulConnect (roaming settings) and %LOCALAPPDATA%\NulConnect
// (logs, WebView2 data); both are created on demand.
std::wstring AppDataDirectory();
std::wstring LocalDataDirectory();
std::wstring JoinPath(const std::wstring& base, const std::wstring& name);

bool ReadFileBytes(const std::wstring& path, std::string& out);
// Writes through a temporary file and renames it over the target.
bool WriteFileAtomic(const std::wstring& path, const std::string& data);
bool FileExists(const std::wstring& path);
bool DeleteFileIfExists(const std::wstring& path);
void DeleteDirectoryTree(const std::wstring& path);

// ---- OS version ------------------------------------------------------------

struct OsVersion {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned build = 0;
};

const OsVersion& GetOsVersion();
bool IsWindows10OrGreater();
bool IsWindows11OrGreater();  // build 22000+
bool SupportsMica();          // build 22621+

// ---- Crypto ----------------------------------------------------------------

// DPAPI, bound to the current Windows user.
std::optional<std::string> ProtectData(const std::string& plain);
std::optional<std::string> UnprotectData(const std::string& protectedData);
std::string RandomHex(size_t bytes);
std::string Base64Encode(const std::string& data);
std::optional<std::string> Base64Decode(const std::string& text);
std::string NewUuid();

// ---- Registry ----------------------------------------------------------------

std::optional<DWORD> ReadRegistryDword(HKEY root, const wchar_t* path, const wchar_t* name);
std::optional<std::wstring> ReadRegistryString(HKEY root, const wchar_t* path, const wchar_t* name);
std::optional<std::vector<BYTE>> ReadRegistryBinary(HKEY root, const wchar_t* path, const wchar_t* name);

}  // namespace nc
