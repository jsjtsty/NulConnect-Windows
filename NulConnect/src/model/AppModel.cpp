#include "pch.h"
#include "model/AppModel.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "model/HelperClient.h"
#include "model/SystemProxy.h"

namespace nc {

namespace {

// Sleeping or losing the network for at least this long drops NAT mappings
// and server-side L3 state, so the tunnel is rebuilt afterwards.
constexpr double kTunnelWakeReconnectThreshold = 30;
constexpr double kTunnelOutageReconnectThreshold = 30;
constexpr size_t kTrafficHistoryLength = 60;

double MonotonicSeconds() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

unsigned TunnelReconnectDelayMs(int attempt) {
    static const unsigned seconds[] = {1, 2, 5, 10, 20, 30};
    if (attempt >= 1 && attempt <= 6) return seconds[attempt - 1] * 1000;
    return 60000;
}

bool ContainsText(const std::wstring& haystack, const wchar_t* needle) {
    return ToLower(haystack).find(needle) != std::wstring::npos;
}

}  // namespace

AppModel::AppModel() = default;

AppModel::~AppModel() {
    networkMonitor_.Stop();
    authQueue_.Drain();
    networkQueue_.Drain();
    statusQueue_.Drain();
}

void AppModel::Initialize() {
    profile_ = profileStore_.Load();
    settings_ = settingsStore_.Load();
    SetDiagnosticLogging(settings_.verboseLogging);
    // Persist the PAC token generated for a profile that had none, so the
    // PAC URL of the next run stays the same as long as the profile does.
    try {
        profileStore_.Save(profile_);
    } catch (...) {
    }
    storedSession_ = sessionVault_.Load();
    sessionSummary_ = sessionVault_.LoadSummary();
    if (!sessionSummary_ && storedSession_) sessionSummary_ = SessionSummary::From(*storedSession_);
    if (!storedSession_) sessionSummary_.reset();
    resourceSnapshot_ = resourceStore_.Load();
    helperVersionText_ = Tr(L"Loading");
    bundledHelperVersionText_ = Tr(L"Loading");

    // A previous run may have exited with the system proxy still pointing at
    // a proxy that no longer exists.
    if (SystemProxy::RestoreIfNeeded()) {
        Log("[Model] restored system proxy left by a previous session");
    }
    helperInstalled_ = HelperClient::IsInstalled();
    helperRunning_ = HelperClient::IsRunning();
    networkMonitor_.Start([this] { HandleNetworkChanged(); });
    RefreshHelperState(true);
    // The helper service can be removed or stopped behind our back.
    helperPollTimer_ = Dispatcher::SetInterval(10000, [this] { RefreshHelperState(false); });
    ApplyLaunchAtStartup();
    PerformLaunchTasks();
}

// A previous run of this app may have crashed while the helper service still
// held the TUN adapter, routes and DNS rules. Nothing of this instance is
// running yet (the application is single-instance), so whatever the helper
// holds is a leftover: clean it up, then honor "connect on launch".
void AppModel::PerformLaunchTasks() {
    SyncHelperLogging();
    statusQueue_.Enqueue([this] {
        bool recovered = false;
        try {
            if (HelperClient::IsRunning()) {
                nlohmann::json status = HelperClient::Status();
                std::string tun = status.contains("tun") && status["tun"].is_object() ? status["tun"].value("status", "") : "";
                if (tun == "starting" || tun == "running" || tun == "stopping") {
                    Log("[Launch] recovering stale VPN state: " + tun);
                    HelperClient::Cleanup();
                    recovered = true;
                }
            }
        } catch (const std::exception& error) {
            Log(std::string("[Launch] recovery failed: ") + error.what());
        }
        Dispatcher::Post([this, recovered] {
            if (recovered) SetBanner(BannerSeverity::Info, Tr(L"Restored network settings left by the previous session"));
            if (!settings_.reconnectOnLaunch || !storedSession_) return;
            bool idle = connectionState_.phase == ConnectionPhase::Disconnected || connectionState_.phase == ConnectionPhase::Failed;
            if (!idle || IsProxyRunning() || IsProxyBusy() || IsTunnelRunning() || IsTunnelBusy()) return;
            Log("[Launch] connect on launch");
            PerformPrimaryAction();
            // Not a user action: a sign-in that follows may stay in the background.
            lastUserActionAt_.reset();
        });
    });
}

// ---- Observation ---------------------------------------------------------------

int AppModel::Subscribe(Observer observer) {
    int token = nextObserver_++;
    observers_[token] = std::move(observer);
    return token;
}

void AppModel::Unsubscribe(int token) {
    observers_.erase(token);
}

void AppModel::Changed() {
    if (notifyPending_) return;
    notifyPending_ = true;
    Dispatcher::Post([this] {
        notifyPending_ = false;
        auto snapshot = observers_;
        for (auto& [token, observer] : snapshot) {
            if (observers_.count(token) && observer) observer();
        }
    });
}

void AppModel::SetBanner(BannerSeverity severity, std::wstring text) {
    banner_ = Banner{nextBannerId_++, severity, std::move(text)};
    Log(L"[Banner] " + banner_.text);
    Changed();
}

void AppModel::DismissBanner() {
    banner_ = Banner{nextBannerId_++, BannerSeverity::Info, {}};
    Changed();
}

void AppModel::SetConnection(ConnectionPhase phase, std::wstring message) {
    connectionState_ = ConnectionState{phase, std::move(message)};
    Changed();
}

void AppModel::Notify(const std::wstring& title, const std::wstring& message) {
    if (settings_.showNotifications && onNotify) onNotify(title, message);
}

// ---- Derived state -------------------------------------------------------------

Profile AppModel::RuntimeProfile() const {
    Profile profile = profile_;
    if (!helperInstalled_) profile.routeMode = RouteMode::Proxy;
    return profile;
}

RouteMode AppModel::EffectiveRouteMode() const {
    return helperInstalled_ ? profile_.routeMode : RouteMode::Proxy;
}

std::wstring AppModel::RoutePresentationModeTitle() const {
    if (EffectiveRouteMode() == RouteMode::Tun) return RouteModeTitle(RouteMode::Tun);
    bool systemProxy = IsSystemProxyEnabled() || profile_.useSystemProxy;
    return systemProxy ? Tr(L"System Proxy") : RouteModeTitle(RouteMode::Proxy);
}

bool AppModel::IsProxyBusy() const {
    return proxyState_.kind == ProxyRuntimeState::Kind::Starting || proxyState_.kind == ProxyRuntimeState::Kind::Stopping;
}

bool AppModel::IsTunnelRunning() const {
    // While reconnecting the connection is still "on" for the user, and
    // Disconnect must cancel it.
    return tunnelState_.kind == TunnelRuntimeState::Kind::Running || tunnelState_.kind == TunnelRuntimeState::Kind::Reconnecting;
}

bool AppModel::IsTunnelBusy() const {
    return tunnelState_.kind == TunnelRuntimeState::Kind::Starting || tunnelState_.kind == TunnelRuntimeState::Kind::Stopping;
}

bool AppModel::IsSystemProxyBusy() const {
    return systemProxyState_.kind == SystemProxyState::Kind::Enabling || systemProxyState_.kind == SystemProxyState::Kind::Disabling;
}

bool AppModel::IsVpnConnectedOrConnecting() const {
    auto phase = connectionState_.phase;
    return phase == ConnectionPhase::Connecting || phase == ConnectionPhase::Connected || phase == ConnectionPhase::Disconnecting;
}

bool AppModel::IsPrimaryActionBusy() const {
    if (EffectiveRouteMode() == RouteMode::Proxy) {
        return IsProxyBusy() || (!IsProxyRunning() && !IsLocalProxyPortValid()) || isLoggingOut_;
    }
    return IsTunnelBusy() || IsHelperActivityBusy() || isLoggingOut_;
}

bool AppModel::CanChangeSystemProxyPreference() const {
    return EffectiveRouteMode() == RouteMode::Proxy && !IsProxyBusy() && !IsTunnelRunning() && !IsTunnelBusy() &&
           !IsSystemProxyBusy();
}

bool AppModel::CanChangeRouteMode() const {
    return helperInstalled_ && !IsProxyRunning() && !IsTunnelRunning() && !IsProxyBusy() && !IsTunnelBusy();
}

bool AppModel::IsLoginConfigurationReady() const {
    std::string host = ToLower(Trim(profile_.serverHost));
    return !host.empty() && host != "localhost" && host != "127.0.0.1";
}

std::wstring AppModel::ProxyEndpointText() const {
    if (IsProxyRunning()) return proxyState_.endpoint.Display();
    if (!IsLocalProxyPortValid()) return Tr(L"Local proxy port is invalid");
    return L"127.0.0.1:" + std::to_wstring(profile_.localProxyPort);
}

std::wstring AppModel::TerminalProxyCommand() const {
    if (!IsProxyRunning() && !IsLocalProxyPortValid()) return {};
    std::wstring endpoint = IsProxyRunning() ? proxyState_.endpoint.Display() : ProxyEndpointText();
    // PowerShell syntax; socks5h makes curl & co. resolve names through the
    // proxy, which intranet names need.
    return L"$env:http_proxy='http://" + endpoint + L"'; $env:https_proxy='http://" + endpoint +
           L"'; $env:all_proxy='socks5h://" + endpoint + L"'";
}

std::wstring AppModel::SshProxyCommand() const {
    if (!IsProxyRunning() && !IsLocalProxyPortValid()) return {};
    std::wstring endpoint = IsProxyRunning() ? proxyState_.endpoint.Display() : ProxyEndpointText();
    // connect.exe ships with Git for Windows; the SOCKS5 proxy resolves the host name.
    return L"-o \"ProxyCommand=connect -S " + endpoint + L" %h %p\"";
}

std::wstring AppModel::ServerDisplayText() const {
    std::string host = Trim(profile_.serverHost);
    if (host.empty()) return Tr(L"Server not configured");
    return Widen(host) + L":" + std::to_wstring(profile_.serverPort);
}

std::wstring AppModel::PrimaryActionTitle() const {
    bool running = EffectiveRouteMode() == RouteMode::Tun ? IsTunnelRunning() : IsProxyRunning();
    if (running) return Tr(L"Disconnect");
    return NeedsLogin() ? Tr(L"Log In & Connect") : Tr(L"Connect");
}

atr::ClientConfig AppModel::ClientConfiguration() const {
    Profile p = RuntimeProfile();
    atr::ClientConfig config;
    config.serverHost = p.serverHost;
    config.serverPort = p.serverPort;
    config.userAgent = p.userAgent;
    config.connectTimeoutMs = p.connectTimeoutMillis;
    config.ioTimeoutMs = p.ioTimeoutMillis;
    config.nodeProbeTimeoutMs = p.nodeProbeTimeoutMillis;
    config.allowInsecureTls = p.allowInsecureTls;
    return config;
}

atr::AuthConfig AppModel::AuthConfiguration() const {
    Profile p = RuntimeProfile();
    atr::AuthConfig config;
    config.serverHost = p.serverHost;
    config.serverPort = p.serverPort;
    config.userAgent = p.userAgent;
    config.clientType = p.clientType;
    config.platform = p.platform;
    config.loginDomain = p.loginDomain;
    config.preferredAuthType = p.preferredAuthType;
    config.ioTimeoutMs = p.ioTimeoutMillis;
    config.allowInsecureTls = p.allowInsecureTls;
    return config;
}

// ---- Profile & settings -----------------------------------------------------------

void AppModel::UpdateProfile(const std::function<void(Profile&)>& update) {
    Profile copy = profile_;
    update(copy);
    if (copy == profile_) return;
    profile_ = copy;
    ScheduleProfilePersistence();
    Changed();
}

void AppModel::ScheduleProfilePersistence() {
    Dispatcher::ClearTimer(profileSaveTimer_);
    profileSaveTimer_ = Dispatcher::SetTimeout(300, [this] {
        profileSaveTimer_ = 0;
        try {
            profileStore_.Save(profile_);
        } catch (...) {
            SetBanner(BannerSeverity::Error, CurrentError().message);
        }
    });
}

bool AppModel::ConfigurePortal(const std::wstring& input) {
    auto address = PortalAddress::Parse(Narrow(input));
    if (!address) {
        SetBanner(BannerSeverity::Warning,
                  Tr(L"Enter a host name such as vpn.example.edu or a portal link starting with https://"));
        return false;
    }
    UpdateProfile([&](Profile& p) {
        p.serverHost = address->host;
        if (address->port) p.serverPort = *address->port;
    });
    return true;
}

void AppModel::ResetProfileToDefaults() {
    profile_ = Profile{};
    try {
        profileStore_.Save(profile_);
        SetBanner(BannerSeverity::Success, Tr(L"Settings saved"));
    } catch (...) {
        SetBanner(BannerSeverity::Error, TrFormat(L"Could not save: %1$@", {CurrentError().message}));
    }
    Changed();
}

void AppModel::UpdateSettings(const std::function<void(AppSettings&)>& update) {
    AppSettings copy = settings_;
    update(copy);
    if (copy == settings_) return;
    bool startupChanged = copy.launchAtStartup != settings_.launchAtStartup;
    bool loggingChanged = copy.verboseLogging != settings_.verboseLogging;
    settings_ = copy;
    SaveSettings();
    if (startupChanged) ApplyLaunchAtStartup();
    if (loggingChanged) ApplyDiagnosticLogging();
    Changed();
}

void AppModel::ApplyDiagnosticLogging() {
    SetDiagnosticLogging(settings_.verboseLogging);
    SyncHelperLogging();
}

// Passes the preference to the privileged helper. Helpers that predate the
// command, or that are not running, simply keep logging off.
void AppModel::SyncHelperLogging() {
    bool enabled = settings_.verboseLogging;
    statusQueue_.Enqueue([enabled] {
        try {
            if (HelperClient::IsRunning()) HelperClient::SetLogging(enabled);
        } catch (const std::exception&) {
        }
    });
}

void AppModel::SaveSettings() {
    try {
        settingsStore_.Save(settings_);
    } catch (...) {
        SetBanner(BannerSeverity::Error, CurrentError().message);
    }
}

void AppModel::ApplyLaunchAtStartup() {
    constexpr const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (settings_.launchAtStartup) {
        std::wstring command = L"\"" + ExecutablePath() + L"\" --background";
        RegSetValueExW(key, L"NulConnect", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"NulConnect");
    }
    RegCloseKey(key);
}

// ---- Session persistence -------------------------------------------------------------

void AppModel::SaveSessionMaterial(const atr::SessionMaterial& material) {
    try {
        sessionVault_.Save(material);
        storedSession_ = material;
        sessionSummary_ = SessionSummary::From(material);
    } catch (...) {
        SetBanner(BannerSeverity::Error, TrFormat(L"Could not save session: %1$@", {CurrentError().message}));
    }
    Changed();
}

void AppModel::SaveResourceSnapshot(const atr::ResourceSnapshot& snapshot) {
    try {
        resourceStore_.Save(snapshot);
        resourceSnapshot_ = snapshot;
    } catch (...) {
        SetBanner(BannerSeverity::Error, TrFormat(L"Could not save resource snapshot: %1$@", {CurrentError().message}));
    }
    Changed();
}

void AppModel::InvalidateStoredSession(const std::wstring& message) {
    authQueue_.Enqueue([engine = authEngine_] { engine->Reset(); });
    try {
        sessionVault_.Clear();
    } catch (...) {
    }
    resourceStore_.Delete();
    storedSession_.reset();
    sessionSummary_.reset();
    resourceSnapshot_.reset();
    loginState_ = LoginState{LoginState::Kind::Failed, 0, message};
    SetConnection(ConnectionPhase::Failed, message);
    SetBanner(BannerSeverity::Error, message);
}

bool AppModel::IsStoredSessionInvalidError(const ErrorInfo& error) {
    if (ContainsText(error.message, L"stored session is not logged in") || ContainsText(error.message, L"not logged in") ||
        ContainsText(error.message, L"invalid sid")) {
        return true;
    }
    return false;
}

bool AppModel::RequiresWebLogin(const ErrorInfo& error) {
    return error.kind == ErrorKind::MissingSession || error.kind == ErrorKind::SessionExpired ||
           IsStoredSessionInvalidError(error);
}

void AppModel::RefreshSessionAndResource(CancelToken token, std::function<void(RefreshResult)> onSuccess,
                                         std::function<void(const ErrorInfo&)> onError) {
    if (!storedSession_) {
        onError(ErrorInfo{ErrorKind::MissingSession, L"proxy mode requires a saved session"});
        return;
    }
    atr::SessionMaterial stored = *storedSession_;
    atr::AuthConfig authConfig = AuthConfiguration();
    atr::ClientConfig clientConfig = ClientConfiguration();
    std::string serverHost = profile_.serverHost;
    Log("[Login] resume stored session user='" + stored.username + "'");
    RunAsync<RefreshResult>(
        authQueue_, token,
        [engine = authEngine_, stored, authConfig, clientConfig, serverHost]() {
            atr::SessionMaterial refreshed = engine->ResumeSession(stored, authConfig);
            std::string resourceBytes = engine->FetchClientResource();
            atr::Client client(clientConfig);
            client.SetSession(refreshed);
            client.SetResource(resourceBytes, serverHost);
            atr::ResourceSnapshot snapshot = client.Snapshot();
            Log("[Login] refreshed resource: ip=" + std::to_string(snapshot.ipResources.size()) +
                " domain=" + std::to_string(snapshot.domainResources.size()) +
                " dns=" + std::to_string(snapshot.dnsResources.size()));
            return RefreshResult{refreshed, snapshot};
        },
        [this, onSuccess](RefreshResult result) {
            SaveSessionMaterial(result.first);
            SaveResourceSnapshot(result.second);
            onSuccess(std::move(result));
        },
        [this, onError](const ErrorInfo& error) {
            if (IsStoredSessionInvalidError(error)) {
                std::wstring message = Tr(L"Sign-in session expired. Please sign in again.");
                InvalidateStoredSession(message);
                onError(ErrorInfo{ErrorKind::SessionExpired, message});
                return;
            }
            onError(error);
        });
}

// ---- Primary action ---------------------------------------------------------------

void AppModel::PerformPrimaryAction() {
    if (IsPrimaryActionBusy()) return;
    lastUserActionAt_ = MonotonicSeconds();
    if (EffectiveRouteMode() == RouteMode::Tun) {
        if (IsTunnelRunning()) StopTunnelMode();
        else StartTunnelMode();
    } else {
        if (IsProxyRunning()) StopProxyMode();
        else StartProxyMode();
    }
}

void AppModel::SetRouteMode(RouteMode mode) {
    if (!helperInstalled_) {
        UpdateProfile([](Profile& p) { p.routeMode = RouteMode::Proxy; });
        return;
    }
    if (IsVpnConnectedOrConnecting()) return;
    UpdateProfile([mode](Profile& p) {
        p.routeMode = mode;
        if (mode == RouteMode::Tun) p.useSystemProxy = false;
    });
}

// ---- Proxy mode --------------------------------------------------------------------

void AppModel::StartProxyMode() {
    if (EffectiveRouteMode() != RouteMode::Proxy) {
        SetBanner(BannerSeverity::Warning, Tr(L"Proxy mode is not selected"));
        return;
    }
    if (!IsLocalProxyPortValid()) {
        SetBanner(BannerSeverity::Warning, Tr(L"Set a valid local proxy port (1–65535) first"));
        return;
    }
    if (IsTunnelRunning() || IsTunnelBusy()) {
        SetBanner(BannerSeverity::Warning, Tr(L"Stop VPN mode first"));
        return;
    }
    if (!storedSession_) {
        RequestWebLogin(RouteMode::Proxy);
        return;
    }
    if (proxyService_) {
        SetBanner(BannerSeverity::Info, Tr(L"Proxy is already running"));
        return;
    }
    proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Starting, {}, {}};
    SetConnection(ConnectionPhase::Connecting, Tr(L"Starting local proxy"));
    LaunchProxy(false);
}

