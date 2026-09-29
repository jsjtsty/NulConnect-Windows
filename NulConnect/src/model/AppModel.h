#pragma once

#include "core/Dispatcher.h"
#include "model/AuthEngine.h"
#include "model/NetworkMonitor.h"
#include "model/ProxyService.h"
#include "model/Stores.h"
#include "model/TunnelManager.h"
#include "model/Types.h"

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace nc {

enum class BannerSeverity { Info, Success, Warning, Error };

struct Banner {
    uint64_t id = 0;
    BannerSeverity severity = BannerSeverity::Info;
    std::wstring text;
};

struct TrafficSample {
    double upload = 0;
    double download = 0;
};

// Application state and connection logic, ported from the macOS AppModel.
// Lives on the UI thread; blocking work runs on serial background queues and
// reports back through the Dispatcher. Observers are notified (coalesced) on
// every state change.
class AppModel {
public:
    AppModel();
    ~AppModel();

    void Initialize();

    // ---- Observation ---------------------------------------------------------
    using Observer = std::function<void()>;
    int Subscribe(Observer observer);
    void Unsubscribe(int token);

    // ---- State -----------------------------------------------------------------
    const Profile& profile() const { return profile_; }
    const AppSettings& settings() const { return settings_; }
    const ConnectionState& connectionState() const { return connectionState_; }
    const ProxyRuntimeState& proxyState() const { return proxyState_; }
    const SystemProxyState& systemProxyState() const { return systemProxyState_; }
    const TunnelRuntimeState& tunnelState() const { return tunnelState_; }
    const LoginState& loginState() const { return loginState_; }
    const HelperActivityState& helperActivity() const { return helperActivity_; }
    const std::optional<SessionSummary>& sessionSummary() const { return sessionSummary_; }
    const std::optional<atr::ResourceSnapshot>& resourceSnapshot() const { return resourceSnapshot_; }
    const std::optional<WebLoginSession>& webLoginSession() const { return webLoginSession_; }
    const std::vector<atr::AuthMethod>& availableLoginMethods() const { return availableLoginMethods_; }
    const Banner& banner() const { return banner_; }
    const std::wstring& helperVersionText() const { return helperVersionText_; }
    const std::wstring& bundledHelperVersionText() const { return bundledHelperVersionText_; }
    const TrafficStatistics& traffic() const { return traffic_; }
    const std::deque<TrafficSample>& trafficHistory() const { return trafficHistory_; }
    bool isLoggingOut() const { return isLoggingOut_; }

    // ---- Derived state ---------------------------------------------------------
    bool IsHelperInstalled() const { return helperInstalled_; }
    bool IsHelperRunning() const { return helperRunning_; }
    bool IsHelperActivityBusy() const { return helperActivity_.IsBusy(); }
    RouteMode EffectiveRouteMode() const;
    std::wstring RoutePresentationModeTitle() const;
    bool IsProxyRunning() const { return proxyState_.kind == ProxyRuntimeState::Kind::Running; }
    bool IsProxyBusy() const;
    bool IsTunnelRunning() const;
    bool IsTunnelBusy() const;
    bool IsSystemProxyEnabled() const { return systemProxyState_.kind == SystemProxyState::Kind::Enabled; }
    bool IsSystemProxyBusy() const;
    bool IsVpnConnectedOrConnecting() const;
    bool IsConnectionActive() const { return IsProxyRunning() || IsTunnelRunning(); }
    bool IsPrimaryActionBusy() const;
    bool CanChangeSystemProxyPreference() const;
    bool CanChangeRouteMode() const;
    bool IsLocalProxyPortValid() const { return profile_.localProxyPort > 0; }
    bool IsLoginConfigurationReady() const;
    bool NeedsLogin() const { return !storedSession_; }
    std::wstring ProxyEndpointText() const;
    std::wstring ServerDisplayText() const;
    // Ready-to-paste commands for the local proxy; empty when the port is invalid.
    std::wstring TerminalProxyCommand() const;
    std::wstring SshProxyCommand() const;
    std::wstring PrimaryActionTitle() const;

    // ---- Actions -----------------------------------------------------------------
    void UpdateProfile(const std::function<void(Profile&)>& update);
    void UpdateSettings(const std::function<void(AppSettings&)>& update);
    void ResetProfileToDefaults();
    // Accepts a host name, host:port or a pasted portal link. Shows a banner
    // and returns false when the text is not a valid address.
    bool ConfigurePortal(const std::wstring& input);

    void PerformPrimaryAction();
    void StartProxyMode();
    void StopProxyMode();
    void StartTunnelMode();
    void ApplyDiagnosticLogging();
    void SyncHelperLogging();
    void StopTunnelMode();
    void SetRouteMode(RouteMode mode);
    void SetSystemProxyEnabled(bool enabled);
    void SetSystemProxyMode(SystemProxyMode mode);

    void RefreshLoginMethods();
    // With `allowSilent`, a sign-in that already succeeded once first runs
    // without a window and is shown only when the page waits for the user.
    void StartWebLogin(std::optional<atr::AuthMethod> method = std::nullopt, bool allowSilent = false);
    // True while the sign-in runs without a window (silent, or waiting to be
    // shown after the user returns).
    bool IsWebLoginHidden() const { return webLoginHidden_; }
    // The hidden sign-in page needs the user: show it, or notify when the
    // user is working elsewhere.
    void PresentWebLogin();
    void PresentDeferredWebLogin();
    void CancelWebLogin();
    void CompleteWebLogin(const std::wstring& callbackUrl);
    void Logout();

    void InstallHelper();
    void UninstallHelper();
    void RefreshHelperState(bool forceVersions = false);

    void SetTrafficObserver(const std::string& key, bool observing);
    void DismissBanner();

    void OnSystemSuspend();
    void OnSystemResume();
    // Stops every network mode and restores the system proxy, then calls
    // `done` on the UI thread.
    void PrepareForExit(std::function<void()> done);

    // ---- Hooks for the application shell -------------------------------------------
    std::function<void()> onWebLoginSessionChanged;
    std::function<void(const std::wstring& title, const std::wstring& message)> onNotify;
    // Deletes the embedded browser's cookies and cache (sign-out).
    std::function<void()> clearWebLoginData;

private:
    void Changed();
    void SetBanner(BannerSeverity severity, std::wstring text);
    void SetConnection(ConnectionPhase phase, std::wstring message);
    void Notify(const std::wstring& title, const std::wstring& message);
    Profile RuntimeProfile() const;
    atr::ClientConfig ClientConfiguration() const;
    atr::AuthConfig AuthConfiguration() const;
    void ScheduleProfilePersistence();
    void SaveSettings();
    void ApplyLaunchAtStartup();
    void PerformLaunchTasks();

    void SaveSessionMaterial(const atr::SessionMaterial& material);
    void SaveResourceSnapshot(const atr::ResourceSnapshot& snapshot);
    std::optional<atr::AuthMethod> PreferredWebLoginMethod(const std::vector<atr::AuthMethod>& methods) const;
    void NormalizeLoginSelectionDefaults(const std::vector<atr::AuthMethod>& methods);
    bool HasCapturePolicy(const atr::AuthMethod& method) const;
    void RequestWebLogin(RouteMode mode);
    void AbandonWebLoginSession();
    void ResetWebLoginPresentation();
    void ContinuePendingConnectionAfterLogin();

    // Refreshes the stored session and resource snapshot on the auth queue.
    using RefreshResult = std::pair<atr::SessionMaterial, atr::ResourceSnapshot>;
    void RefreshSessionAndResource(CancelToken token, std::function<void(RefreshResult)> onSuccess,
                                   std::function<void(const ErrorInfo&)> onError);
    void InvalidateStoredSession(const std::wstring& message);
    static bool IsStoredSessionInvalidError(const ErrorInfo& error);
    static bool RequiresWebLogin(const ErrorInfo& error);

    void LaunchProxy(bool recovering);
    void FinishStoppingProxyMode();
    void HandleProxySessionInvalidated(const std::wstring& message);
    void FailProxySessionInvalidated(const std::wstring& message);
    void EnableSystemProxy(const ProxyEndpoint& endpoint);
    void DisableSystemProxy(std::function<void()> then = nullptr);

    void LaunchTunnel();
    void StartTunnelHealthMonitor();
    void StopTunnelHealthMonitor();
    void HandleTunnelRuntimeStopped(const TunnelRuntimeStatus& status);
    void HandleNetworkChanged();
    void EvaluateTunnelAfterNetworkChange();
    void ScheduleTunnelReconnect(const std::wstring& reason);
    void CancelTunnelReconnect();
    void ReconnectWhenNetworkAvailable(int attempt);
    void UpdateReconnectingState(int attempt);
    void StopTunnelInBackground(std::function<void()> then);

    void EnsureHelperThen(const std::wstring& reason, std::function<void()> onReady,
                          std::function<void(const ErrorInfo&)> onError);

    void BeginTrafficSession(bool continuing);
    void FinishTrafficSession();
    void UpdateTrafficSampling();
    void SampleTraffic(bool final = false);
    void ApplyTrafficCounters(const TrafficCounters& raw);

    // Persistence
    ProfileStore profileStore_;
    SettingsStore settingsStore_;
    SessionVault sessionVault_;
    ResourceStore resourceStore_;

    // State
    Profile profile_;
    AppSettings settings_;
    ConnectionState connectionState_;
    ProxyRuntimeState proxyState_;
    SystemProxyState systemProxyState_;
    TunnelRuntimeState tunnelState_;
    LoginState loginState_;
    HelperActivityState helperActivity_;
    std::optional<SessionSummary> sessionSummary_;
    std::optional<atr::ResourceSnapshot> resourceSnapshot_;
    std::optional<atr::SessionMaterial> storedSession_;
    std::optional<WebLoginSession> webLoginSession_;
    std::vector<atr::AuthMethod> availableLoginMethods_;
    Banner banner_;
    std::wstring helperVersionText_;
    std::wstring bundledHelperVersionText_;
    bool helperInstalled_ = false;
    bool helperRunning_ = false;
    bool isLoggingOut_ = false;
    TrafficStatistics traffic_;
    std::deque<TrafficSample> trafficHistory_;

    // Workers
    SerialQueue authQueue_{"NulConnect Auth"};
    SerialQueue networkQueue_{"NulConnect Network"};
    SerialQueue statusQueue_{"NulConnect Status"};
    std::shared_ptr<AuthEngine> authEngine_ = std::make_shared<AuthEngine>();
    std::shared_ptr<ProxyService> proxyService_;
    NetworkMonitor networkMonitor_;

    CancelToken loginTask_;
    CancelToken proxyTask_;
    CancelToken tunnelTask_;
    Dispatcher::TimerId profileSaveTimer_ = 0;
    Dispatcher::TimerId healthTimer_ = 0;
    Dispatcher::TimerId trafficTimer_ = 0;
    Dispatcher::TimerId networkChangeTimer_ = 0;
    Dispatcher::TimerId reconnectTimer_ = 0;
    Dispatcher::TimerId helperPollTimer_ = 0;
    bool healthCheckInFlight_ = false;
    bool trafficSampleInFlight_ = false;

    std::optional<RouteMode> pendingConnectionMode_;
    bool isRecoveringTunnelSession_ = false;
    bool tunnelShouldStayConnected_ = false;
    bool tunnelHasConnected_ = false;
    std::optional<std::string> tunnelNetworkFingerprint_;
    std::optional<double> tunnelNetworkLostAt_;
    int tunnelReconnectAttempt_ = 0;
    bool reconnectPending_ = false;
    std::optional<double> suspendedAt_;
    bool webLoginHidden_ = false;
    bool webLoginDeferred_ = false;
    std::optional<double> lastUserActionAt_;

    TrafficCounters trafficOffset_;
    std::optional<std::pair<TrafficCounters, double>> previousTrafficSample_;
    std::set<std::string> trafficObservers_;

    std::map<int, Observer> observers_;
    int nextObserver_ = 1;
    bool notifyPending_ = false;
    uint64_t nextBannerId_ = 1;
};

}  // namespace nc
