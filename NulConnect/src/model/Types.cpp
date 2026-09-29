#include "pch.h"
#include "model/Types.h"
#include "core/Localization.h"
#include "core/Platform.h"
#include "core/Str.h"

#include <winhttp.h>

namespace nc {

const std::wstring& RouteModeTitle(RouteMode mode) {
    return mode == RouteMode::Tun ? Tr(L"VPN Mode") : Tr(L"Proxy Mode");
}

const std::wstring& PhaseTitle(ConnectionPhase phase) {
    switch (phase) {
    case ConnectionPhase::Connecting: return Tr(L"Connecting");
    case ConnectionPhase::Connected: return Tr(L"Connected");
    case ConnectionPhase::Disconnecting: return Tr(L"Disconnecting");
    case ConnectionPhase::Failed: return Tr(L"Failed");
    default: return Tr(L"Disconnected");
    }
}

std::optional<PortalAddress> PortalAddress::Parse(const std::string& input) {
    std::string text = Trim(input);
    if (text.empty() || text.find_first_of(" \t\r\n") != std::string::npos) return std::nullopt;
    bool hasScheme = false;
    std::string scheme = "https";
    if (size_t at = text.find("://"); at != std::string::npos) {
        hasScheme = true;
        scheme = ToLower(text.substr(0, at));
        if (scheme != "http" && scheme != "https") return std::nullopt;
        text = text.substr(at + 3);
    }
    std::string authority = text.substr(0, text.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string::npos) return std::nullopt;

    std::string host = authority;
    std::string portText;
    if (authority.front() == '[') {
        size_t close = authority.find(']');
        if (close == std::string::npos) return std::nullopt;
        host = authority.substr(0, close + 1);
        std::string rest = authority.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return std::nullopt;
            portText = rest.substr(1);
        }
    } else if (size_t colon = authority.find(':'); colon != std::string::npos) {
        host = authority.substr(0, colon);
        portText = authority.substr(colon + 1);
    }
    if (host.empty()) return std::nullopt;

