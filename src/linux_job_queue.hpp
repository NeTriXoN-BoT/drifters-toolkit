#pragma once
#include "core.hpp"
#include <QMetaObject>
#include <QDebug>
#include <QCoreApplication>
#include <QEventLoop>
#include <algorithm>
#include <chrono>
#include <QObject>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace drifter {
// Qt network adapters run nested event loops. Posting every request directly to
// their thread would allow a later request to replace a context still in use.
class LinuxJobQueue final : public QObject {
public:
    using Work = std::function<json()>;
    using Completion = std::function<void(json)>;

    LinuxJobQueue(QObject* worker, QObject* parent = nullptr, std::size_t limit = 32)
        : QObject(parent), worker_(worker), execution_(std::make_shared<Execution>()),
          limit_(std::max<std::size_t>(1, std::min<std::size_t>(32, limit))) {}
    ~LinuxJobQueue() override { stopAndWait(); }

    bool enqueue(Work work, Completion completion) {
        if (stopped_ || outstanding() >= limit_) return false;
        pending_.push_back({std::move(work), std::move(completion)});
        dispatch();
        return true;
    }
    std::size_t outstanding() const { return pending_.size() + (active_ ? 1 : 0); }

    // Stop every cooperating queue before draining callbacks for any one of them.
    void requestStop() {
        stopped_ = true;
        pending_.clear();
    }

    // Call on the owner/GUI thread before destroying the backend or its worker.
    void stopAndWait() {
        requestStop();
        std::unique_lock<std::mutex> lock(execution_->mutex);
        while (execution_->closures != 0) {
            // In-flight browser/folder/clipboard adapters may be waiting for a
            // GUI acknowledgement. Drain those callbacks without dispatching
            // another backend task: stopped_ is already set and pending empty.
            lock.unlock();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
            lock.lock();
            if (execution_->closures != 0) execution_->finished.wait_for(lock, std::chrono::milliseconds(10));
        }
    }

private:
    struct Task {
        Work work;
        Completion completion;
    };
    struct Execution {
        std::mutex mutex;
        std::condition_variable finished;
        unsigned closures = 0;
    };
    QObject* worker_;
    std::shared_ptr<Execution> execution_;
    std::deque<Task> pending_;
    bool active_ = false, stopped_ = false;
    const std::size_t limit_;

    void dispatch() {
        if (stopped_ || active_ || pending_.empty()) return;
        active_ = true;
        auto task = std::make_shared<Task>(std::move(pending_.front()));
        pending_.pop_front();
        const auto execution = execution_;
        {
            std::lock_guard<std::mutex> lock(execution->mutex);
            ++execution->closures;
        }
        const bool posted = QMetaObject::invokeMethod(
            worker_,
            [this, execution, task]() mutable {
                json result;
                try {
                    result = task->work();
                } catch (...) {
                    result = {
                        {"ok", false},
                        {"error",
                         "The native request stopped unexpectedly. Check Diagnostics before repeating a file change."}};
                }
                // Post before releasing the lifetime guard. Destruction waits
                // until this invocation no longer needs the QObject pointer.
                QMetaObject::invokeMethod(
                    this,
                    [this, completion = std::move(task->completion), result = std::move(result)]() mutable {
                        active_ = false;
                        if (stopped_) return;
                        try {
                            completion(std::move(result));
                        } catch (...) {
                            qWarning("A native completion handler stopped unexpectedly.");
                        }
                        dispatch();
                    },
                    Qt::QueuedConnection);
                {
                    std::lock_guard<std::mutex> lock(execution->mutex);
                    --execution->closures;
                }
                execution->finished.notify_all();
            },
            Qt::QueuedConnection);
        if (!posted) {
            active_ = false;
            {
                std::lock_guard<std::mutex> lock(execution->mutex);
                --execution->closures;
            }
            execution->finished.notify_all();
            task->completion(
                {{"ok", false}, {"error", "The native worker is unavailable. No automatic retry was attempted."}});
        }
    }
};
}
