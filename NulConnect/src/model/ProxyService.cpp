#include "pch.h"
#include "model/ProxyService.h"
#include "core/Dispatcher.h"
#include "core/Log.h"
#include "core/Str.h"

namespace nc {

ProxyService::ProxyService(const Profile& profile, const atr::SessionMaterial& session,
                           const atr::ResourceSnapshot& resource, uint16_t listenPort)
    : listenPort_(listenPort) {
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

ProxyEndpoint ProxyService::Start() {
    std::lock_guard lock(mutex_);
    if (service_) {
        auto endpoint = service_->Endpoint();
        return {endpoint.host, endpoint.port};
    }
    Log("[Proxy] start listen=127.0.0.1:" + std::to_string(listenPort_));
    atr::Client::ProxyConfig config;
    config.listenHost = "127.0.0.1";
    config.listenPort = listenPort_;
    service_ = client_->StartProxy(config);
    auto endpoint = service_->Endpoint();
    Log("[Proxy] ready endpoint=" + endpoint.host + ":" + std::to_string(endpoint.port));
    stopping_ = false;
    monitor_ = std::thread([this] { MonitorEvents(); });
    return {endpoint.host, endpoint.port};
}

void ProxyService::Stop() {
    stopping_ = true;
    if (monitor_.joinable()) monitor_.join();
    std::lock_guard lock(mutex_);
    if (service_) {
        Log("[Proxy] stop");
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

void ProxyService::MonitorEvents() {
    std::string reported;
    int polls = 0;
    while (!stopping_) {
        for (int i = 0; i < 10 && !stopping_; ++i) Sleep(100);
        if (stopping_) break;
        ++polls;
        try {
            atr::ProxyEvent event;
            {
                std::lock_guard lock(mutex_);
                if (!service_) break;
                if (polls % 2 == 0) {
                    auto stats = service_->Stats();
                    std::string snapshot = "active=" + std::to_string(stats.activeConnections) +
                                           " total=" + std::to_string(stats.totalConnections) + " lastError=" + stats.lastError;
                    if (snapshot != reported || polls % 60 == 0) {
                        reported = snapshot;
                        Log("[Proxy] stats: " + snapshot);
                    }
                }
                event = service_->TakeEvent();
            }
            if (event.kind == atr::ProxyEventKind::SessionInvalidated) {
                Log("[Proxy] event: session invalidated: " + event.message);
                auto handler = onSessionInvalidated;
                std::wstring message = Widen(event.message);
                Dispatcher::Post([handler, message] {
                    if (handler) handler(message);
                });
            } else if (event.kind == atr::ProxyEventKind::Error) {
                Log("[Proxy] event: error: " + event.message);
            }
        } catch (const std::exception& error) {
            Log(std::string("[Proxy] event monitor failed: ") + error.what());
        }
    }
}

}  // namespace nc