    PortalAddress address;
    address.host = ToLower(host);
    if (!portText.empty()) {
        if (portText.size() > 5 || portText.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
        unsigned long port = std::stoul(portText);
        if (port < 1 || port > 65535) return std::nullopt;
        address.port = static_cast<uint16_t>(port);
    } else if (authority.back() == ':') {
        return std::nullopt;
    } else if (hasScheme) {
        // A pasted link means the scheme's default port; a bare host keeps
        // the configured one.
        address.port = static_cast<uint16_t>(scheme == "http" ? 80 : 443);
    }
    return address;
}

std::string MakePacToken() {
    return RandomHex(16);
}

void to_json(nlohmann::json& j, const Profile& v) {
    j = {{"serverHost", v.serverHost},
         {"serverPort", v.serverPort},
         {"localProxyPort", v.localProxyPort},
         {"loginDomain", v.loginDomain},
         {"userAgent", v.userAgent},
         {"allowInsecureTLS", v.allowInsecureTls},
         {"routeMode", v.routeMode == RouteMode::Tun ? "tun" : "proxy"},
         {"useSystemProxy", v.useSystemProxy},
         {"systemProxyMode", v.systemProxyMode == SystemProxyMode::Pac ? "pac" : "all"},
         {"pacToken", v.pacToken},
         {"connectTimeoutMillis", v.connectTimeoutMillis},
         {"ioTimeoutMillis", v.ioTimeoutMillis},
         {"nodeProbeTimeoutMillis", v.nodeProbeTimeoutMillis},
         {"clientType", v.clientType},
         {"platform", v.platform}};
    if (v.preferredAuthType) j["preferredAuthType"] = *v.preferredAuthType;
}

void from_json(const nlohmann::json& j, Profile& v) {
    Profile defaults;
    v.serverHost = j.value("serverHost", defaults.serverHost);
    v.serverPort = j.value("serverPort", defaults.serverPort);
    v.localProxyPort = j.value("localProxyPort", defaults.localProxyPort);
    v.loginDomain = j.value("loginDomain", defaults.loginDomain);
    if (j.contains("preferredAuthType") && j["preferredAuthType"].is_string()) {
        v.preferredAuthType = j["preferredAuthType"].get<std::string>();
    } else {
        v.preferredAuthType.reset();
    }
    v.userAgent = j.value("userAgent", defaults.userAgent);
    v.allowInsecureTls = j.value("allowInsecureTLS", defaults.allowInsecureTls);
    v.routeMode = j.value("routeMode", std::string("proxy")) == "tun" ? RouteMode::Tun : RouteMode::Proxy;
    v.useSystemProxy = j.value("useSystemProxy", defaults.useSystemProxy);
    v.systemProxyMode = j.value("systemProxyMode", std::string("all")) == "pac" ? SystemProxyMode::Pac : SystemProxyMode::All;
    v.pacToken = j.value("pacToken", std::string());
    bool tokenValid = !v.pacToken.empty() && std::all_of(v.pacToken.begin(), v.pacToken.end(),
                                                         [](unsigned char c) { return std::isalnum(c) != 0; });
    if (!tokenValid) v.pacToken = MakePacToken();
    v.connectTimeoutMillis = j.value("connectTimeoutMillis", defaults.connectTimeoutMillis);
    v.ioTimeoutMillis = j.value("ioTimeoutMillis", defaults.ioTimeoutMillis);
    v.nodeProbeTimeoutMillis = j.value("nodeProbeTimeoutMillis", defaults.nodeProbeTimeoutMillis);
    v.clientType = j.value("clientType", defaults.clientType);
    v.platform = j.value("platform", defaults.platform);
}

void to_json(nlohmann::json& j, const AppSettings& v) {
    j = {{"launchAtStartup", v.launchAtStartup},
         {"closeToTray", v.closeToTray},
         {"showNotifications", v.showNotifications},
         {"trayHintShown", v.trayHintShown},
         {"reconnectOnLaunch", v.reconnectOnLaunch},
         {"webLoginCompleted", v.webLoginCompleted},
         {"verboseLogging", v.verboseLogging}};
}

void from_json(const nlohmann::json& j, AppSettings& v) {
    AppSettings d;
    v.launchAtStartup = j.value("launchAtStartup", d.launchAtStartup);
    v.closeToTray = j.value("closeToTray", d.closeToTray);
    v.showNotifications = j.value("showNotifications", d.showNotifications);
    v.trayHintShown = j.value("trayHintShown", d.trayHintShown);
    v.reconnectOnLaunch = j.value("reconnectOnLaunch", d.reconnectOnLaunch);
    v.webLoginCompleted = j.value("webLoginCompleted", d.webLoginCompleted);
    v.verboseLogging = j.value("verboseLogging", d.verboseLogging);
}

SessionSummary SessionSummary::From(const atr::SessionMaterial& material) {
    SessionSummary summary;
    summary.username = material.username;
    summary.deviceId = material.deviceId;
    summary.cookieCount = static_cast<int>(material.cookies.size());
    summary.savedAt = static_cast<int64_t>(time(nullptr));
    return summary;
}

void to_json(nlohmann::json& j, const SessionSummary& v) {
    j = {{"username", v.username}, {"deviceID", v.deviceId}, {"cookieCount", v.cookieCount}, {"savedAt", v.savedAt}};
}

void from_json(const nlohmann::json& j, SessionSummary& v) {
    v.username = j.value("username", "");
    v.deviceId = j.value("deviceID", "");
    v.cookieCount = j.value("cookieCount", 0);
    v.savedAt = j["savedAt"].is_number_integer() ? j["savedAt"].get<int64_t>() : 0;
}

std::wstring ProxyEndpoint::Display() const {
    return Widen(host) + L":" + std::to_wstring(port);
}

std::wstring LoginState::Title() const {
    switch (kind) {
    case Kind::Idle: return Tr(L"Not started");
    case Kind::LoadingMethods: return Tr(L"Loading sign-in methods");
    case Kind::Ready: return TrFormat(L"Loaded %1$@ sign-in methods", {std::to_wstring(methodCount)});
    case Kind::Presenting: return Tr(L"Opening") + L" " + detail;
    case Kind::Finalizing: return Tr(L"Completing sign-in");
    case Kind::Failed: return Tr(L"Sign-in failed");
    case Kind::Succeeded: return Tr(L"Signed in");
    }
    return {};
}

// ---- URL helpers ---------------------------------------------------------------

std::optional<UrlParts> ParseUrl(const std::wstring& url) {
    URL_COMPONENTS components{sizeof(components)};
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return std::nullopt;
    }
    UrlParts parts;
    parts.scheme = ToLower(std::wstring(components.lpszScheme, components.dwSchemeLength));
    parts.host = std::wstring(components.lpszHostName, components.dwHostNameLength);
    parts.port = components.nPort;
    parts.path = std::wstring(components.lpszUrlPath, components.dwUrlPathLength);
    parts.extra = std::wstring(components.lpszExtraInfo, components.dwExtraInfoLength);
    std::wstring query = parts.extra;
    size_t hash = query.find(L'#');
    if (hash != std::wstring::npos) query.resize(hash);
    if (!query.empty() && query.front() == L'?') query.erase(0, 1);
    parts.query = query;
    return parts;
}