void AppModel::LaunchProxy(bool recovering) {
    CancelAndReset(proxyTask_);
    proxyTask_ = MakeCancelToken();
    CancelToken token = proxyTask_;
    Profile profile = RuntimeProfile();
    RefreshSessionAndResource(
        token,
        [this, token, profile, recovering](RefreshResult refreshed) {
            if (IsCancelled(token)) return;
            using Started = std::pair<std::shared_ptr<ProxyService>, ProxyEndpoint>;
            RunAsync<Started>(
                networkQueue_, token,
                [profile, refreshed, this]() {
                    auto service = std::make_shared<ProxyService>(profile, refreshed.first, refreshed.second,
                                                                  profile.localProxyPort);
                    service->onSessionInvalidated = [this](std::wstring message) { HandleProxySessionInvalidated(message); };
                    ProxyEndpoint endpoint = service->Start();
                    return Started{service, endpoint};
                },
                [this, recovering, profile](Started started) {
                    proxyService_ = started.first;
                    ProxyEndpoint endpoint = started.second;
                    proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Running, endpoint, {}};
                    BeginTrafficSession(recovering);
                    if (recovering) {
                        SetConnection(ConnectionPhase::Connected, TrFormat(L"Local proxy restored at %1$@", {endpoint.Display()}));
                        SetBanner(BannerSeverity::Success, Tr(L"Sign-in session restored"));
                    } else {
                        SetConnection(ConnectionPhase::Connected, TrFormat(L"Local proxy started at %1$@", {endpoint.Display()}));
                        SetBanner(BannerSeverity::Success, Tr(L"Proxy mode started"));
                        Notify(Tr(L"Connected"), TrFormat(L"Local proxy started at %1$@", {endpoint.Display()}));
                    }
                    if (profile.useSystemProxy) EnableSystemProxy(endpoint);
                },
                [this](const ErrorInfo& error) {
                    proxyService_.reset();
                    proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Failed, {}, error.message};
                    SetConnection(ConnectionPhase::Failed, error.message);
                    SetBanner(BannerSeverity::Error, TrFormat(L"Could not start proxy: %1$@", {error.message}));
                });
        },
        [this, token](const ErrorInfo& error) {
            if (IsCancelled(token)) return;
            proxyService_.reset();
            proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Failed, {}, error.message};
            SetConnection(ConnectionPhase::Failed, error.message);
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not start proxy: %1$@", {error.message}));
            if (RequiresWebLogin(error)) RequestWebLogin(RouteMode::Proxy);
        });
}

