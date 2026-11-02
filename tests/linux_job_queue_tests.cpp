#include "../src/linux_job_queue.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void nestedWait(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
void waitFor(QEventLoop& loop) {
    QTimer::singleShot(5000, &loop, [&] { loop.exit(99); });
    check(loop.exec() == 0, "The queue fixture exceeded its deadline.");
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QThread thread;
    QObject worker;
    worker.moveToThread(&thread);
    thread.start();
    int result = 0;
    try {
        // Confirm that a real Qt nested loop processes ordinary queued calls.
        std::atomic<int> depth{0}, unsafePeak{0};
        QEventLoop baseline;
        QMetaObject::invokeMethod(
            &worker,
            [&] {
                ++depth;
                QMetaObject::invokeMethod(
                    &worker,
                    [&] {
                        unsafePeak = ++depth;
                        --depth;
                    },
                    Qt::QueuedConnection);
                nestedWait(30);
                --depth;
                QMetaObject::invokeMethod(&baseline, &QEventLoop::quit, Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
        waitFor(baseline);
        check(unsafePeak == 2, "The fixture must reproduce nested-loop worker reentry.");

        drifter::LinuxJobQueue queue(&worker);
        std::atomic<int> peak{0};
        std::vector<int> order;
        int context = 7, completions = 0;
        QEventLoop serialized;
        auto complete = [&](drifter::json value) {
            check(value.value("ok", false), "Serialized work returned a failure.");
            if (++completions == 2) serialized.quit();
        };
        check(queue.enqueue(
                  [&] {
                      peak = ++depth;
                      const int captured = context;
                      order.push_back(1);
                      nestedWait(60);
                      if (context != captured) throw std::runtime_error("Context changed during an in-flight request.");
                      order.push_back(2);
                      --depth;
                      return drifter::json{{"ok", true}};
                  },
                  complete),
              "The first request was refused.");
        QTimer::singleShot(5, &serialized, [&] {
            check(queue.enqueue(
                      [&] {
                          const int current = ++depth;
                          peak = std::max(peak.load(), current);
                          context = 9;
                          order.push_back(3);
                          --depth;
                          return drifter::json{{"ok", true}};
                      },
                      complete),
                  "The second request was refused.");
        });
        waitFor(serialized);
        check(peak == 1, "A second backend call entered the nested event loop.");
        check(order == std::vector<int>({1, 2, 3}), "Worker calls did not stay in FIFO order.");
        check(context == 9 && queue.outstanding() == 0, "The completed queue retained pending work.");

        QEventLoop capped;
        int finished = 0;
        for (int index = 0; index < 32; ++index) {
            check(queue.enqueue([index] { return drifter::json{{"index", index}}; },
                                [&, index](drifter::json value) {
                                    check(value.at("index") == index, "A queue result changed identity.");
                                    check(finished == index, "The bounded queue reordered requests.");
                                    if (++finished == 32) capped.quit();
                                }),
                  "An allowed queue slot was refused.");
        }
        check(queue.outstanding() == 32, "The active request is not counted toward the cap.");
        check(!queue.enqueue([] { return drifter::json{}; }, [](drifter::json) {}),
              "A 33rd outstanding request was accepted.");
        waitFor(capped);

        // The independent media worker uses a stricter cap than file operations.
        drifter::LinuxJobQueue media(&worker, nullptr, 8);
        QEventLoop mediaDone;
        int pictures = 0;
        for (int index = 0; index < 8; ++index)
            check(media.enqueue(
                      [] {
                          nestedWait(5);
                          return drifter::json{};
                      },
                      [&](drifter::json) {
                          if (++pictures == 8) mediaDone.quit();
                      }),
                  "An allowed independent image slot was refused.");
        check(!media.enqueue([] { return drifter::json{}; }, [](drifter::json) {}),
              "A ninth media task exceeded the strict image queue cap.");
        waitFor(mediaDone);
        check(pictures == 8 && media.outstanding() == 0, "The bounded image queue did not drain.");

        QEventLoop failures;
        check(queue.enqueue([]() -> drifter::json { throw std::runtime_error("private fixture detail"); },
                            [&](drifter::json value) {
                                check(!value.value("ok", true), "A thrown request was reported as successful.");
                                check(value.dump().find("private fixture detail") == std::string::npos,
                                      "An unexpected exception disclosed its raw payload.");
                            }),
              "The exception request was refused.");
        check(queue.enqueue([] { return drifter::json{{"ok", true}}; }, [&](drifter::json) { failures.quit(); }),
              "A request following an exception was refused.");
        waitFor(failures);

        std::atomic<bool> started{false}, completed{false};
        bool cancelledRan = false, callbackRan = false;
        auto shuttingDown = std::make_unique<drifter::LinuxJobQueue>(&worker);
        shuttingDown->enqueue(
            [&] {
                started = true;
                nestedWait(80);
                completed = true;
                return drifter::json{};
            },
            [&](drifter::json) { callbackRan = true; });
        shuttingDown->enqueue(
            [&] {
                cancelledRan = true;
                return drifter::json{};
            },
            [](drifter::json) {});
        QElapsedTimer deadline;
        deadline.start();
        while (!started && deadline.elapsed() < 1000) QThread::msleep(1);
        check(started, "The shutdown fixture never entered its nested loop.");
        shuttingDown->stopAndWait();
        check(completed, "Shutdown returned before the active backend call finished.");
        check(!cancelledRan, "Shutdown dispatched a previously queued file workflow.");
        check(!callbackRan, "Shutdown needed GUI completion reentry to finish.");
        check(!shuttingDown->enqueue([] { return drifter::json{}; }, [](drifter::json) {}),
              "A stopped queue accepted new work.");
        shuttingDown.reset();
        QCoreApplication::processEvents();

        // Closing an image queue may drain a completed file callback. Stop
        // both queues first, so that callback cannot start its next mutation.
        QThread mediaThread;
        QObject mediaWorker;
        mediaWorker.moveToThread(&mediaThread);
        mediaThread.start();
        bool crossQueueMutation = false;
        std::atomic<bool> imageStarted{false}, fileStarted{false};
        {
            drifter::LinuxJobQueue images(&mediaWorker, nullptr, 8), files(&worker);
            images.enqueue(
                [&] {
                    imageStarted = true;
                    nestedWait(60);
                    return drifter::json{};
                },
                [](drifter::json) {});
            files.enqueue(
                [&] {
                    fileStarted = true;
                    return drifter::json{};
                },
                [](drifter::json) {});
            files.enqueue(
                [&] {
                    crossQueueMutation = true;
                    return drifter::json{};
                },
                [](drifter::json) {});
            deadline.restart();
            while ((!imageStarted || !fileStarted) && deadline.elapsed() < 1000) QThread::msleep(1);
            check(imageStarted && fileStarted, "The cooperating-queue fixture did not start.");
            images.requestStop();
            files.requestStop();
            images.stopAndWait();
            files.stopAndWait();
            check(!crossQueueMutation, "Draining one queue dispatched a pending mutation in another queue.");
        }
        mediaThread.quit();
        mediaThread.wait();

        std::atomic<bool> wantsGui{false};
        bool acknowledged = false, pendingMutation = false;
        drifter::LinuxJobQueue guiDependent(&worker);
        guiDependent.enqueue(
            [&] {
                wantsGui = true;
                QMetaObject::invokeMethod(&application, [&] { acknowledged = true; }, Qt::BlockingQueuedConnection);
                return drifter::json{{"ok", true}};
            },
            [](drifter::json) {});
        guiDependent.enqueue(
            [&] {
                pendingMutation = true;
                return drifter::json{};
            },
            [](drifter::json) {});
        deadline.restart();
        while (!wantsGui && deadline.elapsed() < 1000) QThread::msleep(1);
        check(wantsGui, "The GUI-acknowledgement fixture did not start.");
        guiDependent.stopAndWait();
        check(acknowledged, "Shutdown deadlocked an active worker waiting for its GUI acknowledgement.");
        check(!pendingMutation, "Draining GUI acknowledgements dispatched a pending backend mutation.");
        check(deadline.elapsed() < 1000, "GUI-dependent shutdown did not finish promptly.");
    } catch (const std::exception& issue) {
        std::cerr << issue.what() << '\n';
        result = 1;
    }
    thread.quit();
    thread.wait();
    if (!result) std::cout << "Linux native FIFO passed " << checks << " real nested-event-loop checks.\n";
    return result;
}
