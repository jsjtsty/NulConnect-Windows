#include "pch.h"
#include "model/TunnelManager.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"
#include "model/HelperClient.h"

namespace nc {

namespace {

std::optional<uint32_t> ParseIpv4(const std::string& text) {
    std::string value = Trim(text);
    IN_ADDR address{};
    if (value.empty() || InetPtonA(AF_INET, value.c_str(), &address) != 1) return std::nullopt;
    return ntohl(address.S_un.S_addr);
}

std::string Ipv4String(uint32_t value) {
    return std::to_string((value >> 24) & 0xFF) + "." + std::to_string((value >> 16) & 0xFF) + "." +
           std::to_string((value >> 8) & 0xFF) + "." + std::to_string(value & 0xFF);
}

std::vector<std::string> SystemDnsServers() {
    std::vector<std::string> servers;
    ULONG size = 0;
    if (GetNetworkParams(nullptr, &size) != ERROR_BUFFER_OVERFLOW) return servers;
    std::vector<BYTE> buffer(size);
    auto* info = reinterpret_cast<FIXED_INFO*>(buffer.data());
    if (GetNetworkParams(info, &size) != NO_ERROR) return servers;
    for (IP_ADDR_STRING* entry = &info->DnsServerList; entry; entry = entry->Next) {
        std::string server = Trim(entry->IpAddress.String);
        if (!server.empty() && server != "0.0.0.0" && ParseIpv4(server)) servers.push_back(server);
    }
    return servers;
}

std::string PreferredDnsServer(const atr::ResourceSnapshot& resource) {
    if (resource.dnsServer) {
        std::string server = Trim(*resource.dnsServer);
        if (!server.empty() && server.find(':') == std::string::npos) return server;
    }
    auto system = SystemDnsServers();
    if (!system.empty()) return system.front();
    return "1.1.1.1";
}

std::string NormalizedDomain(std::string value) {
    value = ToLower(TrimChars(value, ". \r\n\t"));
    if (StartsWith(value, "*.")) value.erase(0, 2);
    return value;
}

void AppendUnique(std::vector<std::string>& list, const std::string& value) {
    if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

}  // namespace

std::vector<std::string> TunnelManager::RangeToCidrs(const std::string& first, const std::string& last) {
    auto start = ParseIpv4(first);
    auto end = ParseIpv4(last);
    std::vector<std::string> output;
    if (!start || !end || *start > *end) {
        for (const auto& value : {first, last}) {
            if (ParseIpv4(value)) AppendUnique(output, Trim(value) + "/32");
        }
        return output;
    }
    uint64_t current = *start;
    uint64_t rangeEnd = *end;
    while (current <= rangeEnd) {
        uint64_t remaining = rangeEnd - current + 1;
        uint64_t alignment = current == 0 ? (1ULL << 32) : (current & (~current + 1));
        uint64_t block = std::min<uint64_t>(alignment, 1ULL << 32);
        while (block > remaining) block >>= 1;
        int prefix = 32;
        for (uint64_t size = block; size > 1; size >>= 1) --prefix;
        output.push_back(Ipv4String(static_cast<uint32_t>(current)) + "/" + std::to_string(prefix));
        current += block;
    }
    return output;
}

TunnelManager::LaunchConfiguration TunnelManager::MakeLaunchConfiguration(const atr::ClientConfig& client,
                                                                        const atr::SessionMaterial& session,
                                                                        const atr::ResourceSnapshot& resource,
                                                                        const std::string& serverHost) {
    LaunchConfiguration configuration;
    configuration.client = client;
    configuration.session = session;
    configuration.resourceBytes = resource.resourceBytes;
    configuration.serviceHost = serverHost;
    configuration.dnsAddress = PreferredDnsServer(resource);
    // The helper adds its fake-IP range (198.19.0.0/16) itself.
    for (const auto& item : resource.ipResources) {
        for (const auto& cidr : RangeToCidrs(item.ipMin, item.ipMax)) AppendUnique(configuration.managedRouteCidrs, cidr);
    }
    for (const auto& item : resource.domainResources) AppendUnique(configuration.managedDomains, NormalizedDomain(item.domain));
    for (const auto& item : resource.dnsResources) AppendUnique(configuration.managedDomains, NormalizedDomain(item.domain));
    return configuration;
}

void TunnelManager::Start(const LaunchConfiguration& c) {
    nlohmann::json cookies = nlohmann::json::array();
    for (const auto& cookie : c.session.cookies) cookies.push_back(cookie);
    nlohmann::json config = {
        {"client",
         {{"server_host", c.client.serverHost},
          {"server_port", c.client.serverPort},
          {"user_agent", c.client.userAgent},
          {"connect_timeout_ms", c.client.connectTimeoutMs},
          {"io_timeout_ms", c.client.ioTimeoutMs},
          {"node_probe_timeout_ms", c.client.nodeProbeTimeoutMs},
          {"allow_insecure_tls", c.client.allowInsecureTls}}},
        {"session",
         {{"username", c.session.username},
          {"sid", c.session.sid},
          {"device_id", c.session.deviceId},
          {"connection_id", c.session.connectionId},
          {"sign_key_hex", c.session.signKeyHex},
          {"cookies", cookies}}},
        {"resource_bytes", Base64Encode(c.resourceBytes)},
        {"service_host", c.serviceHost},
        {"tun_name", nullptr},
        {"dns_addr", c.dnsAddress},
        {"managed_route_cidrs", c.managedRouteCidrs},
        {"managed_domains", c.managedDomains},
        {"mtu", c.mtu},
        {"setup_routes", true},
        {"exit_on_fatal_error", true}};
    Log("[Tunnel] start: server=" + c.client.serverHost + ":" + std::to_string(c.client.serverPort) + " dns=" + c.dnsAddress +
        " routes=" + std::to_string(c.managedRouteCidrs.size()) + " domains=" + std::to_string(c.managedDomains.size()));
    HelperClient::StartTun(config);

    // The helper starts asynchronously; poll until it settles. Opening the
    // tunnel can take a full connect + probe cycle on slow networks.
    uint64_t budgetMs = c.client.connectTimeoutMs + c.client.ioTimeoutMs + c.client.nodeProbeTimeoutMs + 15000;
    ULONGLONG deadline = GetTickCount64() + budgetMs;
    std::string last;
    while (GetTickCount64() < deadline) {
        auto status = RuntimeStatus();
        if (status) {
            last = status->status;
            if (last == "running") {
                Log("[Tunnel] running");
                return;
            }
            if (last == "failed") {
                throw AppError(ErrorKind::HelperFailed, TrFormat(L"VPN privileged component failed: %1$@", {status->message}));
            }
            if (last == "stopped") {
                throw AppError(ErrorKind::HelperFailed, Tr(L"VPN privileged component stopped"));
            }
        }
        Sleep(250);
    }
    throw AppError(ErrorKind::HelperFailed, TrFormat(L"VPN privileged component failed: %1$@",
                                                     {L"helper did not report running TUN state: " + Widen(last)}));
}

void TunnelManager::Stop() {
    if (!HelperClient::IsRunning()) {
        throw AppError(ErrorKind::HelperNotInstalled, Tr(L"VPN privileged component exited"));
    }
    HelperClient::StopTun();
    Log("[Tunnel] stopped");
}

std::optional<TunnelRuntimeStatus> TunnelManager::RuntimeStatus() {
    if (!HelperClient::IsRunning()) {
        return TunnelRuntimeStatus{"failed", Tr(L"VPN privileged component exited"), {}};
    }
    auto data = HelperClient::Status();
    if (!data.contains("tun") || !data["tun"].is_object()) {
        throw AppError(ErrorKind::HelperFailed, Tr(L"Could not read VPN runtime status"));
    }
    const auto& tun = data["tun"];
    TunnelRuntimeStatus status;
    status.status = tun.value("status", "");
    if (tun.contains("message") && tun["message"].is_string()) status.message = Widen(tun["message"].get<std::string>());
    status.traffic.uploadedBytes = tun.value<uint64_t>("upload_bytes", 0);
    status.traffic.downloadedBytes = tun.value<uint64_t>("download_bytes", 0);
    status.traffic.uploadedPackets = tun.value<uint64_t>("upload_packets", 0);
    status.traffic.downloadedPackets = tun.value<uint64_t>("download_packets", 0);
    return status;
}

}  // namespace nc