void AppModel::StopProxyMode() {
    if (!proxyService_) {
        CancelAndReset(proxyTask_);
        proxyState_ = ProxyRuntimeState{};
        SetConnection(ConnectionPhase::Disconnected, Tr(L"Proxy is not running"));
        return;
    }
    proxyState_.kind = ProxyRuntimeState::Kind::Stopping;
    SetConnection(ConnectionPhase::Disconnecting, Tr(L"Stopping local proxy"));
    CancelAndReset(proxyTask_);
    DisableSystemProxy([this] { FinishStoppingProxyMode(); });
}

void AppModel::FinishStoppingProxyMode() {
    SampleTraffic(true);
    auto service = std::move(proxyService_);
    proxyService_.reset();
    RunAsyncVoid(
        networkQueue_, nullptr, [service]() mutable { if (service) service->Stop(); },
        [this] {
            proxyState_ = ProxyRuntimeState{};
            SetConnection(ConnectionPhase::Disconnected, Tr(L"Proxy stopped"));
            SetBanner(BannerSeverity::Info, Tr(L"Proxy mode stopped"));
            FinishTrafficSession();
        },
        [this](const ErrorInfo&) {
            proxyState_ = ProxyRuntimeState{};
            SetConnection(ConnectionPhase::Disconnected, Tr(L"Proxy stopped"));
            FinishTrafficSession();
        });
}

void AppModel::HandleProxySessionInvalidated(const std::wstring& message) {
    Log(L"[Proxy] session invalidated: " + message);
    if (!proxyService_) return;
    SampleTraffic(true);
    auto service = std::move(proxyService_);
    proxyService_.reset();
    networkQueue_.Enqueue([service]() mutable { if (service) service->Stop(); });
    CancelAndReset(proxyTask_);
    if (!storedSession_) {
        FailProxySessionInvalidated(message);
        RequestWebLogin(RouteMode::Proxy);
        return;
    }
    proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Starting, {}, {}};
    SetConnection(ConnectionPhase::Connecting, Tr(L"Sign-in session expired. Attempting recovery."));
    SetBanner(BannerSeverity::Warning, Tr(L"Restoring sign-in session"));
    LaunchProxy(true);
}

void AppModel::FailProxySessionInvalidated(const std::wstring&) {
    StopTunnelHealthMonitor();
    try {
        sessionVault_.Clear();
    } catch (...) {
    }
    resourceStore_.Delete();
    storedSession_.reset();
    sessionSummary_.reset();
    resourceSnapshot_.reset();
    std::wstring message = Tr(L"Sign-in session expired. Please sign in again.");
    proxyState_ = ProxyRuntimeState{ProxyRuntimeState::Kind::Failed, {}, Tr(L"Sign-in session expired")};
    loginState_ = LoginState{LoginState::Kind::Failed, 0, message};
    SetConnection(ConnectionPhase::Failed, message);
    SetBanner(BannerSeverity::Error, message);
    Notify(Tr(L"Sign-in session expired"), message);
}

