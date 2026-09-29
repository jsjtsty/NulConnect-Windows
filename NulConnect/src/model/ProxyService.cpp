#include "pch.h"
#include "model/ProxyService.h"
#include "core/Dispatcher.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

namespace nc {

ProxyService::ProxyService(const Profile& profile, const atr::SessionMaterial& session,
                           const atr::ResourceSnapshot& resource, uint16_t listenPort)
    : pacToken_(profile.pacToken), listenPort_(listenPort) {
    atr::ClientConfig config;
    config.serverHost = profile.serverHost;
    config.serverPort = profile.serverPort;
    config.userAgent = profile.userAgent;
    config.connectTimeoutMs = profile.connectTimeoutMillis;
    config.ioTimeoutMs = profile.ioTimeoutMillis;
    config.nodeProbeTimeoutMs = profile.nodeProbeTimeoutMillis;
    config.allowInsecureTls = profile.allowInsecureTls;
    client_ = std::make_unique<atr::Client>(config);
    client_->SetSession(session);
    client_->SetResource(resource.resourceBytes, profile.serverHost);
}

ProxyService::~ProxyService() {
    Stop();
}

ProxyEndpoint ProxyService::MakeEndpoint(const atr::ProxyEndpoint& endpoint) const {
    ProxyEndpoint result{endpoint.host, endpoint.port, {}};
    if (!pacToken_.empty()) {
        result.pacUrl = "http://" + endpoint.host + ":" + std::to_string(endpoint.port) + "/proxy.pac?token=" + pacToken_;
    }
    return result;
}

// Runs on a library thread; it only logs and hands off to the UI thread.
void ProxyService::HandleEvent(atr::ProxyEvent event) {
    if (event.kind == atr::ProxyEventKind::SessionInvalidated) {
        Log("[Proxy] event: session invalidated: " + event.message);
        std::wstring message = Widen(event.message);
        auto handler = onSessionInvalidated;
        Dispatcher::Post([handler, message] {
            if (handler) handler(message);
        });
    } else if (event.kind == atr::ProxyEventKind::Error) {
        Log("[Proxy] event: error: " + event.message);
    }
}

ProxyEndpoint ProxyService::Start() {
    std::lock_guard lock(mutex_);
    if (service_) {
        return MakeEndpoint(service_->Endpoint());
    }
    Log("[Proxy] start listen=127.0.0.1:" + std::to_string(listenPort_));
    atr::Client::ProxyConfig config;
    config.listenHost = "127.0.0.1";
    config.listenPort = listenPort_;
    config.pacToken = pacToken_;
    service_ = client_->StartProxy(config);
    service_->SetEventCallback([this](atr::ProxyEvent event) { HandleEvent(std::move(event)); });
    ProxyEndpoint endpoint = MakeEndpoint(service_->Endpoint());
    Log("[Proxy] ready endpoint=" + endpoint.host + ":" + std::to_string(endpoint.port));
    return endpoint;
}

void ProxyService::Stop() {
    std::lock_guard lock(mutex_);
    if (service_) {
        Log("[Proxy] stop");
        service_->SetEventCallback(nullptr);
        service_->Stop();
        service_.reset();
    }
}

TrafficCounters ProxyService::Traffic() const {
    std::lock_guard lock(mutex_);
    if (!service_) return {};
    auto traffic = service_->Traffic();
    return {traffic.uploadBytes, traffic.downloadBytes, 0, 0};
}

}  // namespace nc
