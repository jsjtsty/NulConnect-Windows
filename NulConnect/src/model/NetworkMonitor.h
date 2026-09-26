#pragma once

#include <functional>
#include <optional>
#include <string>

namespace nc {

// Watches IP interface/address changes. Callbacks arrive on the UI thread.
class NetworkMonitor {
public:
    struct Snapshot {
        bool isAvailable = false;
        // Identifies the current default path (interface, gateway, source
        // address); changes when the machine moves to another network.
        std::optional<std::string> fingerprint;
    };

    NetworkMonitor() = default;
    ~NetworkMonitor();
    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

    void Start(std::function<void()> onChange);
    void Stop();
    static Snapshot Current();

private:
    static void CALLBACK InterfaceChanged(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type);
    static void CALLBACK AddressChanged(PVOID context, PMIB_UNICASTIPADDRESS_ROW row, MIB_NOTIFICATION_TYPE type);
    void Signal();

    std::function<void()> onChange_;
    HANDLE interfaceHandle_ = nullptr;
    HANDLE addressHandle_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace nc
