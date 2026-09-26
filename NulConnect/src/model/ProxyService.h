#pragma once

#include "model/Types.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace nc {

// The in-process HTTP/SOCKS5 proxy (proxy mode), backed by libreatrust.
// Construction and Start block; run them off the UI thread.
class ProxyService {
public:
    ProxyService(const Profile& profile, const atr::SessionMaterial& session, const atr::ResourceSnapshot& resource,
                 uint16_t listenPort);
    ~ProxyService();

    ProxyEndpoint Start();
    void Stop();
    TrafficCounters Traffic() const;

    // Invoked on the UI thread when the service reports that the sign-in
    // session is no longer valid.
    std::function<void(std::wstring)> onSessionInvalidated;

private:
    void MonitorEvents();

    std::unique_ptr<atr::Client> client_;
    std::unique_ptr<atr::ProxyService> service_;
    mutable std::mutex mutex_;
    std::thread monitor_;
    std::atomic<bool> stopping_{false};
    uint16_t listenPort_;
};

}  // namespace nc
