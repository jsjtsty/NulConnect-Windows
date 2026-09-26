#include "pch.h"
#include "core/Dispatcher.h"
#include "core/Str.h"

namespace nc {

namespace {

constexpr UINT kDispatchMessage = WM_APP + 1;

struct TimerEntry {
    Dispatcher::Task task;
    bool repeat = false;
};

HWND g_window = nullptr;
DWORD g_uiThreadId = 0;
std::mutex g_queueMutex;
std::deque<Dispatcher::Task> g_queue;
std::unordered_map<UINT_PTR, TimerEntry> g_timers;
UINT_PTR g_nextTimerId = 1;

void Drain() {
    while (true) {
        Dispatcher::Task task;
        {
            std::lock_guard lock(g_queueMutex);
            if (g_queue.empty()) return;
            task = std::move(g_queue.front());
            g_queue.pop_front();
        }
        if (task) task();
    }
}

LRESULT CALLBACK DispatcherProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kDispatchMessage) {
        Drain();
        return 0;
    }
    if (message == WM_TIMER) {
        auto it = g_timers.find(wParam);
        if (it == g_timers.end()) {
            KillTimer(hwnd, wParam);
            return 0;
        }
        Dispatcher::Task task = it->second.task;
        if (!it->second.repeat) {
            KillTimer(hwnd, wParam);
            g_timers.erase(it);
        }
        if (task) task();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

AppError::AppError(ErrorKind kind, const std::wstring& message) : std::runtime_error(Narrow(message)), kind_(kind) {}

std::wstring AppError::Message() const {
    return Widen(what());
}

ErrorInfo CurrentError() {
    try {
        throw;
    } catch (const AppError& error) {
        return {error.kind(), error.Message()};
    } catch (const std::exception& error) {
        return {ErrorKind::Generic, Widen(error.what())};
    } catch (...) {
        return {ErrorKind::Generic, L"Unknown error"};
    }
}

void Dispatcher::Initialize() {
    g_uiThreadId = GetCurrentThreadId();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DispatcherProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NulConnect.Dispatcher";
    RegisterClassExW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

void Dispatcher::Shutdown() {
    for (auto& [id, entry] : g_timers) {
        KillTimer(g_window, id);
    }
    g_timers.clear();
    if (g_window) {
        DestroyWindow(g_window);
        g_window = nullptr;
    }
}

void Dispatcher::Post(Task task) {
    {
        std::lock_guard lock(g_queueMutex);
        g_queue.push_back(std::move(task));
    }
    if (g_window) {
        PostMessageW(g_window, kDispatchMessage, 0, 0);
    }
}

Dispatcher::TimerId Dispatcher::SetTimeout(unsigned milliseconds, Task task) {
    UINT_PTR id = g_nextTimerId++;
    g_timers[id] = TimerEntry{std::move(task), false};
    SetTimer(g_window, id, std::max(1u, milliseconds), nullptr);
    return static_cast<TimerId>(id);
}

Dispatcher::TimerId Dispatcher::SetInterval(unsigned milliseconds, Task task) {
    UINT_PTR id = g_nextTimerId++;
    g_timers[id] = TimerEntry{std::move(task), true};
    SetTimer(g_window, id, std::max(1u, milliseconds), nullptr);
    return static_cast<TimerId>(id);
}

void Dispatcher::ClearTimer(TimerId& id) {
    if (id == 0) return;
    KillTimer(g_window, id);
    g_timers.erase(id);
    id = 0;
}

bool Dispatcher::IsUiThread() {
    return GetCurrentThreadId() == g_uiThreadId;
}

SerialQueue::SerialQueue(std::string name) : name_(std::move(name)) {
    thread_ = std::thread([this] { Run(); });
}

SerialQueue::~SerialQueue() {
    Drain();
}

void SerialQueue::Drain() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    condition_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SerialQueue::Enqueue(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        tasks_.push_back(std::move(task));
    }
    condition_.notify_one();
}

void SerialQueue::Run() {
    std::wstring threadName = Widen(name_);
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    if (auto setName = reinterpret_cast<SetThreadDescriptionFn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))) {
        setName(GetCurrentThread(), threadName.c_str());
    }
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try {
            task();
        } catch (...) {
        }
    }
}

void RunAsyncVoid(SerialQueue& queue, CancelToken token, std::function<void()> work, std::function<void()> onSuccess,
                  std::function<void(const ErrorInfo&)> onError) {
    RunAsync<bool>(
        queue, std::move(token),
        [work = std::move(work)]() {
            work();
            return true;
        },
        [onSuccess = std::move(onSuccess)](bool) {
            if (onSuccess) onSuccess();
        },
        std::move(onError));
}

}  // namespace nc