void AppModel::SetSystemProxyEnabled(bool enabled) {
    UpdateProfile([enabled](Profile& p) { p.useSystemProxy = enabled; });
    if (!IsProxyRunning()) {
        SetBanner(BannerSeverity::Info, enabled ? Tr(L"System proxy will turn on automatically when the proxy starts")
                                                : Tr(L"System proxy preference is off"));
        return;
    }
    if (enabled) EnableSystemProxy(proxyState_.endpoint);
    else DisableSystemProxy();
}

void AppModel::SetSystemProxyMode(SystemProxyMode mode) {
    if (mode == profile_.systemProxyMode) return;
    UpdateProfile([mode](Profile& p) { p.systemProxyMode = mode; });
    // Re-apply right away when the system proxy is active.
    if (IsSystemProxyEnabled() && IsProxyRunning()) EnableSystemProxy(proxyState_.endpoint);
}

void AppModel::EnableSystemProxy(const ProxyEndpoint& endpoint) {
    systemProxyState_ = SystemProxyState{SystemProxyState::Kind::Enabling, {}};
    Changed();
    std::string serverHost = profile_.serverHost;
    SystemProxyMode mode = profile_.systemProxyMode;
    RunAsyncVoid(
        networkQueue_, nullptr, [endpoint, serverHost, mode] { SystemProxy::Enable(endpoint, serverHost, mode); },
        [this] {
            systemProxyState_ = SystemProxyState{SystemProxyState::Kind::Enabled, {}};
            SetBanner(BannerSeverity::Success, Tr(L"System proxy enabled"));
        },
        [this](const ErrorInfo& error) {
            systemProxyState_ = SystemProxyState{SystemProxyState::Kind::Failed, error.message};
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not enable system proxy: %1$@", {error.message}));
        });
}

void AppModel::DisableSystemProxy(std::function<void()> then) {
    auto kind = systemProxyState_.kind;
    if (kind == SystemProxyState::Kind::Disabled || kind == SystemProxyState::Kind::Disabling) {
        if (then) then();
        return;
    }
    systemProxyState_ = SystemProxyState{SystemProxyState::Kind::Disabling, {}};
    Changed();
    RunAsyncVoid(
        networkQueue_, nullptr, [] { SystemProxy::Restore(); },
        [this, then] {
            systemProxyState_ = SystemProxyState{};
            SetBanner(BannerSeverity::Info, Tr(L"System proxy disabled"));
            if (then) then();
        },
        [this, then](const ErrorInfo& error) {
            systemProxyState_ = SystemProxyState{SystemProxyState::Kind::Failed, error.message};
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not disable system proxy: %1$@", {error.message}));
            if (then) then();
        });
}

// ---- Helper ------------------------------------------------------------------------------

void AppModel::RefreshHelperState(bool forceVersions) {
    statusQueue_.Enqueue([this, forceVersions] {
        bool installed = HelperClient::IsInstalled();
        bool running = HelperClient::IsRunning();
        std::optional<std::string> installedVersion, bundledVersion;
        if (forceVersions) {
            installedVersion = HelperClient::InstalledVersion();
            bundledVersion = HelperClient::BundledVersion();
        }
        Dispatcher::Post([this, installed, running, forceVersions, installedVersion, bundledVersion] {
            bool changed = installed != helperInstalled_ || running != helperRunning_;
            helperInstalled_ = installed;
            helperRunning_ = running;
            if (forceVersions || changed) {
                if (installedVersion) helperVersionText_ = Widen(*installedVersion);
                else helperVersionText_ = installed ? (running ? Tr(L"Unknown") : Tr(L"Stopped")) : Tr(L"Not installed");
                if (forceVersions) bundledHelperVersionText_ = bundledVersion ? Widen(*bundledVersion) : Tr(L"Unknown");
                if (changed && !forceVersions) RefreshHelperState(true);
                Changed();
            }
        });
    });
}

void AppModel::EnsureHelperThen(const std::wstring& reason, std::function<void()> onReady,
                                std::function<void(const ErrorInfo&)> onError) {
    helperActivity_ = HelperActivityState{HelperActivityState::Kind::Checking, Tr(L"Checking privileged component")};
    Changed();
    RunAsync<bool>(
        networkQueue_, nullptr,
        [] {
            if (!HelperClient::RequiresInstallOrUpgrade()) return false;
            HelperClient::Install();
            return true;
        },
        [this, onReady, reason](bool installed) {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Succeeded,
                                                  installed ? Tr(L"Privileged component is ready")
                                                            : Tr(L"Privileged component is up to date")};
            helperInstalled_ = true;
            helperRunning_ = true;
            if (installed) SetBanner(BannerSeverity::Success, Tr(L"Privileged component is ready"));
            RefreshHelperState(true);
            Changed();
            if (onReady) onReady();
        },
        [this, onError](const ErrorInfo& error) {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Failed, error.message};
            SetBanner(error.kind == ErrorKind::Cancelled ? BannerSeverity::Warning : BannerSeverity::Error,
                      TrFormat(L"Could not install privileged component: %1$@", {error.message}));
            RefreshHelperState(true);
            if (onError) onError(error);
        });
}

void AppModel::InstallHelper() {
    if (IsVpnConnectedOrConnecting() && IsTunnelRunning()) {
        SetBanner(BannerSeverity::Warning, Tr(L"Cannot install or update the privileged component while VPN is connecting or connected"));
        return;
    }
    helperActivity_ = HelperActivityState{HelperActivityState::Kind::Installing, Tr(L"Installing privileged component")};
    Changed();
    RunAsyncVoid(
        networkQueue_, nullptr, [] { HelperClient::Install(); },
        [this] {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Succeeded, Tr(L"Privileged component is ready")};
            helperInstalled_ = helperRunning_ = true;
            SetBanner(BannerSeverity::Success, Tr(L"Privileged component is ready"));
            RefreshHelperState(true);
        },
        [this](const ErrorInfo& error) {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Failed, error.message};
            SetBanner(error.kind == ErrorKind::Cancelled ? BannerSeverity::Warning : BannerSeverity::Error,
                      TrFormat(L"Could not install privileged component: %1$@", {error.message}));
            RefreshHelperState(true);
        });
}

void AppModel::UninstallHelper() {
    if (IsVpnConnectedOrConnecting() && EffectiveRouteMode() == RouteMode::Tun) {
        SetBanner(BannerSeverity::Warning, Tr(L"Cannot uninstall the privileged component while VPN is connecting or connected"));
        return;
    }
    SetBanner(BannerSeverity::Info, Tr(L"Uninstalling privileged component"));
    helperActivity_ = HelperActivityState{HelperActivityState::Kind::Installing, Tr(L"Uninstalling privileged component")};
    Changed();
    RunAsyncVoid(
        networkQueue_, nullptr, [] { HelperClient::Uninstall(); },
        [this] {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Succeeded, Tr(L"Privileged component uninstalled")};
            helperInstalled_ = helperRunning_ = false;
            UpdateProfile([](Profile& p) { p.routeMode = RouteMode::Proxy; });
            SetBanner(BannerSeverity::Success, Tr(L"Privileged component uninstalled"));
            RefreshHelperState(true);
        },
        [this](const ErrorInfo& error) {
            helperActivity_ = HelperActivityState{HelperActivityState::Kind::Failed, error.message};
            SetBanner(error.kind == ErrorKind::Cancelled ? BannerSeverity::Warning : BannerSeverity::Error,
                      TrFormat(L"Could not uninstall privileged component: %1$@", {error.message}));
            RefreshHelperState(true);
        });
}

// ---- VPN mode --------------------------------------------------------------------------

