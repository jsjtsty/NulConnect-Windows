#pragma once

#include "model/Types.h"

#include <optional>
#include <string>
#include <vector>

namespace nc {

struct TunnelRuntimeStatus {
    std::string status;  // starting, running, stopping, stopped, failed
    std::wstring message;
    TrafficCounters traffic;
};

// VPN mode: builds the helper launch configuration and drives the helper.
// All methods block.
class TunnelManager {
public:
    struct LaunchConfiguration {
        atr::ClientConfig client;
        atr::SessionMaterial session;
        std::string resourceBytes;
        std::string serviceHost;
        std::string dnsAddress;
        std::vector<std::string> managedRouteCidrs;
        std::vector<std::string> managedDomains;
        uint16_t mtu = 1400;
    };

    static LaunchConfiguration MakeLaunchConfiguration(const atr::ClientConfig& client, const atr::SessionMaterial& session,
                                                       const atr::ResourceSnapshot& resource, const std::string& serverHost);
    static void Start(const LaunchConfiguration& configuration);
    static void Stop();
    static std::optional<TunnelRuntimeStatus> RuntimeStatus();

    // Exposed for tests: splits an inclusive IPv4 range into CIDR blocks.
    static std::vector<std::string> RangeToCidrs(const std::string& first, const std::string& last);
};

}  // namespace nc
