#include "pch.h"
#include "model/AuthEngine.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Str.h"

namespace nc {

std::vector<atr::AuthMethod> AuthEngine::LoadMethods(const atr::AuthConfig& config) {
    auto session = std::make_unique<atr::AuthSession>(config);
    auto methods = session->AvailableMethods();
    session_ = std::move(session);
    config_ = config;
    return methods;
}

WebLoginSession AuthEngine::ResolveWebLoginSession(const atr::AuthMethod& method, const std::string& deviceId) {
    if (!session_ || !config_) {
        throw AppError(ErrorKind::LoginFailed, Tr(L"Sign-in session has not been initialized"));
    }
    session_->PrepareCallbackLogin(deviceId);
    std::string startUrl = session_->ResolveLoginUrl(method.loginUrl);
    auto parts = ParseUrl(Widen(startUrl));
    auto policy = CapturePolicy::Make(method.authType, config_->serverHost, method.loginUrl,
                                      parts ? Narrow(parts->host) : std::string());
    if (!policy) {
        throw AppError(ErrorKind::LoginFailed, TrFormat(L"Unsupported WebView sign-in type: %1$@", {Widen(method.authType)}));
    }
    callbackDeviceId_ = deviceId;
    callbackPolicy_ = policy;
    WebLoginSession login{nextSessionId_++, method, deviceId, {}, {}, Widen(startUrl), *policy};
    login.title = Widen(method.authName.empty() ? method.authType : method.authName);
    login.subtitle = Widen(method.loginDomain) + L" \x00B7 " + Widen(method.authType);
    return login;
}

atr::AuthChallenge AuthEngine::CompleteWebLogin(const std::wstring& callbackUrl) {
    if (!session_ || !config_ || !callbackDeviceId_ || !callbackPolicy_) {
        throw AppError(ErrorKind::LoginFailed, Tr(L"Sign-in session has not been initialized"));
    }
    std::string validated = callbackPolicy_->Validate(callbackUrl);
    Log("[Login] completing callback " + LoggableUrl(validated));
    return session_->CompleteCallback(validated, *callbackDeviceId_);
}

std::string AuthEngine::FetchClientResource() {
    if (!session_) {
        throw AppError(ErrorKind::LoginFailed, Tr(L"Sign-in session has not been initialized"));
    }
    return session_->FetchClientResource();
}

atr::SessionMaterial AuthEngine::ResumeSession(const atr::SessionMaterial& material, const atr::AuthConfig& config) {
    auto session = std::make_unique<atr::AuthSession>(config);
    auto refreshed = session->ResumeSession(material);
    session_ = std::move(session);
    config_ = config;
    callbackDeviceId_.reset();
    callbackPolicy_.reset();
    return refreshed;
}

void AuthEngine::Reset() {
    session_.reset();
    config_.reset();
    callbackDeviceId_.reset();
    callbackPolicy_.reset();
}

}  // namespace nc
