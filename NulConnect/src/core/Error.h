#pragma once

#include <stdexcept>
#include <string>

namespace nc {

enum class ErrorKind {
    Generic,
    // libreatrust error codes
    InvalidArgument,
    ParseFailed,
    NetworkFailed,
    Unauthorized,
    ChallengeRequired,
    InvalidState,
    CryptoFailed,
    NotFound,
    Unsupported,
    Internal,
    // application errors
    MissingSession,
    SessionExpired,
    Cancelled,
    HelperNotInstalled,
    HelperFailed,
    LoginFailed,
};

struct ErrorInfo {
    ErrorKind kind = ErrorKind::Generic;
    std::wstring message;
};

// Exception type for background work. The message is UTF-8.
class AppError : public std::runtime_error {
public:
    AppError(ErrorKind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    AppError(ErrorKind kind, const std::wstring& message);
    explicit AppError(const std::wstring& message) : AppError(ErrorKind::Generic, message) {}

    ErrorKind kind() const { return kind_; }
    std::wstring Message() const;

private:
    ErrorKind kind_;
};

// Converts the in-flight exception into an ErrorInfo. Call inside `catch`.
ErrorInfo CurrentError();

}  // namespace nc
