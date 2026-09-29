#pragma once

#include <string>
#include <string_view>

namespace nc {

// Diagnostics log at %LOCALAPPDATA%\NulConnect\Logs\NulConnect.log.
// Never pass secrets (sid, cookies, sign keys, callback tickets) here.
void LogInit();
void Log(std::string_view message);
void Log(std::wstring_view message);
std::wstring LogFilePath();

// Turns libreatrust's verbose log on or off. The library writes to the same
// NulConnect.log as this log. Call only after reatrust.dll has been loaded.
void SetDiagnosticLogging(bool enabled);

// Strips the query and fragment from a URL. SSO callbacks carry one-time
// tickets in the query string.
std::string LoggableUrl(std::string_view url);

}  // namespace nc

#define NC_LOG(...) ::nc::Log(__VA_ARGS__)
