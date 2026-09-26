#include "pch.h"
#include "model/NetworkMonitor.h"
#include "core/Dispatcher.h"

#include <netioapi.h>

namespace nc {

NetworkMonitor::~NetworkMonitor() {
    Stop();
}

void NetworkMonitor::Start(std::function<void()> onChange) {
    onChange_ = std::move(onChange);
    alive_->store(true);
    NotifyIpInterfaceChange(AF_UNSPEC, InterfaceChanged, this, FALSE, &interfaceHandle_);
    NotifyUnicastIpAddressChange(AF_INET, AddressChanged, this, FALSE, &addressHandle_);
}

void NetworkMonitor::Stop() {
    alive_->store(false);
    // CancelMibChangeNotify2 waits for in-flight callbacks to finish.
    if (interfaceHandle_) {
        CancelMibChangeNotify2(interfaceHandle_);
        interfaceHandle_ = nullptr;
    }
    if (addressHandle_) {
        CancelMibChangeNotify2(addressHandle_);
        addressHandle_ = nullptr;
    }
}

void NetworkMonitor::Signal() {
    auto alive = alive_;
    Dispatcher::Post([this, alive] {
        if (alive->load() && onChange_) onChange_();
    });
}

void CALLBACK NetworkMonitor::InterfaceChanged(PVOID context, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
    static_cast<NetworkMonitor*>(context)->Signal();
}

void CALLBACK NetworkMonitor::AddressChanged(PVOID context, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) {
    static_cast<NetworkMonitor*>(context)->Signal();
}

NetworkMonitor::Snapshot NetworkMonitor::Current() {
    Snapshot snapshot;
    SOCKADDR_INET destination{};
    destination.Ipv4.sin_family = AF_INET;
    InetPtonW(AF_INET, L"8.8.8.8", &destination.Ipv4.sin_addr);
    MIB_IPFORWARD_ROW2 route{};
    SOCKADDR_INET source{};
    if (GetBestRoute2(nullptr, 0, nullptr, &destination, 0, &route, &source) != NO_ERROR) {
        return snapshot;
    }
    snapshot.isAvailable = true;
    char gateway[INET_ADDRSTRLEN] = {};
    char local[INET_ADDRSTRLEN] = {};
    InetNtopA(AF_INET, &route.NextHop.Ipv4.sin_addr, gateway, sizeof(gateway));
    InetNtopA(AF_INET, &source.Ipv4.sin_addr, local, sizeof(local));
    snapshot.fingerprint = std::to_string(route.InterfaceLuid.Value) + "|" + gateway + "|" + local;
    return snapshot;
}

}  // namespace nc
