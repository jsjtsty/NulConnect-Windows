#include "pch.h"
#include "core/Platform.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "rpcrt4.lib")

#include <bcrypt.h>

namespace nc {

std::wstring ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ExecutableDirectory() {
    std::wstring path = ExecutablePath();
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

static std::wstring KnownFolder(int csidl) {
    wchar_t buffer[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, buffer))) {
        return buffer;
    }
    return ExecutableDirectory();
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& name) {
    if (base.empty()) return name;
    if (base.back() == L'\\' || base.back() == L'/') return base + name;
    return base + L"\\" + name;
}

std::wstring AppDataDirectory() {
    std::wstring path = JoinPath(KnownFolder(CSIDL_APPDATA), L"NulConnect");
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

std::wstring LocalDataDirectory() {
    std::wstring path = JoinPath(KnownFolder(CSIDL_LOCAL_APPDATA), L"NulConnect");
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 64LL * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = out.empty() || ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    out.resize(read);
    return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = data.empty() || WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DeleteFileIfExists(const std::wstring& path) {
    if (DeleteFileW(path.c_str())) return true;
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void DeleteDirectoryTree(const std::wstring& path) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(JoinPath(path, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child = JoinPath(path, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirectoryTree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}

const OsVersion& GetOsVersion() {
    static const OsVersion version = [] {
        OsVersion result;
        // GetVersionEx lies to unmanifested callers; RtlGetVersion does not.
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtlGetVersion) {
                OSVERSIONINFOW info{sizeof(info)};
                if (rtlGetVersion(&info) == 0) {
                    result.major = info.dwMajorVersion;
                    result.minor = info.dwMinorVersion;
                    result.build = info.dwBuildNumber;
                }
            }
        }
        return result;
    }();
    return version;
}

bool IsWindows10OrGreater() {
    return GetOsVersion().major >= 10;
}

bool IsWindows11OrGreater() {
    return GetOsVersion().major >= 10 && GetOsVersion().build >= 22000;
}

bool SupportsMica() {
    return GetOsVersion().major >= 10 && GetOsVersion().build >= 22621;
}

std::optional<std::string> ProtectData(const std::string& plain) {
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"NulConnect", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::optional<std::string> UnprotectData(const std::string& protectedData) {
    DATA_BLOB input{static_cast<DWORD>(protectedData.size()), reinterpret_cast<BYTE*>(const_cast<char*>(protectedData.data()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}

std::string RandomHex(size_t bytes) {
    std::vector<unsigned char> buffer(bytes);
    BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes * 2);
    for (unsigned char byte : buffer) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

std::string Base64Encode(const std::string& data) {
    if (data.empty()) return {};
    const auto* bytes = reinterpret_cast<const BYTE*>(data.data());
    DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD length = 0;
    if (!CryptBinaryToStringA(bytes, static_cast<DWORD>(data.size()), flags, nullptr, &length)) return {};
    std::string out(length, '\0');
    if (!CryptBinaryToStringA(bytes, static_cast<DWORD>(data.size()), flags, out.data(), &length)) return {};
    out.resize(length);
    return out;
}

std::optional<std::string> Base64Decode(const std::string& text) {
    if (text.empty()) return std::string();
    DWORD length = 0;
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &length,
                              nullptr, nullptr)) {
        return std::nullopt;
    }
    std::string out(length, '\0');
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
                              reinterpret_cast<BYTE*>(out.data()), &length, nullptr, nullptr)) {
        return std::nullopt;
    }
    out.resize(length);
    return out;
}

std::string NewUuid() {
    UUID uuid{};
    UuidCreate(&uuid);
    RPC_CSTR text = nullptr;
    std::string result;
    if (UuidToStringA(&uuid, &text) == RPC_S_OK) {
        result = reinterpret_cast<char*>(text);
        RpcStringFreeA(&text);
    }
    return result;
}

std::optional<DWORD> ReadRegistryDword(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        return value;
    }
    return std::nullopt;
}

std::optional<std::wstring> ReadRegistryString(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(wcslen(value.c_str()));
    return value;
}

std::optional<std::vector<BYTE>> ReadRegistryBinary(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(root, path, name, RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::vector<BYTE> value(size);
    if (RegGetValueW(root, path, name, RRF_RT_REG_BINARY, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(size);
    return value;
}

}  // namespace nc
