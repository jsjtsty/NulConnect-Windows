#include "pch.h"
#include "model/SystemProxy.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

namespace nc {

namespace {

struct ProxySettings {
    DWORD flags = PROXY_TYPE_DIRECT;
    std::wstring server;
    std::wstring bypass;
    std::wstring autoConfigUrl;
};

std::wstring BackupPath() {
    return JoinPath(AppDataDirectory(), L"system-proxy-backup.json");
}

ProxySettings Query() {
    INTERNET_PER_CONN_OPTIONW options[4] = {};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    INTERNET_PER_CONN_OPTION_LISTW list{sizeof(list)};
    list.pszConnection = nullptr;
    list.dwOptionCount = 4;
    list.pOptions = options;
    DWORD size = sizeof(list);
    ProxySettings settings;
    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size)) {
        throw AppError(ErrorKind::Internal, TrFormat(L"Could not enable system proxy: %1$@", {L"InternetQueryOption"}));
    }
    settings.flags = options[0].Value.dwValue;
    auto take = [](LPWSTR value) {
        std::wstring text = value ? value : L"";
        if (value) GlobalFree(value);
        return text;
    };
    settings.server = take(options[1].Value.pszValue);
    settings.bypass = take(options[2].Value.pszValue);
    settings.autoConfigUrl = take(options[3].Value.pszValue);
    return settings;
}

void Apply(const ProxySettings& settings) {
    INTERNET_PER_CONN_OPTIONW options[4] = {};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[0].Value.dwValue = settings.flags;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[1].Value.pszValue = const_cast<LPWSTR>(settings.server.c_str());
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[2].Value.pszValue = const_cast<LPWSTR>(settings.bypass.c_str());
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    options[3].Value.pszValue = const_cast<LPWSTR>(settings.autoConfigUrl.c_str());
    INTERNET_PER_CONN_OPTION_LISTW list{sizeof(list)};
    list.pszConnection = nullptr;
    list.dwOptionCount = 4;
    list.pOptions = options;
    if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list))) {
        throw AppError(ErrorKind::Internal, std::wstring(L"InternetSetOption failed: ") + std::to_wstring(GetLastError()));
    }
    InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
    InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

nlohmann::json ToJson(const ProxySettings& settings) {
    return {{"flags", settings.flags},
            {"server", Narrow(settings.server)},
            {"bypass", Narrow(settings.bypass)},
            {"autoConfigUrl", Narrow(settings.autoConfigUrl)}};
}

ProxySettings FromJson(const nlohmann::json& json) {
    ProxySettings settings;
    settings.flags = json.value("flags", static_cast<DWORD>(PROXY_TYPE_DIRECT));
    settings.server = Widen(json.value("server", ""));
    settings.bypass = Widen(json.value("bypass", ""));
    settings.autoConfigUrl = Widen(json.value("autoConfigUrl", ""));
    return settings;
}

}  // namespace

void SystemProxy::Enable(const ProxyEndpoint& endpoint, const std::string& serverHost, SystemProxyMode mode) {
    // Keep the first backup: when re-enabling, the current settings are ours.
    ProxySettings previous;
    std::string data;
    if (ReadFileBytes(BackupPath(), data)) {
        auto json = nlohmann::json::parse(data, nullptr, false);
        if (json.is_object()) previous = FromJson(json);
    } else {
        previous = Query();
        if (!WriteFileAtomic(BackupPath(), ToJson(previous).dump(2))) {
            throw AppError(ErrorKind::Internal, TrFormat(L"Could not enable system proxy: %1$@", {BackupPath()}));
        }
    }
    ProxySettings settings;
    if (mode == SystemProxyMode::Pac && !endpoint.pacUrl.empty()) {
        // With a PAC script only managed resources reach the local proxy;
        // everything else stays direct, and the script's DIRECT fallback keeps
        // the machine online if the proxy goes away.
        settings.flags = PROXY_TYPE_DIRECT | PROXY_TYPE_AUTO_PROXY_URL;
        settings.autoConfigUrl = Widen(endpoint.pacUrl);
        Apply(settings);
        Log("[SystemProxy] enabled PAC " + endpoint.host + ":" + std::to_string(endpoint.port));
        return;
    }
    settings.flags = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;
    settings.server = Widen(endpoint.host) + L":" + std::to_wstring(endpoint.port);
    // As on macOS: the portal host plus the user's existing exceptions stay
    // direct. Private ranges are not bypassed; managed resources usually
    // live there and must reach the proxy.
    std::wstring bypass = L"localhost;127.0.0.1;<local>";
    if (!previous.bypass.empty()) bypass = previous.bypass + L";" + bypass;
    std::string host = Trim(serverHost);
    if (!host.empty()) bypass = Widen(host) + L";" + bypass;
    settings.bypass = bypass;
    Apply(settings);
    Log("[SystemProxy] enabled " + Narrow(settings.server));
}

void SystemProxy::Restore() {
    std::string data;
    if (!ReadFileBytes(BackupPath(), data)) {
        // No backup: fall back to a direct connection only if the current
        // setting still points at us.
        ProxySettings current = Query();
        bool ours = ((current.flags & PROXY_TYPE_PROXY) && current.server.rfind(L"127.0.0.1:", 0) == 0) ||
                    ((current.flags & PROXY_TYPE_AUTO_PROXY_URL) && current.autoConfigUrl.rfind(L"http://127.0.0.1:", 0) == 0 &&
                     current.autoConfigUrl.find(L"/proxy.pac?token=") != std::wstring::npos);
        if (ours) {
            ProxySettings direct;
            direct.flags = PROXY_TYPE_DIRECT | (current.flags & PROXY_TYPE_AUTO_DETECT);
            Apply(direct);
        }
        return;
    }
    auto json = nlohmann::json::parse(data, nullptr, false);
    ProxySettings previous = json.is_object() ? FromJson(json) : ProxySettings{};
    Apply(previous);
    DeleteFileIfExists(BackupPath());
    Log("[SystemProxy] restored previous settings");
}

bool SystemProxy::RestoreIfNeeded() {
    if (!FileExists(BackupPath())) return false;
    try {
        Restore();
        return true;
    } catch (const std::exception& error) {
        Log(std::string("[SystemProxy] restore failed: ") + error.what());
        return false;
    }
}

}  // namespace nc