void AppModel::StartTunnelMode() {
    if (EffectiveRouteMode() != RouteMode::Tun) {
        SetBanner(BannerSeverity::Warning, Tr(L"VPN mode is not selected"));
        return;
    }
    if (!helperInstalled_) {
        SetBanner(BannerSeverity::Warning, Tr(L"Install the helper on the Privileged Component tab first"));
        return;
    }
    if (IsProxyRunning() || IsProxyBusy()) {
        SetBanner(BannerSeverity::Warning, Tr(L"Stop proxy mode first"));
        return;
    }
    if (!storedSession_) {
        RequestWebLogin(RouteMode::Tun);
        return;
    }
    if (IsTunnelRunning() || IsTunnelBusy()) {
        SetBanner(BannerSeverity::Info, Tr(L"VPN is already running"));
        return;
    }
    CancelTunnelReconnect();
    tunnelShouldStayConnected_ = true;
    if (!isRecoveringTunnelSession_) {
        tunnelHasConnected_ = false;
        tunnelReconnectAttempt_ = 0;
    }
    LaunchTunnel();
}

void AppModel::LaunchTunnel() {
    tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Starting, 0, {}};
    SetConnection(ConnectionPhase::Connecting, Tr(L"Starting VPN mode"));
    CancelAndReset(tunnelTask_);
    tunnelTask_ = MakeCancelToken();
    CancelToken token = tunnelTask_;

    auto fail = [this, token](const ErrorInfo& error) {
        if (IsCancelled(token)) return;
        Log(L"[Tunnel] start failed: " + error.message);
        StopTunnelHealthMonitor();
        networkQueue_.Enqueue([] {
            try {
                TunnelManager::Stop();
            } catch (...) {
            }
        });
        if (tunnelShouldStayConnected_ && tunnelHasConnected_ && error.kind != ErrorKind::Cancelled && !RequiresWebLogin(error)) {
            // It worked before (network drop, sleep, network switch): keep
            // retrying instead of failing.
            tunnelState_ = TunnelRuntimeState{};
            ScheduleTunnelReconnect(error.message);
            return;
        }
        tunnelShouldStayConnected_ = false;
        isRecoveringTunnelSession_ = false;
        tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Failed, 0, error.message};
        SetConnection(ConnectionPhase::Failed, error.message);
        SetBanner(BannerSeverity::Error, TrFormat(L"Could not start VPN: %1$@", {error.message}));
        if (RequiresWebLogin(error)) RequestWebLogin(RouteMode::Tun);
    };

    Log("[Tunnel] refreshing session and resource");
    RefreshSessionAndResource(
        token,
        [this, token, fail](RefreshResult refreshed) {
            if (IsCancelled(token)) return;
            atr::ClientConfig client = ClientConfiguration();
            std::string serverHost = profile_.serverHost;
            auto start = [this, token, fail, client, refreshed, serverHost] {
                RunAsyncVoid(
                    networkQueue_, token,
                    [client, refreshed, serverHost] {
                        auto configuration =
                            TunnelManager::MakeLaunchConfiguration(client, refreshed.first, refreshed.second, serverHost);
                        TunnelManager::Start(configuration);
                    },
                    [this] {
                        bool wasReconnect = isRecoveringTunnelSession_ && tunnelHasConnected_;
                        tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Running, 0, {}};
                        tunnelHasConnected_ = true;
                        tunnelReconnectAttempt_ = 0;
                        tunnelNetworkFingerprint_ = NetworkMonitor::Current().fingerprint;
                        tunnelNetworkLostAt_.reset();
                        BeginTrafficSession(isRecoveringTunnelSession_);
                        SetConnection(ConnectionPhase::Connected, Tr(L"VPN mode started"));
                        SetBanner(BannerSeverity::Success, wasReconnect ? Tr(L"VPN reconnected") : Tr(L"VPN mode started"));
                        Notify(Tr(L"Connected"), wasReconnect ? Tr(L"VPN reconnected") : Tr(L"VPN mode started"));
                        StartTunnelHealthMonitor();
                        isRecoveringTunnelSession_ = false;
                    },
                    fail);
            };
            // Install or upgrade the helper first when needed (UAC prompt).
            EnsureHelperThen(Tr(L"Installing privileged component"), start, fail);
        },
        fail);
}

void AppModel::StopTunnelInBackground(std::function<void()> then) {
    SampleTraffic(true);
    RunAsyncVoid(
        networkQueue_, nullptr,
        [] {
            try {
                TunnelManager::Stop();
            } catch (const AppError& error) {
                if (error.kind() != ErrorKind::HelperNotInstalled) throw;
            }
        },
        [then] {
            if (then) then();
        },
        [then](const ErrorInfo&) {
            if (then) then();
        });
}

void AppModel::StopTunnelMode() {
    tunnelShouldStayConnected_ = false;
    CancelTunnelReconnect();
    if (!IsTunnelRunning() && !IsTunnelBusy()) {
        tunnelState_ = TunnelRuntimeState{};
        SetConnection(ConnectionPhase::Disconnected, Tr(L"VPN is not running"));
        return;
    }
    tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Stopping, 0, {}};
    StopTunnelHealthMonitor();
    isRecoveringTunnelSession_ = false;
    SetConnection(ConnectionPhase::Disconnecting, Tr(L"Stopping VPN mode"));
    CancelAndReset(tunnelTask_);
    SampleTraffic(true);
    RunAsyncVoid(
        networkQueue_, nullptr, [] { TunnelManager::Stop(); },
        [this] {
            tunnelState_ = TunnelRuntimeState{};
            SetConnection(ConnectionPhase::Disconnected, Tr(L"VPN stopped"));
            SetBanner(BannerSeverity::Info, Tr(L"VPN mode stopped"));
            FinishTrafficSession();
        },
        [this](const ErrorInfo& error) {
            tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Failed, 0, error.message};
            SetConnection(ConnectionPhase::Failed, error.message);
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not stop VPN: %1$@", {error.message}));
            FinishTrafficSession();
        });
}

void AppModel::StartTunnelHealthMonitor() {
    StopTunnelHealthMonitor();
    healthTimer_ = Dispatcher::SetInterval(5000, [this] {
        if (healthCheckInFlight_ || tunnelState_.kind != TunnelRuntimeState::Kind::Running) return;
        healthCheckInFlight_ = true;
        RunAsync<std::optional<TunnelRuntimeStatus>>(
            statusQueue_, nullptr, [] { return TunnelManager::RuntimeStatus(); },
            [this](std::optional<TunnelRuntimeStatus> status) {
                healthCheckInFlight_ = false;
                if (!healthTimer_ || !status) return;
                if (status->status == "failed" || status->status == "stopped") HandleTunnelRuntimeStopped(*status);
            },
            [this](const ErrorInfo& error) {
                healthCheckInFlight_ = false;
                Log(L"[Tunnel] health monitor failed: " + error.message);
            });
    });
}

void AppModel::StopTunnelHealthMonitor() {
    Dispatcher::ClearTimer(healthTimer_);
}

void AppModel::HandleTunnelRuntimeStopped(const TunnelRuntimeStatus& status) {
    StopTunnelHealthMonitor();
    std::wstring message = status.message.empty() ? Tr(L"VPN privileged component stopped") : status.message;
    ErrorInfo error{ErrorKind::HelperFailed, message};
    if (IsStoredSessionInvalidError(error) && !isRecoveringTunnelSession_) {
        isRecoveringTunnelSession_ = true;
        tunnelState_ = TunnelRuntimeState{};
        SetConnection(ConnectionPhase::Connecting, Tr(L"Sign-in session expired. Restoring VPN."));
        SetBanner(BannerSeverity::Warning, Tr(L"Restoring sign-in session"));
        StartTunnelMode();
        return;
    }
    if (tunnelShouldStayConnected_ && tunnelHasConnected_) {
        ScheduleTunnelReconnect(message);
        return;
    }
    tunnelShouldStayConnected_ = false;
    isRecoveringTunnelSession_ = false;
    tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Failed, 0, message};
    SetConnection(ConnectionPhase::Failed, message);
    SetBanner(BannerSeverity::Error, TrFormat(L"VPN failed: %1$@", {message}));
    Notify(Tr(L"Disconnected"), TrFormat(L"VPN failed: %1$@", {message}));
    FinishTrafficSession();
}

void AppModel::HandleNetworkChanged() {
    // Let the path settle (Wi-Fi roaming, DHCP) before acting.
    Dispatcher::ClearTimer(networkChangeTimer_);
    networkChangeTimer_ = Dispatcher::SetTimeout(2000, [this] {
        networkChangeTimer_ = 0;
        EvaluateTunnelAfterNetworkChange();
    });
}

