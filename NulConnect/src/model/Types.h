#pragma once

#include "atr/Atr.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace nc {

enum class RouteMode { Proxy, Tun };

enum class ConnectionPhase { Disconnected, Connecting, Connected, Disconnecting, Failed };

struct ConnectionState {
    ConnectionPhase phase = ConnectionPhase::Disconnected;
    std::wstring message;
};

const std::wstring& RouteModeTitle(RouteMode mode);
const std::wstring& PhaseTitle(ConnectionPhase phase);

// Connection profile. Field names match the macOS app's profile.json so the
// file format is shared.
struct Profile {
    std::string serverHost;
    uint16_t serverPort = 443;
    uint16_t localProxyPort = 1920;
    std::string loginDomain;
    std::optional<std::string> preferredAuthType;
    std::string userAgent =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) aTrustTray/2.4.10.50 "
        "Chrome/83.0.4103.94 Electron/9.0.2 Safari/537.36 aTrustTray-Linux-Plat-Ubuntu-x64 SPCClientType";
    bool allowInsecureTls = false;
    RouteMode routeMode = RouteMode::Proxy;
    bool useSystemProxy = false;
    uint64_t connectTimeoutMillis = 15000;
    uint64_t ioTimeoutMillis = 10000;
    uint64_t nodeProbeTimeoutMillis = 5000;
    std::string clientType = "SDPClient";
    std::string platform = "Linux";

    bool operator==(const Profile&) const = default;
};

void to_json(nlohmann::json& j, const Profile& value);
void from_json(const nlohmann::json& j, Profile& value);

// Windows-only application preferences.
struct AppSettings {
    bool launchAtStartup = false;
    bool closeToTray = true;
    bool showNotifications = true;
    bool trayHintShown = false;
    bool reconnectOnLaunch = false;

    bool operator==(const AppSettings&) const = default;
};

void to_json(nlohmann::json& j, const AppSettings& value);
void from_json(const nlohmann::json& j, AppSettings& value);

struct SessionSummary {
    std::string username;
    std::string deviceId;
    int cookieCount = 0;
    int64_t savedAt = 0;  // Unix seconds

    static SessionSummary From(const atr::SessionMaterial& material);
};

void to_json(nlohmann::json& j, const SessionSummary& value);
void from_json(const nlohmann::json& j, SessionSummary& value);

struct ProxyEndpoint {
    std::string host;
    uint16_t port = 0;
    std::wstring Display() const;
};

struct TrafficCounters {
    uint64_t uploadedBytes = 0;
    uint64_t downloadedBytes = 0;
    uint64_t uploadedPackets = 0;
    uint64_t downloadedPackets = 0;
};

struct TrafficStatistics {
    TrafficCounters counters;
    double uploadBytesPerSecond = 0;
    double downloadBytesPerSecond = 0;
    std::optional<double> connectionStartedAt;  // monotonic seconds
    double connectionDuration = 0;
    bool isLive = false;
};

struct ProxyRuntimeState {
    enum class Kind { Stopped, Starting, Running, Stopping, Failed } kind = Kind::Stopped;
    ProxyEndpoint endpoint;
    std::wstring message;
};

struct SystemProxyState {
    enum class Kind { Disabled, Enabling, Enabled, Disabling, Failed } kind = Kind::Disabled;
    std::wstring message;
};

struct TunnelRuntimeState {
    enum class Kind { Stopped, Starting, Running, Stopping, Reconnecting, Failed } kind = Kind::Stopped;
    int attempt = 0;
    std::wstring message;
};

struct LoginState {
    enum class Kind { Idle, LoadingMethods, Ready, Presenting, Finalizing, Failed, Succeeded } kind = Kind::Idle;
    int methodCount = 0;
    std::wstring detail;

    std::wstring Title() const;
};

struct HelperActivityState {
    enum class Kind { Idle, Checking, Installing, WaitingForStart, Succeeded, Failed } kind = Kind::Idle;
    std::wstring message;

    bool IsBusy() const {
        return kind == Kind::Checking || kind == Kind::Installing || kind == Kind::WaitingForStart;
    }
};

// Decides which navigation in the sign-in browser is the SSO callback, and
// normalizes it before it is handed to libreatrust.
class CapturePolicy {
public:
    enum class Kind { Cas, HttpsOauth2 };

    static std::optional<CapturePolicy> Make(const std::string& authType, const std::string& baseHost,
                                             const std::string& loginUrl, const std::string& additionalHost = {});

    bool ShouldCapture(const std::wstring& url) const;
    // Throws AppError(LoginFailed) when the URL is not an acceptable callback.
    std::string Validate(const std::wstring& url) const;
    std::wstring Hint() const;
    Kind kind() const { return kind_; }

private:
    Kind kind_ = Kind::Cas;
    std::string baseHost_;
    std::set<std::string> allowedHosts_;
};

struct WebLoginSession {
    uint64_t id = 0;
    atr::AuthMethod method;
    std::string deviceId;
    std::wstring title;
    std::wstring subtitle;
    std::wstring startUrl;
    CapturePolicy policy;
};

// Parsed URL parts (via WinHTTP).
struct UrlParts {
    std::wstring scheme;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;
    std::wstring query;  // without '?'
    std::wstring extra;  // query + fragment as returned by WinHTTP
};

std::optional<UrlParts> ParseUrl(const std::wstring& url);
std::optional<std::wstring> QueryValue(const std::wstring& query, std::wstring_view name);

}  // namespace nc