std::optional<std::wstring> QueryValue(const std::wstring& query, std::wstring_view name) {
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find(L'&', start);
        if (end == std::wstring::npos) end = query.size();
        std::wstring_view item(query.data() + start, end - start);
        size_t equals = item.find(L'=');
        std::wstring_view key = item.substr(0, equals);
        if (key == name) {
            return equals == std::wstring_view::npos ? std::wstring() : std::wstring(item.substr(equals + 1));
        }
        start = end + 1;
    }
    return std::nullopt;
}

// ---- CapturePolicy ---------------------------------------------------------------

namespace {

std::string NormalizedHost(std::string value) {
    return ToLower(TrimChars(value, ". \r\n\t"));
}

std::string HostOf(const std::string& url) {
    auto parts = ParseUrl(Widen(url));
    return parts ? Narrow(parts->host) : std::string();
}

bool HasNonEmpty(const std::wstring& query, std::wstring_view name) {
    auto value = QueryValue(query, name);
    return value && !value->empty();
}

}  // namespace

std::optional<CapturePolicy> CapturePolicy::Make(const std::string& authType, const std::string& baseHost,
                                                 const std::string& loginUrl, const std::string& additionalHost) {
    std::string base = NormalizedHost(baseHost);
    if (base.empty()) return std::nullopt;
    CapturePolicy policy;
    policy.baseHost_ = base;
    policy.allowedHosts_.insert(base);
    std::string loginHost = HostOf(loginUrl);
    if (!loginHost.empty()) policy.allowedHosts_.insert(NormalizedHost(loginHost));
    if (!additionalHost.empty()) policy.allowedHosts_.insert(NormalizedHost(additionalHost));
    if (authType == "auth/cas") {
        policy.kind_ = Kind::Cas;
    } else if (authType == "auth/httpsOauth2") {
        policy.kind_ = Kind::HttpsOauth2;
    } else {
        return std::nullopt;
    }
    return policy;
}

std::wstring CapturePolicy::Hint() const {
    return kind_ == Kind::Cas ? Tr(L"Capture CAS callback containing a ticket") : Tr(L"Capture OAuth2 callback containing a code");
}

bool CapturePolicy::ShouldCapture(const std::wstring& url) const {
    auto parts = ParseUrl(url);
    if (!parts) return false;
    return HasNonEmpty(parts->query, kind_ == Kind::Cas ? L"ticket" : L"code");
}

std::string CapturePolicy::Validate(const std::wstring& url) const {
    auto fail = [](const wchar_t* key) { throw AppError(ErrorKind::LoginFailed, Tr(key)); };
    auto parts = ParseUrl(url);
    if (!parts) fail(L"Could not parse callback URL");
    std::string host = NormalizedHost(Narrow(parts->host));
    if (kind_ == Kind::Cas) {
        if (host.empty()) fail(L"CAS callback is missing a host name");
        if (!allowedHosts_.count(host)) fail(L"CAS callback host does not match");
        if (parts->path.find(L"cas") == std::wstring::npos) fail(L"CAS callback path does not match");
        if (!HasNonEmpty(parts->query, L"ticket")) fail(L"CAS callback is missing a ticket");
        if (parts->scheme != L"https" && parts->scheme != L"http") fail(L"CAS callback uses an invalid scheme");
        // Always complete over HTTPS against the portal host, keeping an
        // explicit non-default port.
        bool defaultPort = (parts->scheme == L"https" && parts->port == 443) || (parts->scheme == L"http" && parts->port == 80);
        std::string port = defaultPort ? std::string() : ":" + std::to_string(parts->port);
        return "https://" + baseHost_ + port + Narrow(parts->path) + Narrow(parts->extra);
    }
    if (parts->scheme != L"https") fail(L"OAuth2 callback must use HTTPS");
    if (!allowedHosts_.count(host)) fail(L"OAuth2 callback host does not match");
    if (parts->path != L"/passport/v1/auth/httpsOauth2") fail(L"OAuth2 callback path does not match");
    if (!HasNonEmpty(parts->query, L"code")) fail(L"OAuth2 callback is missing a code");
    return Narrow(url);
}

}  // namespace nc