void AppModel::EvaluateTunnelAfterNetworkChange() {
    if (tunnelState_.kind == TunnelRuntimeState::Kind::Reconnecting) {
        UpdateReconnectingState(tunnelState_.attempt);
        return;
    }
    if (!tunnelShouldStayConnected_ || tunnelState_.kind != TunnelRuntimeState::Kind::Running) return;
    auto snapshot = NetworkMonitor::Current();
    if (!snapshot.isAvailable) {
        // Short drops (roaming) often recover on their own; if the link really
        // died the helper reports a failure and the reconnect loop waits for
        // the network to return.
        if (!tunnelNetworkLostAt_) tunnelNetworkLostAt_ = MonotonicSeconds();
        SetConnection(ConnectionPhase::Connecting, Tr(L"Network unavailable. VPN will reconnect when the network returns."));
        return;
    }
    double outage = tunnelNetworkLostAt_ ? MonotonicSeconds() - *tunnelNetworkLostAt_ : 0;
    tunnelNetworkLostAt_.reset();
    if (!snapshot.fingerprint) return;
    if (!tunnelNetworkFingerprint_) {
        tunnelNetworkFingerprint_ = snapshot.fingerprint;
        return;
    }
    if (*snapshot.fingerprint != *tunnelNetworkFingerprint_) {
        ScheduleTunnelReconnect(L"network changed");
    } else if (outage >= kTunnelOutageReconnectThreshold) {
        ScheduleTunnelReconnect(L"network returned after " + std::to_wstring(static_cast<int>(outage)) + L"s");
    } else if (connectionState_.phase == ConnectionPhase::Connecting) {
        SetConnection(ConnectionPhase::Connected, Tr(L"VPN mode started"));
    }
}

void AppModel::ScheduleTunnelReconnect(const std::wstring& reason) {
    if (!tunnelShouldStayConnected_ || reconnectPending_) return;
    reconnectPending_ = true;
    StopTunnelHealthMonitor();
    int attempt = ++tunnelReconnectAttempt_;
    tunnelState_ = TunnelRuntimeState{TunnelRuntimeState::Kind::Reconnecting, attempt, {}};
    UpdateReconnectingState(attempt);
    if (attempt == 1) {
        SetBanner(BannerSeverity::Warning, Tr(L"VPN connection lost. Reconnecting…"));
        Notify(Tr(L"Reconnecting"), Tr(L"VPN connection lost. Reconnecting…"));
    }
    Log(L"[Tunnel] reconnect scheduled attempt=" + std::to_wstring(attempt) + L" reason=" + reason);

    StopTunnelInBackground([this, attempt] {
        if (!reconnectPending_) return;
        Dispatcher::ClearTimer(reconnectTimer_);
        reconnectTimer_ = Dispatcher::SetTimeout(TunnelReconnectDelayMs(attempt), [this, attempt] {
            reconnectTimer_ = 0;
            ReconnectWhenNetworkAvailable(attempt);
        });
    });
}

void AppModel::ReconnectWhenNetworkAvailable(int attempt) {
    if (!reconnectPending_ || !tunnelShouldStayConnected_ || tunnelState_.kind != TunnelRuntimeState::Kind::Reconnecting) {
        return;
    }
    if (!NetworkMonitor::Current().isAvailable) {
        UpdateReconnectingState(attempt);
        reconnectTimer_ = Dispatcher::SetTimeout(1000, [this, attempt] {
            reconnectTimer_ = 0;
            ReconnectWhenNetworkAvailable(attempt);
        });
        return;
    }
    reconnectPending_ = false;
    isRecoveringTunnelSession_ = true;
    Log(L"[Tunnel] reconnect attempt=" + std::to_wstring(attempt) + L" starting");
    LaunchTunnel();
}

void AppModel::CancelTunnelReconnect() {
    reconnectPending_ = false;
    Dispatcher::ClearTimer(reconnectTimer_);
}

void AppModel::UpdateReconnectingState(int attempt) {
    std::wstring message = NetworkMonitor::Current().isAvailable
                               ? TrFormat(L"Connection lost. Reconnecting VPN (attempt %1$@)", {std::to_wstring(attempt)})
                               : Tr(L"Network unavailable. VPN will reconnect when the network returns.");
    if (connectionState_.phase == ConnectionPhase::Connecting && connectionState_.message == message) return;
    SetConnection(ConnectionPhase::Connecting, message);
}

void AppModel::OnSystemSuspend() {
    suspendedAt_ = static_cast<double>(time(nullptr));
}

void AppModel::OnSystemResume() {
    if (!suspendedAt_) return;
    double slept = static_cast<double>(time(nullptr)) - *suspendedAt_;
    suspendedAt_.reset();
    if (tunnelShouldStayConnected_ && tunnelState_.kind == TunnelRuntimeState::Kind::Running &&
        slept >= kTunnelWakeReconnectThreshold) {
        ScheduleTunnelReconnect(L"system woke after " + std::to_wstring(static_cast<int>(slept)) + L"s of sleep");
    }
}

// ---- Sign-in -----------------------------------------------------------------------------

bool AppModel::HasCapturePolicy(const atr::AuthMethod& method) const {
    return CapturePolicy::Make(method.authType, profile_.serverHost, method.loginUrl).has_value();
}

std::optional<atr::AuthMethod> AppModel::PreferredWebLoginMethod(const std::vector<atr::AuthMethod>& methods) const {
    if (methods.empty()) return std::nullopt;
    if (profile_.preferredAuthType) {
        if (!profile_.loginDomain.empty()) {
            for (const auto& m : methods) {
                if (m.loginDomain == profile_.loginDomain && m.authType == *profile_.preferredAuthType) return m;
            }
        }
        for (const auto& m : methods) {
            if (m.authType == *profile_.preferredAuthType) return m;
        }
    }
    if (!profile_.loginDomain.empty()) {
        for (const auto& m : methods) {
            if (m.loginDomain == profile_.loginDomain) return m;
        }
    }
    return methods.front();
}

void AppModel::NormalizeLoginSelectionDefaults(const std::vector<atr::AuthMethod>& methods) {
    if (methods.empty()) return;
    atr::AuthMethod suggested = PreferredWebLoginMethod(methods).value_or(methods.front());
    bool domainValid = false, typeValid = false;
    for (const auto& m : methods) {
        if (m.loginDomain == profile_.loginDomain) domainValid = true;
        if (profile_.preferredAuthType && m.authType == *profile_.preferredAuthType) typeValid = true;
    }
    bool updateDomain = profile_.loginDomain.empty() || !domainValid;
    bool updateType = !profile_.preferredAuthType || !typeValid;
    if (!updateDomain && !updateType) return;
    UpdateProfile([&](Profile& p) {
        if (updateDomain) p.loginDomain = suggested.loginDomain;
        if (updateType) p.preferredAuthType = suggested.authType;
    });
}

void AppModel::RefreshLoginMethods() {
    if (!IsLoginConfigurationReady()) {
        loginState_ = LoginState{};
        availableLoginMethods_.clear();
        SetBanner(BannerSeverity::Warning, Tr(L"Enter and save the server address first"));
        return;
    }
    CancelAndReset(loginTask_);
    loginTask_ = MakeCancelToken();
    loginState_ = LoginState{LoginState::Kind::LoadingMethods};
    SetBanner(BannerSeverity::Info, Tr(L"Loading sign-in methods"));
    atr::AuthConfig config = AuthConfiguration();
    RunAsync<std::vector<atr::AuthMethod>>(
        authQueue_, loginTask_, [engine = authEngine_, config] { return engine->LoadMethods(config); },
        [this](std::vector<atr::AuthMethod> methods) {
            NormalizeLoginSelectionDefaults(methods);
            int supported = static_cast<int>(std::count_if(methods.begin(), methods.end(), [this](const atr::AuthMethod& m) {
                return HasCapturePolicy(m);
            }));
            availableLoginMethods_ = methods;
            loginState_ = supported > 0 ? LoginState{LoginState::Kind::Ready, supported, {}}
                                        : LoginState{LoginState::Kind::Failed, 0, Tr(L"No supported WebView sign-in method found")};
            SetBanner(BannerSeverity::Info, methods.empty() ? Tr(L"No sign-in methods found") : Tr(L"Sign-in methods refreshed"));
        },
        [this](const ErrorInfo& error) {
            loginState_ = LoginState{LoginState::Kind::Failed, 0, error.message};
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not load sign-in methods: %1$@", {error.message}));
        });
}

