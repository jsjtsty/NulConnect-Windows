#include "pch.h"
#include "windows/Pages.h"
#include "app/Resource.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "ui/Host.h"

namespace nc {

using namespace ui;

namespace {

void OpenUrl(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace

Page::Page(AppModel& model, Host* host) : StackPanel(Orientation::Vertical, 4.0f), model_(model), host_(host) {}

TextBlock* Page::AddSection(const std::wstring& title) {
    // Extra space above a section header, as in Windows Settings.
    Emplace<Spacer>(16.0f);
    auto* header = Add(SectionHeader(title));
    Emplace<Spacer>(2.0f);
    return header;
}

SettingsCard* Page::AddCard(Icon icon, const std::wstring& title, const std::wstring& description) {
    return Emplace<SettingsCard>(icon, title, description);
}

namespace {

// ---- Home ------------------------------------------------------------------------

class HomePage : public Page {
public:
    HomePage(AppModel& model, Host* host) : Page(model, host) {
        welcome_ = Emplace<Card>(24.0f);
        auto* welcomeStack =
            static_cast<StackPanel*>(welcome_->SetContent(std::make_unique<StackPanel>(Orientation::Vertical, 8.0f)));
        welcomeStack->Emplace<TextBlock>(Tr(L"Welcome to NulConnect"), TextStyle::Subtitle);
        welcomeStack->Emplace<TextBlock>(
            Tr(L"Enter the address of your organization's VPN portal. You can paste the link you open in a browser to sign in."),
            TextStyle::Body, TextColor::Secondary);
        auto* portalRow = welcomeStack->Emplace<StackPanel>(Orientation::Horizontal, 8.0f);
        portal_ = portalRow->Emplace<TextBox>();
        portal_->SetPlaceholder(Tr(L"VPN Portal Address"));
        portal_->SetPreferredWidth(320);
        portal_->onChange = [this](const std::wstring& text) {
            bool valid = PortalAddress::Parse(Narrow(text)).has_value();
            portal_->SetError(!Trim(text).empty() && !valid);
            portalContinue_->SetEnabled(valid);
        };
        portal_->onCommit = [this](const std::wstring& text) {
            if (PortalAddress::Parse(Narrow(text))) model_.ConfigurePortal(text);
        };
        portalContinue_ = portalRow->Emplace<Button>(Tr(L"Continue"), ButtonStyle::Accent);
        portalContinue_->SetEnabled(false);
        portalContinue_->onClick = [this] { model_.ConfigurePortal(portal_->Text()); };
        portalHint_ = welcomeStack->Emplace<TextBlock>(
            Tr(L"Enter a host name such as vpn.example.edu or a portal link starting with https://"), TextStyle::Caption,
            TextColor::Secondary);
        Emplace<Spacer>(8.0f);

        auto* hero = Emplace<Card>(28.0f);
        auto* stack = static_cast<StackPanel*>(hero->SetContent(std::make_unique<StackPanel>(Orientation::Vertical, 6.0f)));
        stack->SetAlignment(Alignment::Center);
        glyph_ = stack->Emplace<StatusGlyph>(96.0f);
        stack->Emplace<Spacer>(6.0f);
        phase_ = stack->Emplace<TextBlock>(L"", TextStyle::Title);
        phase_->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        message_ = stack->Emplace<TextBlock>(L"", TextStyle::Body, TextColor::Secondary);
        message_->SetAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        message_->SetMaxLines(3);
        stack->Emplace<Spacer>(14.0f);
        modeSwitch_ = stack->Emplace<Segmented>(
            std::vector<std::wstring>{RouteModeTitle(RouteMode::Proxy), RouteModeTitle(RouteMode::Tun)});
        modeSwitch_->SetDangerIndex(1);
        modeSwitch_->onChange = [this](int index) { model_.SetRouteMode(index == 1 ? RouteMode::Tun : RouteMode::Proxy); };
        stack->Emplace<Spacer>(6.0f);
        primary_ = stack->Emplace<Button>(L"", ButtonStyle::Accent);
        primary_->SetLarge(true);
        primary_->SetMinWidth(220);
        primary_->onClick = [this] { model_.PerformPrimaryAction(); };

        Emplace<Spacer>(8.0f);
        mode_ = AddCard(Icon::Swap, Tr(L"Mode"));
        modeValue_ = mode_->SetTrailing(ValueText(L""));
        account_ = AddCard(Icon::Person, Tr(L"Account"));
        accountValue_ = account_->SetTrailing(ValueText(L""));
        server_ = AddCard(Icon::Server, Tr(L"Server"));
        serverValue_ = server_->SetTrailing(ValueText(L""));
        proxy_ = AddCard(Icon::Network, Tr(L"Local Proxy"));
        auto row = std::make_unique<StackPanel>(Orientation::Horizontal, 8.0f);
        proxyValue_ = row->Add(ValueText(L""));
        copy_ = row->Emplace<Button>(L"", ButtonStyle::Subtle, Icon::Copy);
        copy_->SetTooltip(Tr(L"Copy"));
        copy_->onClick = [this] {
            if (CopyTextToClipboard(host_->Hwnd(), model_.ProxyEndpointText())) {
                copy_->SetIcon(Icon::Check);
                std::weak_ptr<bool> alive = alive_;
                Dispatcher::SetTimeout(1500, [this, alive] {
                    if (alive.lock()) copy_->SetIcon(Icon::Copy);
                });
            }
        };
        proxy_->SetTrailing(std::move(row));
        terminalCommand_ = AddCard(Icon::Document, Tr(L"Copy Terminal Proxy Command"),
                                   Tr(L"Sets the proxy environment variables in a PowerShell session."));
        terminalCommand_->SetClickable(true, Icon::Copy);
        terminalCommand_->onClick = [this] { CopyCommand(terminalCommand_, model_.TerminalProxyCommand()); };
        sshCommand_ = AddCard(Icon::Document, Tr(L"Copy SSH ProxyCommand Option"),
                              Tr(L"Uses connect.exe from Git for Windows to tunnel SSH through the SOCKS5 proxy."));
        sshCommand_->SetClickable(true, Icon::Copy);
        sshCommand_->onClick = [this] { CopyCommand(sshCommand_, model_.SshProxyCommand()); };

        trafficHeader_ = AddSection(Tr(L"Live Traffic"));
        trafficCard_ = Emplace<Card>(16.0f);
        auto* trafficStack =
            static_cast<StackPanel*>(trafficCard_->SetContent(std::make_unique<StackPanel>(Orientation::Vertical, 12.0f)));
        auto* rates = trafficStack->Emplace<StackPanel>(Orientation::Horizontal, 16.0f);
        download_ = rates->Emplace<RateTile>(Icon::ArrowDown, Tr(L"Download"), true);
        upload_ = rates->Emplace<RateTile>(Icon::ArrowUp, Tr(L"Upload"), false);
        chart_ = trafficStack->Emplace<TrafficChart>(96.0f);
        Refresh();
    }

    bool WantsTraffic() const override { return true; }

    void Refresh() override {
        bool configured = model_.IsLoginConfigurationReady();
        welcome_->SetVisible(!configured);
        const auto& state = model_.connectionState();
        glyph_->SetPhase(state.phase);
        phase_->SetText(PhaseTitle(state.phase));
        std::wstring message = state.message;
        if (message.empty()) {
            message = model_.NeedsLogin() ? Tr(L"Sign in to the server using single sign-on.") : model_.ServerDisplayText();
        }
        const auto& systemProxy = model_.systemProxyState();
        if (state.phase != ConnectionPhase::Failed && systemProxy.kind == SystemProxyState::Kind::Failed &&
            !systemProxy.message.empty()) {
            message = systemProxy.message;
        }
        message_->SetText(message);
        modeSwitch_->SetVisible(model_.IsHelperInstalled());
        modeSwitch_->SetSelected(model_.EffectiveRouteMode() == RouteMode::Tun ? 1 : 0, false);
        modeSwitch_->SetEnabled(model_.CanChangeRouteMode());
        const auto& summary = model_.sessionSummary();
        account_->SetVisible(summary.has_value() && !model_.NeedsLogin());
        if (summary) accountValue_->SetText(Widen(summary->username));
        bool running = model_.IsConnectionActive();
        primary_->SetText(model_.PrimaryActionTitle());
        primary_->SetIcon(running ? Icon::Power : (model_.NeedsLogin() ? Icon::SignIn : Icon::Power));
        primary_->SetStyle(running ? ButtonStyle::Danger : ButtonStyle::Accent);
        primary_->SetEnabled(!model_.IsPrimaryActionBusy() && model_.IsLoginConfigurationReady());
        modeValue_->SetText(model_.RoutePresentationModeTitle());
        serverValue_->SetText(model_.ServerDisplayText());
        proxyValue_->SetText(model_.ProxyEndpointText());
        bool proxyMode = model_.EffectiveRouteMode() == RouteMode::Proxy;
        proxy_->SetVisible(proxyMode);
        terminalCommand_->SetVisible(proxyMode);
        sshCommand_->SetVisible(proxyMode);
        bool live = running && model_.traffic().isLive;
        trafficHeader_->SetVisible(live);
        trafficCard_->SetVisible(live);
        if (live) {
            download_->SetValue(FormatRate(model_.traffic().downloadBytesPerSecond));
            upload_->SetValue(FormatRate(model_.traffic().uploadBytesPerSecond));
            chart_->SetSamples(model_.trafficHistory());
        }
    }

private:
    void CopyCommand(SettingsCard* card, const std::wstring& text) {
        if (text.empty() || !CopyTextToClipboard(host_->Hwnd(), text)) return;
        card->SetClickable(true, Icon::Check);
        std::weak_ptr<bool> alive = alive_;
        Dispatcher::SetTimeout(1500, [this, alive, card] {
            if (alive.lock()) card->SetClickable(true, Icon::Copy);
        });
    }

    SettingsCard* terminalCommand_;
    SettingsCard* sshCommand_;
    Segmented* modeSwitch_;
    SettingsCard* account_;
    TextBlock* accountValue_;
    Card* welcome_;
    TextBox* portal_;
    Button* portalContinue_;
    TextBlock* portalHint_;
    StatusGlyph* glyph_;
    TextBlock* phase_;
    TextBlock* message_;
    Button* primary_;
    SettingsCard* mode_;
    TextBlock* modeValue_;
    SettingsCard* server_;
    TextBlock* serverValue_;
    SettingsCard* proxy_;
    TextBlock* proxyValue_;
    Button* copy_;
    TextBlock* trafficHeader_;
    Card* trafficCard_;
    RateTile* download_;
    RateTile* upload_;
    TrafficChart* chart_;
};

// ---- Account ---------------------------------------------------------------------

class AccountPage : public Page {
public:
    AccountPage(AppModel& model, Host* host) : Page(model, host) {
        AddSection(Tr(L"VPN Portal"));
        auto* serverCard = AddCard(Icon::Server, Tr(L"Server"), Tr(L"Host name of the secure access portal."));
        host_box_ = serverCard->SetTrailing(std::make_unique<TextBox>());
        host_box_->SetPlaceholder(L"vpn.example.com");
        host_box_->SetPreferredWidth(280);
        host_box_->onCommit = [this](const std::wstring& text) {
            // An empty box clears the address; anything else must be a valid
            // host name or a pasted portal link.
            if (Trim(text).empty()) {
                model_.UpdateProfile([](Profile& p) { p.serverHost.clear(); });
            } else if (!model_.ConfigurePortal(text)) {
                host_box_->SetText(Widen(model_.profile().serverHost));
            }
            Refresh();
        };
        auto* portCard = AddCard(Icon::Plug, Tr(L"Port"));
        port_box_ = portCard->SetTrailing(std::make_unique<TextBox>());
        port_box_->SetDigitsOnly(true);
        port_box_->SetMaxLength(5);
        port_box_->SetPreferredWidth(120);
        port_box_->onCommit = [this](const std::wstring& text) {
            unsigned long value = std::wcstoul(text.c_str(), nullptr, 10);
            if (value >= 1 && value <= 65535) {
                model_.UpdateProfile([&](Profile& p) { p.serverPort = static_cast<uint16_t>(value); });
            } else {
                port_box_->SetText(std::to_wstring(model_.profile().serverPort));
            }
        };

        AddSection(Tr(L"Account"));
        account_ = AddCard(Icon::Person, Tr(L"Account"));
        accountValue_ = account_->SetTrailing(ValueText(L""));
        action_ = AddCard(Icon::SignIn, Tr(L"Log In"));
        action_->SetClickable(true);
        action_->onClick = [this] { OnAction(); };
        resources_ = AddCard(Icon::Network, Tr(L"Resources"));
        resourcesValue_ = resources_->SetTrailing(ValueText(L""));
        Refresh();
    }

    void Refresh() override {
        const Profile& profile = model_.profile();
        if (!host_box_->HasFocus()) host_box_->SetText(Widen(profile.serverHost));
        if (!port_box_->HasFocus()) port_box_->SetText(std::to_wstring(profile.serverPort));
        const auto& summary = model_.sessionSummary();
        if (summary && !model_.NeedsLogin()) {
            accountValue_->SetText(Widen(summary->username));
            action_->SetIcon(Icon::SignOut);
            action_->SetTitle(Tr(L"Log Out"));
            action_->SetTitleColor(TextColor::Critical);
            action_->SetDescription(Tr(L"Delete the locally saved login session, account resources, and web login data."));
            action_->SetEnabled(!model_.isLoggingOut());
        } else {
            accountValue_->SetText(Tr(L"Not logged in"));
            action_->SetIcon(Icon::SignIn);
            action_->SetTitle(Tr(L"Log In"));
            action_->SetTitleColor(TextColor::Primary);
            action_->SetDescription(Tr(L"Sign in to the server using single sign-on."));
            action_->SetEnabled(model_.IsLoginConfigurationReady() && !model_.isLoggingOut() &&
                                model_.loginState().kind != LoginState::Kind::LoadingMethods);
        }
        const auto& snapshot = model_.resourceSnapshot();
        resources_->SetVisible(snapshot.has_value() && !model_.NeedsLogin());
        if (snapshot) {
            resourcesValue_->SetText(TrFormat(L"%1$@ address ranges · %2$@ domains",
                                              {std::to_wstring(snapshot->ipResources.size()),
                                               std::to_wstring(snapshot->domainResources.size() + snapshot->dnsResources.size())}));
        }
    }

private:
    void OnAction() {
        if (model_.NeedsLogin()) {
            model_.StartWebLogin();
            return;
        }
        if (!model_.IsVpnConnectedOrConnecting()) {
            model_.Logout();
            return;
        }
        DialogOptions options;
        options.title = Tr(L"Log Out");
        options.message = Tr(L"Logging out will stop the current connection and delete the saved session and web login data.");
        options.primaryText = Tr(L"Disconnect and Log Out");
        options.primaryIsDanger = true;
        options.closeText = Tr(L"Cancel");
        ShowContentDialog(host_, options, [this](DialogResult result) {
            if (result == DialogResult::Primary) model_.Logout();
        });
    }

    TextBox* host_box_;
    TextBox* port_box_;
    SettingsCard* account_;
    TextBlock* accountValue_;
    SettingsCard* action_;
    SettingsCard* resources_;
    TextBlock* resourcesValue_;
};

// ---- Connection -----------------------------------------------------------------------

class ConnectionPage : public Page {
public:
    ConnectionPage(AppModel& model, Host* host) : Page(model, host) {
        AddSection(Tr(L"Mode"));
        auto* modeCard = AddCard(Icon::Swap, Tr(L"Connection Mode"));
        mode_ = modeCard->SetTrailing(std::make_unique<Segmented>(
            std::vector<std::wstring>{RouteModeTitle(RouteMode::Proxy), RouteModeTitle(RouteMode::Tun)}));
        mode_->SetDangerIndex(1);
        mode_->onChange = [this](int index) { model_.SetRouteMode(index == 1 ? RouteMode::Tun : RouteMode::Proxy); };
        tunNotice_ = Emplace<InfoBar>(Severity::Warning, L"",
                                      Tr(L"VPN mode routes all traffic and may cause unexpected issues. Use it only when needed."));
        helperNotice_ = Emplace<InfoBar>(Severity::Informational, L"",
                                         Tr(L"VPN mode requires the NulConnect Helper service. Install it on the Privileged Component page."));
        systemProxy_ = AddCard(Icon::Globe, Tr(L"Enable System Proxy"),
                               Tr(L"System proxy mode may conflict with other proxy software. Use it only when needed."));
        systemProxyToggle_ = systemProxy_->SetTrailing(std::make_unique<ToggleSwitch>());
        systemProxyToggle_->SetDangerous(true);
        systemProxyToggle_->onChange = [this](bool on) { model_.SetSystemProxyEnabled(on); };
        scope_ = AddCard(Icon::Network, Tr(L"System Proxy Scope"));
        scopeControl_ = scope_->SetTrailing(std::make_unique<Segmented>(
            std::vector<std::wstring>{Tr(L"All Traffic"), Tr(L"Intranet Only (PAC)")}));
        scopeControl_->onChange = [this](int index) {
            model_.SetSystemProxyMode(index == 1 ? SystemProxyMode::Pac : SystemProxyMode::All);
        };

        AddSection(Tr(L"Local Proxy"));
        portCard_ = AddCard(Icon::Network, Tr(L"Listening Port"), Tr(L"HTTP and SOCKS5 proxy on 127.0.0.1."));
        port_ = portCard_->SetTrailing(std::make_unique<TextBox>());
        port_->SetDigitsOnly(true);
        port_->SetMaxLength(5);
        port_->SetPreferredWidth(120);
        port_->onChange = [this](const std::wstring& text) { ValidatePort(text); };
        port_->onCommit = [this](const std::wstring& text) { CommitPort(text); };

        AddSection(Tr(L"Client Parameters"));
        auto* agentCard = AddCard(Icon::Document, L"User-Agent");
        agent_ = agentCard->SetTrailing(std::make_unique<TextBox>());
        agent_->SetPreferredWidth(420);
        agent_->onCommit = [this](const std::wstring& text) {
            std::string value = Narrow(text);
            model_.UpdateProfile([&](Profile& p) { p.userAgent = value; });
        };
        auto* tlsCard = AddCard(Icon::Lock, Tr(L"Allow Insecure TLS"), Tr(L"Skip certificate validation. Use only for testing."));
        tls_ = tlsCard->SetTrailing(std::make_unique<ToggleSwitch>());
        tls_->onChange = [this](bool on) { model_.UpdateProfile([on](Profile& p) { p.allowInsecureTls = on; }); };
        Refresh();
    }

    void Refresh() override {
        const Profile& profile = model_.profile();
        mode_->SetSelected(model_.EffectiveRouteMode() == RouteMode::Tun ? 1 : 0, false);
        mode_->SetEnabled(model_.CanChangeRouteMode());
        tunNotice_->SetVisible(model_.EffectiveRouteMode() == RouteMode::Tun);
        helperNotice_->SetVisible(!model_.IsHelperInstalled());
        systemProxy_->SetVisible(model_.EffectiveRouteMode() == RouteMode::Proxy);
        systemProxyToggle_->SetOn(profile.useSystemProxy);
        systemProxyToggle_->SetEnabled(model_.CanChangeSystemProxyPreference());
        systemProxy_->SetTitleColor(profile.useSystemProxy ? TextColor::Critical : TextColor::Primary);
        scope_->SetVisible(model_.EffectiveRouteMode() == RouteMode::Proxy);
        scope_->SetDescription(profile.systemProxyMode == SystemProxyMode::Pac
                                   ? Tr(L"Only intranet resources use the proxy. Other traffic keeps working even if NulConnect quits. Apps that ignore PAC files (many command-line tools) are not proxied.")
                                   : std::wstring());
        scopeControl_->SetSelected(profile.systemProxyMode == SystemProxyMode::Pac ? 1 : 0, false);
        bool busy = model_.IsProxyRunning() || model_.IsProxyBusy() || model_.IsTunnelRunning() || model_.IsTunnelBusy();
        port_->SetEnabled(!busy);
        if (!port_->HasFocus()) {
            port_->SetText(profile.localProxyPort ? std::to_wstring(profile.localProxyPort) : std::wstring());
            ValidatePort(port_->Text());
        }
        if (!agent_->HasFocus()) agent_->SetText(Widen(profile.userAgent));
        tls_->SetOn(profile.allowInsecureTls);
    }

private:
    std::optional<std::wstring> PortError(const std::wstring& text) const {
        std::wstring value = Trim(text);
        if (value.empty()) return Tr(L"Enter a local proxy port");
        if (value.find_first_not_of(L"0123456789") != std::wstring::npos) return Tr(L"Port must contain digits only");
        unsigned long port = std::wcstoul(value.c_str(), nullptr, 10);
        if (port < 1 || port > 65535) return Tr(L"Port must be between 1 and 65535");
        return std::nullopt;
    }

    void ValidatePort(const std::wstring& text) {
        auto error = PortError(text);
        port_->SetError(error.has_value());
        portCard_->SetDescription(error ? *error : Tr(L"HTTP and SOCKS5 proxy on 127.0.0.1."));
        portCard_->SetDescriptionColor(error ? TextColor::Critical : TextColor::Secondary);
    }

    void CommitPort(const std::wstring& text) {
        auto error = PortError(text);
        uint16_t port = error ? 0 : static_cast<uint16_t>(std::wcstoul(Trim(text).c_str(), nullptr, 10));
        model_.UpdateProfile([port](Profile& p) { p.localProxyPort = port; });
    }

    Segmented* mode_;
    InfoBar* tunNotice_;
    InfoBar* helperNotice_;
    SettingsCard* systemProxy_;
    ToggleSwitch* systemProxyToggle_;
    SettingsCard* scope_;
    Segmented* scopeControl_;
    SettingsCard* portCard_;
    TextBox* port_;
    TextBox* agent_;
    ToggleSwitch* tls_;
};

// ---- Statistics ------------------------------------------------------------------------

class StatisticsPage : public Page {
public:
    StatisticsPage(AppModel& model, Host* host) : Page(model, host) {
        AddSection(Tr(L"Live Traffic"));
        auto* card = Emplace<Card>(16.0f);
        auto* stack = static_cast<StackPanel*>(card->SetContent(std::make_unique<StackPanel>(Orientation::Vertical, 12.0f)));
        auto* rates = stack->Emplace<StackPanel>(Orientation::Horizontal, 16.0f);
        download_ = rates->Emplace<RateTile>(Icon::ArrowDown, Tr(L"Download"), true);
        upload_ = rates->Emplace<RateTile>(Icon::ArrowUp, Tr(L"Upload"), false);
        chart_ = stack->Emplace<TrafficChart>(160.0f);

        AddSection(Tr(L"This Connection"));
        downloaded_ = AddCard(Icon::ArrowDown, Tr(L"Downloaded"))->SetTrailing(ValueText(L""));
        uploaded_ = AddCard(Icon::ArrowUp, Tr(L"Uploaded"))->SetTrailing(ValueText(L""));
        duration_ = AddCard(Icon::Clock, Tr(L"Connection Duration"))->SetTrailing(ValueText(L""));
        Refresh();
    }

    bool WantsTraffic() const override { return true; }

    void Refresh() override {
        const auto& traffic = model_.traffic();
        download_->SetValue(FormatRate(traffic.downloadBytesPerSecond));
        upload_->SetValue(FormatRate(traffic.uploadBytesPerSecond));
        chart_->SetSamples(model_.trafficHistory());
        downloaded_->SetText(FormatBytes(traffic.counters.downloadedBytes));
        uploaded_->SetText(FormatBytes(traffic.counters.uploadedBytes));
        duration_->SetText(traffic.connectionStartedAt ? FormatDuration(traffic.connectionDuration) : L"--");
    }

private:
    RateTile* download_;
    RateTile* upload_;
    TrafficChart* chart_;
    TextBlock* downloaded_;
    TextBlock* uploaded_;
    TextBlock* duration_;
};

// ---- Privileged component -----------------------------------------------------------------

class HelperPage : public Page {
public:
    HelperPage(AppModel& model, Host* host) : Page(model, host) {
        auto* intro = Emplace<TextBlock>(Tr(L"The NulConnect Helper is a Windows service that creates the virtual network adapter, routes and DNS rules for VPN mode. Proxy mode does not need it."),
                                         TextStyle::Body, TextColor::Secondary);
        (void)intro;
        AddSection(Tr(L"Privileged Component"));
        installed_ = AddCard(Icon::Info, Tr(L"Installed Version"))->SetTrailing(ValueText(L""));
        bundled_ = AddCard(Icon::Download, Tr(L"Bundled Version"))->SetTrailing(ValueText(L""));
        status_ = AddCard(Icon::Server, Tr(L"Service Status"))->SetTrailing(ValueText(L""));
        install_ = AddCard(Icon::Download, Tr(L"Install or Update Privileged Component"),
                           Tr(L"Install the privileged component to enable VPN mode. Installation is not recommended unless needed."));
        install_->SetClickable(true);
        install_->onClick = [this] { ConfirmInstall(); };
        activity_ = Emplace<StackPanel>(Orientation::Horizontal, 12.0f);
        activity_->SetPadding(16, 8, 16, 8);
        activity_->Emplace<ProgressRing>(20.0f, 2.0f);
        activityText_ = activity_->Emplace<TextBlock>(L"", TextStyle::Body, TextColor::Secondary);
        uninstall_ = AddCard(Icon::Trash, Tr(L"Uninstall Privileged Component"),
                             Tr(L"Remove the NulConnect Helper service and its state files."));
        uninstall_->SetClickable(true);
        uninstall_->onClick = [this] { ConfirmUninstall(); };
        Refresh();
    }

    void Refresh() override {
        installed_->SetText(model_.helperVersionText());
        bundled_->SetText(model_.bundledHelperVersionText());
        status_->SetText(!model_.IsHelperInstalled() ? Tr(L"Not installed")
                                                     : (model_.IsHelperRunning() ? Tr(L"Running") : Tr(L"Stopped")));
        bool busy = model_.IsHelperActivityBusy();
        install_->SetEnabled(!busy && !(model_.IsTunnelRunning() || model_.IsTunnelBusy()));
        uninstall_->SetEnabled(!busy && model_.IsHelperInstalled() && !(model_.IsTunnelRunning() || model_.IsTunnelBusy()));
        activity_->SetVisible(busy);
        activityText_->SetText(model_.helperActivity().message);
    }

private:
    void ConfirmInstall() {
        DialogOptions options;
        options.title = Tr(L"Install Privileged Component");
        options.message = Tr(L"Installing the privileged component requires administrator access and registers a Windows service. Continue only if you need VPN mode.");
        options.primaryText = Tr(L"Continue Installation");
        options.closeText = Tr(L"Cancel");
        ShowContentDialog(host_, options, [this](DialogResult result) {
            if (result == DialogResult::Primary) model_.InstallHelper();
        });
    }

    void ConfirmUninstall() {
        DialogOptions options;
        options.title = Tr(L"Uninstall Privileged Component");
        options.message = Tr(L"This removes the privileged component, LaunchDaemon, and state files. Install it again to use system proxy or VPN mode.");
        options.primaryText = Tr(L"Uninstall");
        options.primaryIsDanger = true;
        options.closeText = Tr(L"Cancel");
        ShowContentDialog(host_, options, [this](DialogResult result) {
            if (result == DialogResult::Primary) model_.UninstallHelper();
        });
    }

    TextBlock* installed_;
    TextBlock* bundled_;
    TextBlock* status_;
    SettingsCard* install_;
    StackPanel* activity_;
    TextBlock* activityText_;
    SettingsCard* uninstall_;
};

// ---- General ---------------------------------------------------------------------------

class GeneralPage : public Page {
public:
    GeneralPage(AppModel& model, Host* host) : Page(model, host) {
        AddSection(Tr(L"Startup"));
        startup_ = AddCard(Icon::Rocket, Tr(L"Start with Windows"), Tr(L"Launch NulConnect in the notification area when you sign in."))
                       ->SetTrailing(std::make_unique<ToggleSwitch>());
        startup_->onChange = [this](bool on) { model_.UpdateSettings([on](AppSettings& s) { s.launchAtStartup = on; }); };
        tray_ = AddCard(Icon::Window, Tr(L"Keep running in the notification area"),
                        Tr(L"Closing the window keeps NulConnect and the connection running."))
                    ->SetTrailing(std::make_unique<ToggleSwitch>());
        tray_->onChange = [this](bool on) { model_.UpdateSettings([on](AppSettings& s) { s.closeToTray = on; }); };
        notify_ = AddCard(Icon::Info, Tr(L"Show notifications"), Tr(L"Notify when the connection is established, lost or restored."))
                      ->SetTrailing(std::make_unique<ToggleSwitch>());
        notify_->onChange = [this](bool on) { model_.UpdateSettings([on](AppSettings& s) { s.showNotifications = on; }); };
        autoConnect_ = AddCard(Icon::Plug, Tr(L"Connect Automatically on Launch"),
                               Tr(L"Connect in the selected mode when NulConnect starts and a saved session exists."))
                           ->SetTrailing(std::make_unique<ToggleSwitch>());
        autoConnect_->onChange = [this](bool on) { model_.UpdateSettings([on](AppSettings& s) { s.reconnectOnLaunch = on; }); };

        AddSection(Tr(L"Troubleshooting"));
        auto* logs = AddCard(Icon::Folder, Tr(L"Open Log Folder"), Tr(L"Diagnostic logs never contain passwords, tickets or session keys."));
        logs->SetClickable(true, Icon::Open);
        logs->onClick = [] {
            std::wstring folder = JoinPath(LocalDataDirectory(), L"Logs");
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        };
        auto* reset = AddCard(Icon::Refresh, Tr(L"Reset Connection Settings"),
                              Tr(L"Restore the server address and client parameters to their defaults."));
        reset->SetClickable(true);
        reset->onClick = [this] {
            DialogOptions options;
            options.title = Tr(L"Reset Connection Settings");
            options.message = Tr(L"Restore the server address and client parameters to their defaults.");
            options.primaryText = Tr(L"Reset");
            options.primaryIsDanger = true;
            options.closeText = Tr(L"Cancel");
            ShowContentDialog(host_, options, [this](DialogResult result) {
                if (result == DialogResult::Primary) model_.ResetProfileToDefaults();
            });
        };
        Refresh();
    }

    void Refresh() override {
        const AppSettings& settings = model_.settings();
        startup_->SetOn(settings.launchAtStartup);
        tray_->SetOn(settings.closeToTray);
        notify_->SetOn(settings.showNotifications);
        autoConnect_->SetOn(settings.reconnectOnLaunch);
    }

private:
    ToggleSwitch* startup_;
    ToggleSwitch* tray_;
    ToggleSwitch* notify_;
    ToggleSwitch* autoConnect_;
};

// ---- About -------------------------------------------------------------------------------

class LogoHeader : public Widget {
public:
    float Measure(float) override { return 96; }
    void Paint(Canvas& canvas) override {
        const Palette& p = Theme::Current();
        DrawBrandLogo(canvas, MakeRect(bounds_.left + 16, bounds_.top + 16, 64, 64));
        float left = bounds_.left + 96;
        canvas.Text(L"NulConnect", TextStyle::Subtitle, RectF{left, bounds_.top + 20, bounds_.right, bounds_.top + 48}, p.textPrimary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
        canvas.Text(TrFormat(L"Version %1$@", {NC_VERSION_WSTRING}), TextStyle::Body,
                    RectF{left, bounds_.top + 50, bounds_.right, bounds_.top + 72}, p.textSecondary,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR, false);
    }
};

class AboutPage : public Page {
public:
    AboutPage(AppModel& model, Host* host) : Page(model, host) {
        auto* card = Emplace<Card>(0.0f);
        card->SetContent(std::make_unique<LogoHeader>());
        AddSection(Tr(L"About NulConnect"));
        AddCard(Icon::Info, Tr(L"Version"))->SetTrailing(ValueText(NC_VERSION_WSTRING));
        AddCard(Icon::Document, Tr(L"Build"))->SetTrailing(ValueText(NC_BUILD_WSTRING));
        AddCard(Icon::Person, Tr(L"Copyright"))->SetTrailing(ValueText(L"Copyright (C) NulStudio 2014-2026"));
        auto* license = AddCard(Icon::Document, Tr(L"License"), L"GNU Affero General Public Licence v3.0");
        license->SetClickable(true, Icon::Open);
        license->onClick = [] { OpenUrl(L"https://www.gnu.org/licenses/agpl-3.0.html"); };
        auto* source = AddCard(Icon::Globe, Tr(L"Source Code"), L"github.com/jsjtsty/NulConnect");
        source->SetClickable(true, Icon::Open);
        source->onClick = [] { OpenUrl(L"https://github.com/jsjtsty/NulConnect"); };

        AddSection(Tr(L"Third-Party Components"));
        AddCard(Icon::None, L"libreatrust", L"GNU Affero General Public License v3.0");
        AddCard(Icon::None, L"nulconnect-helper", L"GNU Affero General Public License v3.0");
        AddCard(Icon::None, L"Wintun", L"Copyright (C) WireGuard LLC. Prebuilt Binaries License");
        AddCard(Icon::None, L"Microsoft Edge WebView2", L"Copyright (C) Microsoft Corporation. BSD-3-Clause (SDK)");
        AddCard(Icon::None, L"JSON for Modern C++ (nlohmann/json)", L"Copyright (C) Niels Lohmann. MIT License");
    }

    void Refresh() override {}
};

}  // namespace

std::unique_ptr<Page> CreatePage(PageId id, AppModel& model, Host* host) {
    switch (id) {
    case PageId::Home: return std::make_unique<HomePage>(model, host);
    case PageId::Account: return std::make_unique<AccountPage>(model, host);
    case PageId::Connection: return std::make_unique<ConnectionPage>(model, host);
    case PageId::Statistics: return std::make_unique<StatisticsPage>(model, host);
    case PageId::Helper: return std::make_unique<HelperPage>(model, host);
    case PageId::General: return std::make_unique<GeneralPage>(model, host);
    case PageId::About: return std::make_unique<AboutPage>(model, host);
    }
    return std::make_unique<HomePage>(model, host);
}

const std::wstring& PageTitle(PageId id) {
    switch (id) {
    case PageId::Home: return Tr(L"Home");
    case PageId::Account: return Tr(L"Account");
    case PageId::Connection: return Tr(L"Connection");
    case PageId::Statistics: return Tr(L"Statistics");
    case PageId::Helper: return Tr(L"Privileged Component");
    case PageId::General: return Tr(L"General");
    case PageId::About: return Tr(L"About");
    }
    return Tr(L"Home");
}

Icon PageIcon(PageId id) {
    switch (id) {
    case PageId::Home: return Icon::Home;
    case PageId::Account: return Icon::Person;
    case PageId::Connection: return Icon::Globe;
    case PageId::Statistics: return Icon::Chart;
    case PageId::Helper: return Icon::Lock;
    case PageId::General: return Icon::Settings;
    case PageId::About: return Icon::Info;
    }
    return Icon::Home;
}

}  // namespace nc
