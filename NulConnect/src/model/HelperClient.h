#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace nc {

// Talks to the NulConnect Helper Windows service over its Named Pipe and
// installs/removes it through an elevated helper process. All methods block.
class HelperClient {
public:
    static bool IsInstalled();
    static bool IsRunning();

    static std::optional<std::string> InstalledVersion();
    static std::optional<std::string> BundledVersion();
    static bool RequiresInstallOrUpgrade();

    // Shows the UAC prompt. Throws AppError on failure or cancellation.
    static void Install();
    static void Uninstall();

    static nlohmann::json Status();
    static nlohmann::json StartTun(const nlohmann::json& config);
    static void StopTun();
    // Undoes whatever a previous client left behind (TUN adapter, routes, DNS).
    static void Cleanup();
    // Older helpers do not know the command; callers ignore the failure.
    static void SetLogging(bool enabled);

    // Compares dotted versions ("0.2.10" > "0.2.9").
    static int CompareVersions(const std::string& left, const std::string& right);

private:
    static nlohmann::json Send(const std::string& command, const nlohmann::json& extra, DWORD timeoutMs);
    static void RunElevated(const std::wstring& verb);
};

}  // namespace nc