void AppModel::StartWebLogin(std::optional<atr::AuthMethod> method, bool allowSilent) {
    if (!IsLoginConfigurationReady()) {
        loginState_ = LoginState{LoginState::Kind::Failed, 0, Tr(L"Enter the server address in Settings first")};
        SetBanner(BannerSeverity::Warning, Tr(L"Enter and save the server address first"));
        return;
    }
    CancelAndReset(loginTask_);
    loginTask_ = MakeCancelToken();
    loginState_ = LoginState{LoginState::Kind::LoadingMethods};
    SetBanner(BannerSeverity::Info, Tr(L"Preparing web sign-in"));
    atr::AuthConfig config = AuthConfiguration();
    std::string serverHost = profile_.serverHost;
    Profile profile = profile_;
    struct Result {
        std::vector<atr::AuthMethod> methods;
        std::optional<WebLoginSession> session;
    };
    SessionVault vault;
    bool silent = allowSilent && settings_.webLoginCompleted;
    RunAsync<Result>(
        authQueue_, loginTask_,
        [engine = authEngine_, config, method, serverHost, profile, vault] {
            Result result;
            result.methods = engine->LoadMethods(config);
            std::optional<atr::AuthMethod> target = method;
            if (!target) {
                std::vector<atr::AuthMethod> supported;
                for (const auto& m : result.methods) {
                    if (CapturePolicy::Make(m.authType, serverHost, m.loginUrl)) supported.push_back(m);
                }
                // Same preference order as PreferredWebLoginMethod.
                auto pick = [&]() -> std::optional<atr::AuthMethod> {
                    if (supported.empty()) return std::nullopt;
                    if (profile.preferredAuthType) {
                        for (const auto& m : supported) {
                            if (m.loginDomain == profile.loginDomain && m.authType == *profile.preferredAuthType) return m;
                        }
                        for (const auto& m : supported) {
                            if (m.authType == *profile.preferredAuthType) return m;
                        }
                    }
                    for (const auto& m : supported) {
                        if (!profile.loginDomain.empty() && m.loginDomain == profile.loginDomain) return m;
                    }
                    return supported.front();
                };
                target = pick();
            }
            if (!target) return result;
            std::string deviceId = vault.LoadOrCreateDeviceId();
            result.session = engine->ResolveWebLoginSession(*target, deviceId);
            return result;
        },
        [this, silent](Result result) {
            availableLoginMethods_ = result.methods;
            if (!result.session) {
                std::wstring message = Tr(L"No supported WebView sign-in method found");
                loginState_ = LoginState{LoginState::Kind::Failed, 0, message};
                pendingConnectionMode_.reset();
                SetBanner(BannerSeverity::Error, message);
                return;
            }
            webLoginSession_ = result.session;
            webLoginHidden_ = silent;
            webLoginDeferred_ = false;
            loginState_ = LoginState{LoginState::Kind::Presenting, 0, result.session->title};
            if (silent) SetBanner(BannerSeverity::Info, Tr(L"Restoring sign-in session"));
            else SetBanner(BannerSeverity::Info, TrFormat(L"Opened %1$@", {result.session->title}));
            Log("[Login] open web session silent=" + std::to_string(silent) + " start=" + LoggableUrl(Narrow(result.session->startUrl)));
            if (onWebLoginSessionChanged) onWebLoginSessionChanged();
        },
        [this](const ErrorInfo& error) {
            loginState_ = LoginState{LoginState::Kind::Failed, 0, error.message};
            pendingConnectionMode_.reset();
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not open sign-in: %1$@", {error.message}));
        });
}

void AppModel::ResetWebLoginPresentation() {
    webLoginHidden_ = false;
    webLoginDeferred_ = false;
}

void AppModel::PresentWebLogin() {
    if (!webLoginSession_ || !webLoginHidden_ || webLoginDeferred_) return;
    bool foreground = false;
    if (HWND window = GetForegroundWindow()) {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        foreground = processId == GetCurrentProcessId();
    }
    bool userWaiting = foreground || (lastUserActionAt_ && MonotonicSeconds() - *lastUserActionAt_ < 60) ||
                       !settings_.showNotifications;
    if (userWaiting) {
        webLoginHidden_ = false;
        SetBanner(BannerSeverity::Info, TrFormat(L"Opened %1$@", {webLoginSession_->title}));
        if (onWebLoginSessionChanged) onWebLoginSessionChanged();
    } else {
        // A background sign-in (for example an automatic reconnect) must not
        // steal focus: notify and show the window when the user returns.
        webLoginDeferred_ = true;
        SetBanner(BannerSeverity::Info, Tr(L"Sign in again to reconnect"));
        Notify(L"NulConnect", Tr(L"Sign in again to reconnect"));
    }
}

void AppModel::PresentDeferredWebLogin() {
    if (!webLoginDeferred_ || !webLoginSession_) return;
    ResetWebLoginPresentation();
    SetBanner(BannerSeverity::Info, TrFormat(L"Opened %1$@", {webLoginSession_->title}));
    if (onWebLoginSessionChanged) onWebLoginSessionChanged();
}

void AppModel::CancelWebLogin() {
    CancelAndReset(loginTask_);
    ResetWebLoginPresentation();
    bool hadSession = webLoginSession_.has_value();
    webLoginSession_.reset();
    pendingConnectionMode_.reset();
    if (hadSession && onWebLoginSessionChanged) onWebLoginSessionChanged();
    if (loginState_.kind == LoginState::Kind::Succeeded) return;
    loginState_ = LoginState{};
    SetBanner(BannerSeverity::Info, Tr(L"Sign-in cancelled"));
}

void AppModel::RequestWebLogin(RouteMode mode) {
    pendingConnectionMode_ = mode;
    StartWebLogin(std::nullopt, true);
}

void AppModel::ContinuePendingConnectionAfterLogin() {
    if (!pendingConnectionMode_) return;
    RouteMode mode = *pendingConnectionMode_;
    pendingConnectionMode_.reset();
    if (mode == RouteMode::Proxy) StartProxyMode();
    else StartTunnelMode();
}

void AppModel::CompleteWebLogin(const std::wstring& callbackUrl) {
    if (!webLoginSession_) {
        SetBanner(BannerSeverity::Warning, Tr(L"Sign-in session does not exist"));
        return;
    }
    loginState_ = LoginState{LoginState::Kind::Finalizing};
    SetBanner(BannerSeverity::Info, Tr(L"Completing sign-in"));
    CancelAndReset(loginTask_);
    loginTask_ = MakeCancelToken();
    CancelToken token = loginTask_;
    atr::ClientConfig clientConfig = ClientConfiguration();
    std::string serverHost = profile_.serverHost;
    struct Result {
        atr::AuthChallenge challenge;
        std::optional<atr::ResourceSnapshot> snapshot;
    };
    RunAsync<Result>(
        authQueue_, token,
        [engine = authEngine_, callbackUrl, clientConfig, serverHost] {
            Result result;
            result.challenge = engine->CompleteWebLogin(callbackUrl);
            if (result.challenge.kind == atr::ChallengeKind::Done) {
                try {
                    std::string bytes = engine->FetchClientResource();
                    atr::Client client(clientConfig);
                    client.SetSession(result.challenge.session);
                    client.SetResource(bytes, serverHost);
                    result.snapshot = client.Snapshot();
                } catch (const std::exception& error) {
                    Log(std::string("[Login] resource snapshot refresh failed: ") + error.what());
                }
            }
            return result;
        },
        [this](Result result) {
            switch (result.challenge.kind) {
            case atr::ChallengeKind::Done: {
                SaveSessionMaterial(result.challenge.session);
                if (!settings_.webLoginCompleted) {
                    settings_.webLoginCompleted = true;
                    SaveSettings();
                }
                loginState_ = LoginState{LoginState::Kind::Succeeded, 0, Tr(L"Session saved")};
                ResetWebLoginPresentation();
                webLoginSession_.reset();
                if (onWebLoginSessionChanged) onWebLoginSessionChanged();
                if (!IsProxyRunning()) SetConnection(ConnectionPhase::Disconnected, Tr(L"Signed in. You can start the proxy."));
                SetBanner(BannerSeverity::Success, Tr(L"Signed in"));
                if (result.snapshot) SaveResourceSnapshot(*result.snapshot);
                ContinuePendingConnectionAfterLogin();
                break;
            }
            case atr::ChallengeKind::CallbackUrl:
                AbandonWebLoginSession();
                loginState_ = LoginState{LoginState::Kind::Failed, 0, TrFormat(L"Callback requires further handling: %1$@", {L"callback"})};
                SetBanner(BannerSeverity::Error, TrFormat(L"Sign-in requires another redirect: %1$@", {Widen(LoggableUrl(result.challenge.authUrl))}));
                break;
            case atr::ChallengeKind::Captcha:
                AbandonWebLoginSession();
                loginState_ = LoginState{LoginState::Kind::Failed, 0, Tr(L"Sign-in requires a CAPTCHA, which is not supported yet")};
                SetBanner(BannerSeverity::Error, Tr(L"Sign-in returned a CAPTCHA challenge"));
                break;
            case atr::ChallengeKind::SmsCode:
                AbandonWebLoginSession();
                loginState_ = LoginState{LoginState::Kind::Failed, 0, Tr(L"Sign-in requires SMS verification, which is not supported yet")};
                SetBanner(BannerSeverity::Error, Tr(L"Sign-in returned an SMS verification challenge"));
                break;
            }
            Changed();
        },
        [this](const ErrorInfo& error) {
            // The captured ticket is single-use; this sign-in attempt is over.
            AbandonWebLoginSession();
            pendingConnectionMode_.reset();
            loginState_ = LoginState{LoginState::Kind::Failed, 0, error.message};
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not complete sign-in: %1$@", {error.message}));
        });
}

