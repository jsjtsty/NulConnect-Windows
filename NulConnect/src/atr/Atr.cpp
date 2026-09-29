#include "pch.h"
#include "atr/Atr.h"

#include "core/Platform.h"
#include "libreatrust.h"

namespace nc::atr {

namespace {

ErrorKind KindFor(int code) {
    switch (code) {
    case ATR_INVALID_ARGUMENT: return ErrorKind::InvalidArgument;
    case ATR_PARSE_FAILED: return ErrorKind::ParseFailed;
    case ATR_NETWORK_FAILED: return ErrorKind::NetworkFailed;
    case ATR_UNAUTHORIZED: return ErrorKind::Unauthorized;
    case ATR_CHALLENGE_REQUIRED: return ErrorKind::ChallengeRequired;
    case ATR_INVALID_STATE: return ErrorKind::InvalidState;
    case ATR_CRYPTO_FAILED: return ErrorKind::CryptoFailed;
    case ATR_NOT_FOUND: return ErrorKind::NotFound;
    case ATR_UNSUPPORTED: return ErrorKind::Unsupported;
    default: return ErrorKind::Internal;
    }
}

void Check(int code, const char* operation) {
    if (code == ATR_OK) return;
    const char* message = atr_last_error_message();
    std::string text = message && *message ? message : std::string(operation) + " failed (" + std::to_string(code) + ")";
    throw AppError(KindFor(code), text);
}

std::string Take(char* value) {
    if (!value) return {};
    std::string result(value);
    atr_string_free(value);
    return result;
}

std::string Copy(const char* value) {
    return value ? std::string(value) : std::string();
}

std::vector<std::string> CopyList(const atr_string_list_t& list) {
    std::vector<std::string> result;
    result.reserve(list.len);
    for (size_t i = 0; i < list.len; ++i) {
        result.push_back(Copy(list.items[i]));
    }
    return result;
}

SessionMaterial CopySession(const atr_session_material_t& raw) {
    SessionMaterial material;
    material.username = Copy(raw.username);
    material.sid = Copy(raw.sid);
    material.deviceId = Copy(raw.device_id);
    material.connectionId = Copy(raw.connection_id);
    material.signKeyHex = Copy(raw.sign_key_hex);
    for (size_t i = 0; i < raw.cookies.len; ++i) {
        const auto& cookie = raw.cookies.items[i];
        material.cookies.push_back({Copy(cookie.host), Copy(cookie.scheme), Copy(cookie.name), Copy(cookie.value)});
    }
    return material;
}

// Keeps the C views of a SessionMaterial alive for the duration of a call.
class SessionInput {
public:
    explicit SessionInput(const SessionMaterial& material) {
        cookies_.reserve(material.cookies.size());
        for (const auto& cookie : material.cookies) {
            cookies_.push_back({cookie.host.c_str(), cookie.scheme.c_str(), cookie.name.c_str(), cookie.value.c_str()});
        }
        input_.username = material.username.c_str();
        input_.sid = material.sid.c_str();
        input_.device_id = material.deviceId.c_str();
        input_.connection_id = material.connectionId.c_str();
        input_.sign_key_hex = material.signKeyHex.c_str();
        input_.cookies.items = cookies_.empty() ? nullptr : cookies_.data();
        input_.cookies.len = cookies_.size();
    }
    const atr_session_material_input_t* get() const { return &input_; }

private:
    std::vector<atr_cookie_input_t> cookies_;
    atr_session_material_input_t input_{};
};

AuthChallenge TakeChallenge(atr_auth_challenge_t& raw) {
    auto kind = [](atr_auth_challenge_kind_t value) {
        switch (value) {
        case ATR_AUTH_CHALLENGE_CAPTCHA: return ChallengeKind::Captcha;
        case ATR_AUTH_CHALLENGE_SMS_CODE: return ChallengeKind::SmsCode;
        case ATR_AUTH_CHALLENGE_CALLBACK_URL: return ChallengeKind::CallbackUrl;
        default: return ChallengeKind::Done;
        }
    };
    AuthChallenge challenge;
    challenge.kind = kind(raw.kind);
    challenge.callbackKind = kind(raw.callback_kind);
    if (raw.image.data && raw.image.len) {
        challenge.image.assign(reinterpret_cast<const char*>(raw.image.data), raw.image.len);
    }
    challenge.authId = Copy(raw.auth_id);
    challenge.authUrl = Copy(raw.auth_url);
    if (challenge.kind == ChallengeKind::Done) {
        challenge.session = CopySession(raw.session);
    }
    atr_auth_challenge_free(&raw);
    return challenge;
}

}  // namespace

AuthSession::AuthSession(const AuthConfig& config) {
    std::string preferred = config.preferredAuthType.value_or("");
    atr_auth_config_t raw{};
    raw.server_host = config.serverHost.c_str();
    raw.server_port = config.serverPort;
    raw.user_agent = config.userAgent.c_str();
    raw.client_type = config.clientType.c_str();
    raw.platform = config.platform.c_str();
    raw.login_domain = config.loginDomain.c_str();
    raw.preferred_auth_type = config.preferredAuthType ? preferred.c_str() : nullptr;
    raw.io_timeout_ms = config.ioTimeoutMs;
    raw.allow_insecure_tls = config.allowInsecureTls;
    Check(atr_auth_session_new(&raw, &handle_), "atr_auth_session_new");
}

AuthSession::~AuthSession() {
    if (handle_) atr_auth_session_free(handle_);
}

std::vector<AuthMethod> AuthSession::AvailableMethods() {
    atr_auth_method_list_t list{};
    Check(atr_auth_session_available_methods(handle_, &list), "available_methods");
    std::vector<AuthMethod> methods;
    for (size_t i = 0; i < list.len; ++i) {
        const auto& item = list.items[i];
        methods.push_back({Copy(item.login_domain), Copy(item.auth_type), Copy(item.auth_name), Copy(item.login_url)});
    }
    atr_auth_method_list_free(&list);
    return methods;
}

std::string AuthSession::ResolveLoginUrl(const std::string& loginUrl) {
    char* out = nullptr;
    Check(atr_auth_session_resolve_login_url(handle_, loginUrl.c_str(), &out), "resolve_login_url");
    return Take(out);
}

void AuthSession::PrepareCallbackLogin(const std::string& deviceId) {
    Check(atr_auth_session_prepare_callback_login(handle_, deviceId.c_str()), "prepare_callback_login");
}

AuthChallenge AuthSession::CompleteCallback(const std::string& callbackUrl, const std::string& deviceId) {
    atr_callback_target_t target{callbackUrl.c_str()};
    atr_auth_challenge_t raw{};
    Check(atr_auth_session_complete_callback_with_device(handle_, &target, deviceId.c_str(), &raw), "complete_callback");
    return TakeChallenge(raw);
}

SessionMaterial AuthSession::ResumeSession(const SessionMaterial& material) {
    SessionInput input(material);
    atr_session_material_t raw{};
    Check(atr_auth_session_resume_session(handle_, input.get(), &raw), "resume_session");
    SessionMaterial result = CopySession(raw);
    atr_session_material_free(&raw);
    return result;
}

std::string AuthSession::FetchClientResource() {
    atr_blob_t blob{};
    Check(atr_auth_session_fetch_client_resource(handle_, &blob), "fetch_client_resource");
    std::string bytes(reinterpret_cast<const char*>(blob.data), blob.len);
    atr_blob_free(&blob);
    return bytes;
}

Client::Client(const ClientConfig& config) {
    atr_client_config_t raw{};
    raw.server_host = config.serverHost.c_str();
    raw.server_port = config.serverPort;
    raw.user_agent = config.userAgent.c_str();
    raw.connect_timeout_ms = config.connectTimeoutMs;
    raw.io_timeout_ms = config.ioTimeoutMs;
    raw.node_probe_timeout_ms = config.nodeProbeTimeoutMs;
    raw.allow_insecure_tls = config.allowInsecureTls;
    raw.bind_interface = nullptr;
    raw.auto_detect_interface = true;
    Check(atr_client_new(&raw, &handle_), "atr_client_new");
}

Client::~Client() {
    if (handle_) atr_client_free(handle_);
}

void Client::SetSession(const SessionMaterial& material) {
    SessionInput input(material);
    Check(atr_client_set_session(handle_, input.get()), "set_session");
}

void Client::SetResource(const std::string& resourceBytes, const std::string& serviceHost) {
    Check(atr_client_set_resource(handle_, reinterpret_cast<const uint8_t*>(resourceBytes.data()), resourceBytes.size(),
                                  serviceHost.c_str()),
          "set_resource");
}

ResourceSnapshot Client::Snapshot() const {
    atr_resource_snapshot_t raw{};
    Check(atr_client_get_resource_snapshot(handle_, &raw), "get_resource_snapshot");
    ResourceSnapshot snapshot;
    snapshot.resourceBytes.assign(reinterpret_cast<const char*>(raw.resource_bytes.data), raw.resource_bytes.len);
    if (raw.dns_server) snapshot.dnsServer = Copy(raw.dns_server);
    if (raw.major_node_group) snapshot.majorNodeGroup = Copy(raw.major_node_group);
    for (size_t i = 0; i < raw.ip_resources.len; ++i) {
        const auto& item = raw.ip_resources.items[i];
        snapshot.ipResources.push_back({Copy(item.ip_min), Copy(item.ip_max), item.port_min, item.port_max,
                                        Copy(item.protocol), Copy(item.app_id), Copy(item.node_group_id)});
    }
    for (size_t i = 0; i < raw.domain_resources.len; ++i) {
        const auto& item = raw.domain_resources.items[i];
        snapshot.domainResources.push_back({Copy(item.domain), item.port_min, item.port_max, Copy(item.protocol),
                                            Copy(item.app_id), Copy(item.node_group_id)});
    }
    for (size_t i = 0; i < raw.dns_resources.len; ++i) {
        snapshot.dnsResources.push_back({Copy(raw.dns_resources.items[i].domain), Copy(raw.dns_resources.items[i].ip)});
    }
    for (size_t i = 0; i < raw.node_groups.len; ++i) {
        snapshot.nodeGroups.push_back({Copy(raw.node_groups.items[i].group_id), CopyList(raw.node_groups.items[i].addresses)});
    }
    snapshot.excludedIps = CopyList(raw.excluded_ips);
    atr_resource_snapshot_free(&raw);
    return snapshot;
}

std::unique_ptr<ProxyService> Client::StartProxy(const ProxyConfig& config) const {
    atr_proxy_service_config_t raw{};
    raw.listen_host = config.listenHost.c_str();
    raw.listen_port = config.listenPort;
    raw.connect_timeout_ms = config.connectTimeoutMs;
    raw.idle_timeout_ms = config.idleTimeoutMs;
    raw.enable_http = config.enableHttp;
    raw.enable_socks5 = config.enableSocks5;
    raw.pac_token = config.pacToken.empty() ? nullptr : config.pacToken.c_str();
    atr_proxy_service_t* service = nullptr;
    Check(atr_client_start_proxy_service(handle_, &raw, &service), "start_proxy_service");
    return std::make_unique<ProxyService>(service);
}

ProxyService::~ProxyService() {
    if (handle_) {
        atr_proxy_service_set_event_callback(handle_, nullptr, nullptr);
        atr_proxy_service_stop(handle_);
        atr_proxy_service_free(handle_);
    }
}

void ProxyService::Stop() {
    if (handle_) atr_proxy_service_stop(handle_);
}

bool ProxyService::IsRunning() const {
    atr_proxy_service_status_t status = ATR_PROXY_SERVICE_STOPPED;
    Check(atr_proxy_service_status(handle_, &status), "proxy_service_status");
    return status == ATR_PROXY_SERVICE_RUNNING;
}

ProxyEndpoint ProxyService::Endpoint() const {
    atr_proxy_service_endpoint_t raw{};
    Check(atr_proxy_service_get_endpoint(handle_, &raw), "proxy_service_endpoint");
    ProxyEndpoint endpoint{Copy(raw.host), raw.port};
    atr_proxy_service_endpoint_free(&raw);
    return endpoint;
}

ProxyStats ProxyService::Stats() const {
    atr_proxy_service_stats_t raw{};
    Check(atr_proxy_service_get_stats(handle_, &raw), "proxy_service_stats");
    ProxyStats stats{raw.active_connections, raw.total_connections, Copy(raw.last_error)};
    atr_proxy_service_stats_free(&raw);
    return stats;
}

ProxyTraffic ProxyService::Traffic() const {
    atr_proxy_service_traffic_stats_t raw{};
    Check(atr_proxy_service_get_traffic_stats(handle_, &raw), "proxy_service_traffic");
    return {raw.managed_upload_bytes, raw.managed_download_bytes};
}

namespace {

ProxyEvent MakeEvent(atr_proxy_service_event_kind_t kind, std::string message) {
    ProxyEvent event;
    event.message = std::move(message);
    switch (kind) {
    case ATR_PROXY_SERVICE_EVENT_ERROR: event.kind = ProxyEventKind::Error; break;
    case ATR_PROXY_SERVICE_EVENT_SESSION_INVALIDATED: event.kind = ProxyEventKind::SessionInvalidated; break;
    default: event.kind = ProxyEventKind::None; break;
    }
    return event;
}

void EventTrampoline(atr_proxy_service_event_kind_t kind, const char* message, void* userData) {
    auto* callback = static_cast<std::function<void(ProxyEvent)>*>(userData);
    try {
        (*callback)(MakeEvent(kind, message ? message : ""));
    } catch (...) {
    }
}

}  // namespace

ProxyEvent ProxyService::TakeEvent() const {
    atr_proxy_service_event_kind_t kind = ATR_PROXY_SERVICE_EVENT_NONE;
    char* message = nullptr;
    Check(atr_proxy_service_take_event(handle_, &kind, &message), "proxy_service_take_event");
    return MakeEvent(kind, Take(message));
}

void ProxyService::SetEventCallback(std::function<void(ProxyEvent)> callback) {
    if (!callback) {
        Check(atr_proxy_service_set_event_callback(handle_, nullptr, nullptr), "proxy_service_set_event_callback");
        eventCallback_.reset();
        return;
    }
    auto next = std::make_unique<std::function<void(ProxyEvent)>>(std::move(callback));
    Check(atr_proxy_service_set_event_callback(handle_, &EventTrampoline, next.get()), "proxy_service_set_event_callback");
    eventCallback_ = std::move(next);
}

// ---- JSON --------------------------------------------------------------------

void to_json(nlohmann::json& j, const Cookie& value) {
    j = {{"host", value.host}, {"scheme", value.scheme}, {"name", value.name}, {"value", value.value}};
}

void from_json(const nlohmann::json& j, Cookie& value) {
    value.host = j.value("host", "");
    value.scheme = j.value("scheme", "");
    value.name = j.value("name", "");
    value.value = j.value("value", "");
}

void to_json(nlohmann::json& j, const SessionMaterial& value) {
    j = {{"username", value.username},     {"sid", value.sid},
         {"deviceID", value.deviceId},     {"connectionID", value.connectionId},
         {"signKeyHex", value.signKeyHex}, {"cookies", value.cookies}};
}

void from_json(const nlohmann::json& j, SessionMaterial& value) {
    value.username = j.value("username", "");
    value.sid = j.value("sid", "");
    value.deviceId = j.value("deviceID", "");
    value.connectionId = j.value("connectionID", "");
    value.signKeyHex = j.value("signKeyHex", "");
    value.cookies = j.value("cookies", std::vector<Cookie>{});
}

void to_json(nlohmann::json& j, const ResourceSnapshot& value) {
    nlohmann::json ip = nlohmann::json::array();
    for (const auto& item : value.ipResources) {
        ip.push_back({{"ipMin", item.ipMin}, {"ipMax", item.ipMax}, {"portMin", item.portMin}, {"portMax", item.portMax},
                      {"protocol", item.protocol}, {"appID", item.appId}, {"nodeGroupID", item.nodeGroupId}});
    }
    nlohmann::json domain = nlohmann::json::array();
    for (const auto& item : value.domainResources) {
        domain.push_back({{"domain", item.domain}, {"portMin", item.portMin}, {"portMax", item.portMax},
                          {"protocol", item.protocol}, {"appID", item.appId}, {"nodeGroupID", item.nodeGroupId}});
    }
    nlohmann::json dns = nlohmann::json::array();
    for (const auto& item : value.dnsResources) {
        dns.push_back({{"domain", item.domain}, {"ip", item.ip}});
    }
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& item : value.nodeGroups) {
        nodes.push_back({{"groupID", item.groupId}, {"addresses", item.addresses}});
    }
    j = {{"resourceBytes", Base64Encode(value.resourceBytes)},
         {"ipResources", ip},
         {"domainResources", domain},
         {"dnsResources", dns},
         {"nodeGroups", nodes},
         {"excludedIPs", value.excludedIps}};
    if (value.dnsServer) j["dnsServer"] = *value.dnsServer;
    if (value.majorNodeGroup) j["majorNodeGroup"] = *value.majorNodeGroup;
}

