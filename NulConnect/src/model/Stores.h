#pragma once

#include "model/Types.h"

#include <optional>
#include <string>

namespace nc {

// All stores live in %APPDATA%\NulConnect. Secrets (session material,
// device id, resource snapshot) are encrypted with DPAPI for the current
// Windows user. Methods throw AppError on I/O failure.

class ProfileStore {
public:
    Profile Load() const;
    void Save(const Profile& profile) const;
};

class SettingsStore {
public:
    AppSettings Load() const;
    void Save(const AppSettings& settings) const;
};

class SessionVault {
public:
    void Save(const atr::SessionMaterial& material) const;
    std::optional<atr::SessionMaterial> Load() const;
    std::optional<SessionSummary> LoadSummary() const;
    std::string LoadOrCreateDeviceId() const;
    void Clear() const;
};

class ResourceStore {
public:
    void Save(const atr::ResourceSnapshot& snapshot) const;
    std::optional<atr::ResourceSnapshot> Load() const;
    void Delete() const;
};

}  // namespace nc
