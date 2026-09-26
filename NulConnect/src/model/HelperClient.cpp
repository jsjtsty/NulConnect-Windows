#include "pch.h"
#include "model/HelperClient.h"
#include "core/Error.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/Str.h"

namespace nc {

namespace {

constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\NulConnectHelper";
constexpr const wchar_t* kServiceName = L"NulConnectHelper";
constexpr const wchar_t* kHelperExe = L"nulconnect-helper.exe";

std::optional<DWORD> ServiceState() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return std::nullopt;
    std::optional<DWORD> state;
    if (SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_QUERY_STATUS)) {
        SERVICE_STATUS status{};
        if (QueryServiceStatus(service, &status)) state = status.dwCurrentState;
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return state;
}

std::wstring BundledHelperPath() {
    return JoinPath(ExecutableDirectory(), kHelperExe);
}

std::wstring ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = buffer ? Trim(std::wstring(buffer)) : L"error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    return text;
}

}  // namespace

bool HelperClient::IsInstalled() {
    return ServiceState().has_value();
}

bool HelperClient::IsRunning() {
    auto state = ServiceState();
    return state && *state == SERVICE_RUNNING;
}

nlohmann::json HelperClient::Send(const std::string& command, const nlohmann::json& extra, DWORD timeoutMs) {
    nlohmann::json request = extra.is_object() ? extra : nlohmann::json::object();
    request["id"] = NewUuid();
    request["command"] = command;
    std::string payload = request.dump();
    std::string response(64 * 1024, '\0');
    DWORD read = 0;
    // CallNamedPipe connects, writes one message, reads the reply and closes;
    // it waits up to `timeoutMs` for a free pipe instance.
    BOOL ok = CallNamedPipeW(kPipeName, payload.data(), static_cast<DWORD>(payload.size()), response.data(),
                             static_cast<DWORD>(response.size()), &read, timeoutMs);
    if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_SEM_TIMEOUT) {
            throw AppError(ErrorKind::HelperFailed,
                           TrFormat(L"Could not connect to privileged component: %1$@", {ErrorText(error)}));
        }
        throw AppError(ErrorKind::HelperFailed, Tr(L"Privileged component command failed") + L": " + ErrorText(error));
    }
    response.resize(read);
    auto json = nlohmann::json::parse(response, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw AppError(ErrorKind::HelperFailed, Tr(L"Privileged component command failed"));
    }
    if (!json.value("ok", false)) {
        std::string message = json.contains("error") ? json["error"].value("message", "") : "";
        if (message.empty()) message = Narrow(Tr(L"Privileged component command failed"));
        throw AppError(ErrorKind::HelperFailed, message);
    }
    return json.contains("data") ? json["data"] : nlohmann::json::object();
}

std::optional<std::string> HelperClient::InstalledVersion() {
    if (!IsRunning()) return std::nullopt;
    try {
        return Send("version", {}, 2000).value("version", "");
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::string> HelperClient::BundledVersion() {
    std::wstring path = BundledHelperPath();
    if (!FileExists(path)) return std::nullopt;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) return std::nullopt;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullptr;
    std::wstring commandLine = L"\"" + path + L"\" version";
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        return std::nullopt;
    }
    std::string output;
    char buffer[256];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
        if (output.size() > 4096) break;
    }
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readPipe);
    std::string version = Trim(output);
    if (version.empty() || version.size() > 32) return std::nullopt;
    return version;
}

int HelperClient::CompareVersions(const std::string& left, const std::string& right) {
    auto parse = [](std::string value) {
        if (!value.empty() && (value[0] == 'v' || value[0] == 'V')) value.erase(0, 1);
        std::vector<long> parts;
        for (const auto& piece : Split(value, '.')) parts.push_back(std::strtol(piece.c_str(), nullptr, 10));
        return parts;
    };
    auto a = parse(left), b = parse(right);
    size_t count = std::max(a.size(), b.size());
    a.resize(count, 0);
    b.resize(count, 0);
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

bool HelperClient::RequiresInstallOrUpgrade() {
    if (!IsInstalled() || !IsRunning()) return true;
    auto installed = InstalledVersion();
    auto bundled = BundledVersion();
    if (!installed) return true;
    if (!bundled) return false;
    return CompareVersions(*bundled, *installed) > 0;
}

void HelperClient::RunElevated(const std::wstring& verb) {
    std::wstring helper = BundledHelperPath();
    if (!FileExists(helper)) {
        throw AppError(ErrorKind::HelperNotInstalled, Tr(L"The privileged component is missing from the application folder."));
    }
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring resultPath = JoinPath(tempDir, L"nulconnect-helper-" + Widen(RandomHex(8)) + L".json");
    std::wstring parameters = verb + L" --result \"" + resultPath + L"\"";
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = helper.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    Log("[Helper] running elevated: " + Narrow(verb));
    if (!ShellExecuteExW(&info)) {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            throw AppError(ErrorKind::Cancelled, Tr(L"Administrator authorization was cancelled."));
        }
        throw AppError(ErrorKind::HelperFailed, ErrorText(error));
    }
    WaitForSingleObject(info.hProcess, 120000);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    std::string data;
    std::string message;
    if (ReadFileBytes(resultPath, data)) {
        auto json = nlohmann::json::parse(data, nullptr, false);
        if (json.is_object() && !json.value("ok", false)) message = json.value("message", "");
    }
    DeleteFileIfExists(resultPath);
    if (exitCode != 0) {
        if (message.empty()) message = Narrow(Tr(L"Privileged component command failed"));
        Log("[Helper] elevated " + Narrow(verb) + " failed: " + message);
        throw AppError(ErrorKind::HelperFailed, message);
    }
    Log("[Helper] elevated " + Narrow(verb) + " succeeded");
}

void HelperClient::Install() {
    RunElevated(L"install");
}

void HelperClient::Uninstall() {
    RunElevated(L"uninstall");
}

nlohmann::json HelperClient::Status() {
    return Send("status", {}, 3000);
}

nlohmann::json HelperClient::StartTun(const nlohmann::json& config) {
    return Send("start_tun", {{"config", config}}, 10000);
}

void HelperClient::StopTun() {
    Send("stop_tun", {}, 30000);
}

}  // namespace nc