void from_json(const nlohmann::json& j, ResourceSnapshot& value) {
    value.resourceBytes = Base64Decode(j.value("resourceBytes", "")).value_or("");
    if (j.contains("dnsServer") && j["dnsServer"].is_string()) value.dnsServer = j["dnsServer"].get<std::string>();
    if (j.contains("majorNodeGroup") && j["majorNodeGroup"].is_string()) value.majorNodeGroup = j["majorNodeGroup"].get<std::string>();
    for (const auto& item : j.value("ipResources", nlohmann::json::array())) {
        value.ipResources.push_back({item.value("ipMin", ""), item.value("ipMax", ""), item.value<uint16_t>("portMin", 0),
                                     item.value<uint16_t>("portMax", 0), item.value("protocol", ""), item.value("appID", ""),
                                     item.value("nodeGroupID", "")});
    }
    for (const auto& item : j.value("domainResources", nlohmann::json::array())) {
        value.domainResources.push_back({item.value("domain", ""), item.value<uint16_t>("portMin", 0),
                                         item.value<uint16_t>("portMax", 0), item.value("protocol", ""),
                                         item.value("appID", ""), item.value("nodeGroupID", "")});
    }
    for (const auto& item : j.value("dnsResources", nlohmann::json::array())) {
        value.dnsResources.push_back({item.value("domain", ""), item.value("ip", "")});
    }
    for (const auto& item : j.value("nodeGroups", nlohmann::json::array())) {
        value.nodeGroups.push_back({item.value("groupID", ""), item.value("addresses", std::vector<std::string>{})});
    }
    value.excludedIps = j.value("excludedIPs", std::vector<std::string>{});
}

}  // namespace nc::atr