void AppModel::AbandonWebLoginSession() {
    ResetWebLoginPresentation();
    if (!webLoginSession_) return;
    webLoginSession_.reset();
    if (onWebLoginSessionChanged) onWebLoginSessionChanged();
}

void AppModel::Logout() {
    if (isLoggingOut_) return;
    isLoggingOut_ = true;
    pendingConnectionMode_.reset();
    SetBanner(BannerSeverity::Info, Tr(L"Signing out"));
    PrepareForExit([this] {
        authQueue_.Enqueue([engine = authEngine_] { engine->Reset(); });
        if (clearWebLoginData) clearWebLoginData();
        // The SSO cookies are gone; a silent attempt could only time out.
        if (settings_.webLoginCompleted) {
            settings_.webLoginCompleted = false;
            SaveSettings();
        }
        ResetWebLoginPresentation();
        try {
            sessionVault_.Clear();
            resourceStore_.Delete();
            storedSession_.reset();
            sessionSummary_.reset();
            resourceSnapshot_.reset();
            availableLoginMethods_.clear();
            bool hadSession = webLoginSession_.has_value();
            webLoginSession_.reset();
            if (hadSession && onWebLoginSessionChanged) onWebLoginSessionChanged();
            loginState_ = LoginState{};
            SetBanner(BannerSeverity::Success, Tr(L"Signed out"));
        } catch (...) {
            SetBanner(BannerSeverity::Error, TrFormat(L"Could not sign out: %1$@", {CurrentError().message}));
        }
        isLoggingOut_ = false;
        Changed();
    });
}

void AppModel::PrepareForExit(std::function<void()> done) {
    tunnelShouldStayConnected_ = false;
    CancelTunnelReconnect();
    StopTunnelHealthMonitor();
    CancelAndReset(tunnelTask_);
    CancelAndReset(proxyTask_);
    SampleTraffic(true);

    bool stopTunnel = IsTunnelRunning() || IsTunnelBusy();
    bool restoreProxy = systemProxyState_.kind != SystemProxyState::Kind::Disabled;
    auto service = std::move(proxyService_);
    proxyService_.reset();
    RunAsyncVoid(
        networkQueue_, nullptr,
        [stopTunnel, restoreProxy, service]() mutable {
            if (stopTunnel) {
                try {
                    TunnelManager::Stop();
                } catch (...) {
                }
            }
            if (restoreProxy) {
                try {
                    SystemProxy::Restore();
                } catch (...) {
                }
            }
            if (service) service->Stop();
            service.reset();
        },
        [this, done] {
            tunnelState_ = TunnelRuntimeState{};
            proxyState_ = ProxyRuntimeState{};
            systemProxyState_ = SystemProxyState{};
            SetConnection(ConnectionPhase::Disconnected, Tr(L"Network component stopped"));
            FinishTrafficSession();
            if (done) done();
        },
        [done](const ErrorInfo&) {
            if (done) done();
        });
}

// ---- Traffic ---------------------------------------------------------------------------

void AppModel::SetTrafficObserver(const std::string& key, bool observing) {
    if (observing) trafficObservers_.insert(key);
    else trafficObservers_.erase(key);
    UpdateTrafficSampling();
}

void AppModel::BeginTrafficSession(bool continuing) {
    if (continuing) {
        trafficOffset_ = traffic_.counters;
    } else {
        trafficOffset_ = {};
        traffic_ = TrafficStatistics{};
        traffic_.connectionStartedAt = MonotonicSeconds();
        traffic_.isLive = true;
        trafficHistory_.clear();
    }
    previousTrafficSample_.reset();
    UpdateTrafficSampling();
    Changed();
}

void AppModel::FinishTrafficSession() {
    Dispatcher::ClearTimer(trafficTimer_);
    previousTrafficSample_.reset();
    traffic_.uploadBytesPerSecond = 0;
    traffic_.downloadBytesPerSecond = 0;
    if (traffic_.connectionStartedAt) traffic_.connectionDuration = std::max(0.0, MonotonicSeconds() - *traffic_.connectionStartedAt);
    traffic_.isLive = false;
    Changed();
}

void AppModel::UpdateTrafficSampling() {
    bool wanted = !trafficObservers_.empty() && (IsProxyRunning() || tunnelState_.kind == TunnelRuntimeState::Kind::Running);
    if (wanted && !trafficTimer_) {
        SampleTraffic();
        trafficTimer_ = Dispatcher::SetInterval(1000, [this] { SampleTraffic(); });
    } else if (!wanted && trafficTimer_) {
        Dispatcher::ClearTimer(trafficTimer_);
        previousTrafficSample_.reset();
        traffic_.uploadBytesPerSecond = 0;
        traffic_.downloadBytesPerSecond = 0;
        Changed();
    }
}

void AppModel::SampleTraffic(bool final) {
    if (proxyService_) {
        try {
            ApplyTrafficCounters(proxyService_->Traffic());
        } catch (...) {
        }
        return;
    }
    if (tunnelState_.kind != TunnelRuntimeState::Kind::Running && !final) {
        if (!IsProxyRunning() && !IsTunnelRunning()) FinishTrafficSession();
        return;
    }
    // Tunnel counters live in the helper; the last periodic sample stands in
    // for the final value rather than blocking the UI thread on the pipe.
    if (final || tunnelState_.kind != TunnelRuntimeState::Kind::Running || trafficSampleInFlight_) return;
    trafficSampleInFlight_ = true;
    RunAsync<std::optional<TunnelRuntimeStatus>>(
        statusQueue_, nullptr, [] { return TunnelManager::RuntimeStatus(); },
        [this](std::optional<TunnelRuntimeStatus> status) {
            trafficSampleInFlight_ = false;
            if (status && tunnelState_.kind == TunnelRuntimeState::Kind::Running) ApplyTrafficCounters(status->traffic);
        },
        [this](const ErrorInfo&) { trafficSampleInFlight_ = false; });
}

void AppModel::ApplyTrafficCounters(const TrafficCounters& raw) {
    TrafficCounters counters{trafficOffset_.uploadedBytes + raw.uploadedBytes, trafficOffset_.downloadedBytes + raw.downloadedBytes,
                             trafficOffset_.uploadedPackets + raw.uploadedPackets,
                             trafficOffset_.downloadedPackets + raw.downloadedPackets};
    double now = MonotonicSeconds();
    double up = 0, down = 0;
    if (previousTrafficSample_) {
        double elapsed = now - previousTrafficSample_->second;
        if (elapsed > 0) {
            auto diff = [](uint64_t value, uint64_t previous) { return value >= previous ? value - previous : 0; };
            up = static_cast<double>(diff(counters.uploadedBytes, previousTrafficSample_->first.uploadedBytes)) / elapsed;
            down = static_cast<double>(diff(counters.downloadedBytes, previousTrafficSample_->first.downloadedBytes)) / elapsed;
        }
        trafficHistory_.push_back({up, down});
        while (trafficHistory_.size() > kTrafficHistoryLength) trafficHistory_.pop_front();
    }
    previousTrafficSample_ = std::make_pair(counters, now);
    traffic_.counters = counters;
    traffic_.uploadBytesPerSecond = up;
    traffic_.downloadBytesPerSecond = down;
    if (traffic_.connectionStartedAt) traffic_.connectionDuration = std::max(0.0, now - *traffic_.connectionStartedAt);
    traffic_.isLive = true;
    Changed();
}

}  // namespace nc
