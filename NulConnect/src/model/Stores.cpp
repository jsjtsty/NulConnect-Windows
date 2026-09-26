#include "pch.h"
#include "model/Stores.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

namespace nc {

namespace {

std::wstring StorePath(const wchar_t* name) {
    return JoinPath(AppDataDirectory(), name);
}

std::optional<nlohmann::json> ReadJson(const std::wstring& path) {
    std::string data;
    if (!ReadFileBytes(path, data)) return std::nullopt;
    auto json = nlohmann::json::parse(data, nullptr, false);
    if (json.is_discarded()) {
        Log("[Store] ignoring unreadable " + Narrow(path));
        return std::nullopt;
    }
    return json;
}

void WriteJson(const std::wstring& path, const nlohmann::json& json) {
    if (!WriteFileAtomic(path, json.dump(2))) {
        throw AppError(ErrorKind::Internal, TrFormat(L"Could not save: %1$@", {path}));
    }
}

std::optional<std::string> ReadProtected(const std::wstring& path) {
    std::string data;
    if (!ReadFileBytes(path, data)) return std::nullopt;
    auto plain = UnprotectData(data);
    if (!plain) Log("[Store] could not decrypt " + Narrow(path));
    return plain;
}

void WriteProtected(const std::wstring& path, const std::string& plain) {
    auto encrypted = ProtectData(plain);
    if (!encrypted || !WriteFileAtomic(path, *encrypted)) {
        throw AppError(ErrorKind::Internal, TrFormat(L"Could not save: %1$@", {path}));
    }
}

bool IsValidDeviceId(const std::string& value) {
    return value.size() == 32 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

}  // namespace

Profile ProfileStore::Load() const {
    auto json = ReadJson(StorePath(L"profile.json"));
    if (!json || !json->is_object()) return Profile{};
    try {
        return json->get<Profile>();
    } catch (const std::exception&) {
        return Profile{};
    }
}

void ProfileStore::Save(const Profile& profile) const {
    WriteJson(StorePath(L"profile.json"), profile);
}

AppSettings SettingsStore::Load() const {
    auto json = ReadJson(StorePath(L"settings.json"));
    if (!json || !json->is_object()) return AppSettings{};
    try {
        return json->get<AppSettings>();
    } catch (const std::exception&) {
        return AppSettings{};
    }
}

void SettingsStore::Save(const AppSettings& settings) const {
    WriteJson(StorePath(L"settings.json"), settings);
}

void SessionVault::Save(const atr::SessionMaterial& material) const {
    nlohmann::json json = material;
    WriteProtected(StorePath(L"session.dat"), json.dump());
    WriteJson(StorePath(L"session-summary.json"), SessionSummary::From(material));
}

std::optional<atr::SessionMaterial> SessionVault::Load() const {
    auto plain = ReadProtected(StorePath(L"session.dat"));
    if (!plain) return std::nullopt;
    auto json = nlohmann::json::parse(*plain, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::nullopt;
    try {
        return json.get<atr::SessionMaterial>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<SessionSummary> SessionVault::LoadSummary() const {
    auto json = ReadJson(StorePath(L"session-summary.json"));
    if (!json || !json->is_object()) return std::nullopt;
    try {
        return json->get<SessionSummary>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SessionVault::LoadOrCreateDeviceId() const {
    std::wstring path = StorePath(L"device-id.dat");
    if (auto stored = ReadProtected(path); stored && IsValidDeviceId(*stored)) {
        return *stored;
    }
    std::string deviceId = RandomHex(16);
    WriteProtected(path, deviceId);
    return deviceId;
}

void SessionVault::Clear() const {
    if (!DeleteFileIfExists(StorePath(L"session.dat")) || !DeleteFileIfExists(StorePath(L"session-summary.json"))) {
        throw AppError(ErrorKind::Internal, Tr(L"Could not clear session: %1$@"));
    }
}

void ResourceStore::Save(const atr::ResourceSnapshot& snapshot) const {
    nlohmann::json json = snapshot;
    WriteProtected(StorePath(L"resource-snapshot.dat"), json.dump());
}

std::optional<atr::ResourceSnapshot> ResourceStore::Load() const {
    auto plain = ReadProtected(StorePath(L"resource-snapshot.dat"));
    if (!plain) return std::nullopt;
    auto json = nlohmann::json::parse(*plain, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::nullopt;
    try {
        return json.get<atr::ResourceSnapshot>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void ResourceStore::Delete() const {
    DeleteFileIfExists(StorePath(L"resource-snapshot.dat"));
}

}  // namespace nc
