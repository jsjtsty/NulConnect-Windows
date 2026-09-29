#pragma once

// C++ wrapper around the libreatrust C ABI. Every call may block on the
// network and must run off the UI thread. Failures throw nc::AppError with
// the matching ErrorKind. All strings are UTF-8.

#include "core/Error.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct atr_client_t;
struct atr_auth_session_t;
struct atr_proxy_service_t;

namespace nc::atr {

struct ClientConfig {
    std::string serverHost;
    uint16_t serverPort = 443;
    std::string userAgent;
    uint64_t connectTimeoutMs = 15000;
    uint64_t ioTimeoutMs = 10000;
    uint64_t nodeProbeTimeoutMs = 5000;
    bool allowInsecureTls = false;
};

struct AuthConfig {
    std::string serverHost;
    uint16_t serverPort = 443;
    std::string userAgent;
    std::string clientType;
    std::string platform;
    std::string loginDomain;
    std::optional<std::string> preferredAuthType;
    uint64_t ioTimeoutMs = 10000;
    bool allowInsecureTls = false;
};

struct Cookie {
    std::string host;
    std::string scheme;
    std::string name;
    std::string value;
};

struct SessionMaterial {
    std::string username;
    std::string sid;
    std::string deviceId;
    std::string connectionId;
    std::string signKeyHex;
    std::vector<Cookie> cookies;
};

struct AuthMethod {
    std::string loginDomain;
    std::string authType;
    std::string authName;
    std::string loginUrl;
};

enum class ChallengeKind { Captcha, SmsCode, CallbackUrl, Done };

struct AuthChallenge {
    ChallengeKind kind = ChallengeKind::Done;
    std::string image;
    std::string authId;
    std::string authUrl;
    ChallengeKind callbackKind = ChallengeKind::Done;
    SessionMaterial session;
};

struct IpResource {
    std::string ipMin;
    std::string ipMax;
    uint16_t portMin = 0;
    uint16_t portMax = 0;
    std::string protocol;
    std::string appId;
    std::string nodeGroupId;
};

struct DomainResource {
    std::string domain;
    uint16_t portMin = 0;
    uint16_t portMax = 0;
    std::string protocol;
    std::string appId;
    std::string nodeGroupId;
};

struct DnsResource {
    std::string domain;
    std::string ip;
};

struct NodeGroup {
    std::string groupId;
    std::vector<std::string> addresses;
};

struct ResourceSnapshot {
    std::string resourceBytes;
    std::optional<std::string> dnsServer;
    std::optional<std::string> majorNodeGroup;
    std::vector<IpResource> ipResources;
    std::vector<DomainResource> domainResources;
    std::vector<DnsResource> dnsResources;
    std::vector<NodeGroup> nodeGroups;
    std::vector<std::string> excludedIps;
};

struct ProxyEndpoint {
    std::string host;
    uint16_t port = 0;
};

struct ProxyStats {
    uint64_t activeConnections = 0;
    uint64_t totalConnections = 0;
    std::string lastError;
};

struct ProxyTraffic {
    uint64_t uploadBytes = 0;
    uint64_t downloadBytes = 0;
};

enum class ProxyEventKind { None, Error, SessionInvalidated };

struct ProxyEvent {
    ProxyEventKind kind = ProxyEventKind::None;
    std::string message;
};

class AuthSession {
public:
    explicit AuthSession(const AuthConfig& config);
    ~AuthSession();
    AuthSession(const AuthSession&) = delete;
    AuthSession& operator=(const AuthSession&) = delete;

    std::vector<AuthMethod> AvailableMethods();
    std::string ResolveLoginUrl(const std::string& loginUrl);
    void PrepareCallbackLogin(const std::string& deviceId);
    AuthChallenge CompleteCallback(const std::string& callbackUrl, const std::string& deviceId);
    SessionMaterial ResumeSession(const SessionMaterial& material);
    std::string FetchClientResource();

private:
    atr_auth_session_t* handle_ = nullptr;
};

class ProxyService;

class Client {
public:
    explicit Client(const ClientConfig& config);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void SetSession(const SessionMaterial& material);
    void SetResource(const std::string& resourceBytes, const std::string& serviceHost);
    ResourceSnapshot Snapshot() const;

    struct ProxyConfig {
        std::string listenHost = "127.0.0.1";
        uint16_t listenPort = 1920;
        uint64_t connectTimeoutMs = 10000;
        uint64_t idleTimeoutMs = 0;
        bool enableHttp = true;
        bool enableSocks5 = true;
        // Serves /proxy.pac?token=<pacToken> when not empty.
        std::string pacToken;
    };
    std::unique_ptr<ProxyService> StartProxy(const ProxyConfig& config) const;

private:
    atr_client_t* handle_ = nullptr;
};

class ProxyService {
public:
    explicit ProxyService(atr_proxy_service_t* handle) : handle_(handle) {}
    ~ProxyService();
    ProxyService(const ProxyService&) = delete;
    ProxyService& operator=(const ProxyService&) = delete;

    void Stop();
    bool IsRunning() const;
    ProxyEndpoint Endpoint() const;
    ProxyStats Stats() const;
    ProxyTraffic Traffic() const;
    ProxyEvent TakeEvent() const;
    // The callback runs on a library thread; it must not stop or free the
    // service. Pass an empty function to clear it; that call returns once a
    // running callback has finished.
    void SetEventCallback(std::function<void(ProxyEvent)> callback);

private:
    atr_proxy_service_t* handle_ = nullptr;
    std::unique_ptr<std::function<void(ProxyEvent)>> eventCallback_;
};

// Session material and resource snapshots are persisted as JSON.
void to_json(nlohmann::json& j, const Cookie& value);
void from_json(const nlohmann::json& j, Cookie& value);
void to_json(nlohmann::json& j, const SessionMaterial& value);
void from_json(const nlohmann::json& j, SessionMaterial& value);
void to_json(nlohmann::json& j, const ResourceSnapshot& value);
void from_json(const nlohmann::json& j, ResourceSnapshot& value);

}  // namespace nc::atr
