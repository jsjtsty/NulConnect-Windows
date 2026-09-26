#pragma once

#include "model/Types.h"

#include <memory>
#include <optional>
#include <vector>

namespace nc {

// Sign-in state machine around libreatrust's auth session. Not thread-safe:
// every call must run on the model's auth queue, which serializes them the
// way the macOS actor did.
class AuthEngine {
public:
    std::vector<atr::AuthMethod> LoadMethods(const atr::AuthConfig& config);
    WebLoginSession ResolveWebLoginSession(const atr::AuthMethod& method, const std::string& deviceId);
    atr::AuthChallenge CompleteWebLogin(const std::wstring& callbackUrl);
    std::string FetchClientResource();
    atr::SessionMaterial ResumeSession(const atr::SessionMaterial& material, const atr::AuthConfig& config);
    void Reset();

private:
    std::unique_ptr<atr::AuthSession> session_;
    std::optional<atr::AuthConfig> config_;
    std::optional<std::string> callbackDeviceId_;
    std::optional<CapturePolicy> callbackPolicy_;
    uint64_t nextSessionId_ = 1;
};

}  // namespace nc
