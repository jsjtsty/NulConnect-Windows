#pragma once

#include "core/Error.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nc {

// Runs callbacks on the UI thread. All model state is owned by the UI thread;
// background work reports back through `Post`.
class Dispatcher {
public:
    using Task = std::function<void()>;
    using TimerId = unsigned;

    static void Initialize();
    static void Shutdown();

    // Thread-safe.
    static void Post(Task task);

    // UI thread only. Timer ids are never 0.
    static TimerId SetTimeout(unsigned milliseconds, Task task);
    static TimerId SetInterval(unsigned milliseconds, Task task);
    static void ClearTimer(TimerId& id);
    static bool IsUiThread();
};

// A single background thread executing tasks in order. Used to serialize
// blocking libreatrust and helper calls.
class SerialQueue {
public:
    explicit SerialQueue(std::string name);
    ~SerialQueue();
    SerialQueue(const SerialQueue&) = delete;
    SerialQueue& operator=(const SerialQueue&) = delete;

    void Enqueue(std::function<void()> task);
    // Blocks until all queued tasks ran and stops the thread.
    void Drain();

private:
    void Run();

    std::string name_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::thread thread_;
};

// Cancellation flag shared between the UI thread and a background task.
using CancelToken = std::shared_ptr<std::atomic<bool>>;
inline CancelToken MakeCancelToken() { return std::make_shared<std::atomic<bool>>(false); }
inline bool IsCancelled(const CancelToken& token) { return token && token->load(); }
inline void CancelAndReset(CancelToken& token) {
    if (token) token->store(true);
    token.reset();
}

// Runs `work` on `queue`, then delivers its result or error to the UI thread.
// When `token` is cancelled before delivery, neither callback runs.
template <typename T>
void RunAsync(SerialQueue& queue, CancelToken token, std::function<T()> work, std::function<void(T)> onSuccess,
              std::function<void(const ErrorInfo&)> onError) {
    queue.Enqueue([token, work = std::move(work), onSuccess = std::move(onSuccess), onError = std::move(onError)]() mutable {
        if (IsCancelled(token)) return;
        try {
            auto result = std::make_shared<T>(work());
            Dispatcher::Post([token, onSuccess = std::move(onSuccess), result]() mutable {
                if (IsCancelled(token)) return;
                if (onSuccess) onSuccess(std::move(*result));
            });
        } catch (...) {
            ErrorInfo error = CurrentError();
            Dispatcher::Post([token, onError = std::move(onError), error]() {
                if (IsCancelled(token)) return;
                if (onError) onError(error);
            });
        }
    });
}

void RunAsyncVoid(SerialQueue& queue, CancelToken token, std::function<void()> work, std::function<void()> onSuccess,
                  std::function<void(const ErrorInfo&)> onError);

}  // namespace nc
