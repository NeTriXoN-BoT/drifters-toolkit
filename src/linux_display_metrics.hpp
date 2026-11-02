#pragma once
#include "core.hpp"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace drifter {

// Qt geometry is device-independent. The target window's DPR can differ from
// QScreen's DPR on fractional Wayland, so expose both without applying DPI twice.
inline json linuxDisplayMetrics(QWidget* widget) {
    Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
    auto* window = widget ? widget->windowHandle() : nullptr;
    auto* screen = window ? window->screen() : widget ? widget->screen() : QGuiApplication::primaryScreen();
    auto ratio = [](qreal value) {
        return std::isfinite(value) && value > 0 && value <= 16 ? static_cast<double>(value) : 1.0;
    };
    const double screenRatio = screen ? ratio(screen->devicePixelRatio()) : 1.0;
    const double windowRatio = window ? ratio(window->devicePixelRatio()) : screenRatio;
    const auto geometry = screen ? screen->geometry() : QRect();
    auto work = screen ? screen->availableGeometry() : QRect();
    if (work.isEmpty()) work = geometry;
    const auto pixels = [](int logical, double scale) {
        return static_cast<int>(std::min(1000000.0, std::max(0.0, std::round(logical * scale))));
    };
    const auto dpi = [](qreal value) {
        return std::isfinite(value) && value > 0 && value < 10000 ? static_cast<double>(value) : 96.0;
    };
    return {{"source", "qt-qscreen"},
            {"available", screen != nullptr},
            {"monitorId", screen ? screen->name().left(256).toStdString() : std::string()},
            {"monitorLogicalWidth", geometry.width()},
            {"monitorLogicalHeight", geometry.height()},
            {"workLogicalWidth", work.width()},
            {"workLogicalHeight", work.height()},
            {"monitorPixelWidth", pixels(geometry.width(), screenRatio)},
            {"monitorPixelHeight", pixels(geometry.height(), screenRatio)},
            {"monitorPixelSizeDerived", true},
            {"devicePixelRatio", windowRatio},
            {"screenDevicePixelRatio", screenRatio},
            {"logicalDpiX", screen ? dpi(screen->logicalDotsPerInchX()) : 96.0},
            {"logicalDpiY", screen ? dpi(screen->logicalDotsPerInchY()) : 96.0},
            {"windowHidden", widget ? !widget->isVisible() || widget->isMinimized() : true},
            {"windowLogicalWidth", widget ? widget->width() : 0},
            {"windowLogicalHeight", widget ? widget->height() : 0}};
}

inline QSize linuxInitialWindowSize(const json& metrics, QSize frame = {}, bool automatic = true) {
    double extra = 1.0;
    const double ratio = metrics.value("devicePixelRatio", 1.0);
    const double shortPixels = std::min(metrics.value("monitorPixelWidth", 0), metrics.value("monitorPixelHeight", 0));
    if (automatic && ratio > 0 && std::isfinite(ratio) && shortPixels > 0)
        extra = std::round(std::clamp(shortPixels / 1080.0 / ratio, 1.0, 2.0) * 20.0) / 20.0;
    int width = static_cast<int>(std::round(1440.0 * extra));
    int height = static_cast<int>(std::round(955.0 * extra));
    const int availableWidth = metrics.value("workLogicalWidth", 0) - std::max(0, frame.width());
    const int availableHeight = metrics.value("workLogicalHeight", 0) - std::max(0, frame.height());
    if (availableWidth > 0) width = std::min(width, std::max(640, availableWidth));
    if (availableHeight > 0) height = std::min(height, std::max(480, availableHeight));
    return {width, height};
}

// GUI-thread signals only: no idle polling, environment mutation or worker-side
// QScreen access. Changes coalesce before producing a deduplicated observation.
class LinuxDisplayTracker final : public QObject {
    QPointer<QWidget> widget_;
    QPointer<QWindow> window_;
    QPointer<QScreen> screen_;
    QMetaObject::Connection windowConnection_;
    std::vector<QMetaObject::Connection> screenConnections_;
    QTimer timer_;
    std::function<void(const json&)> changed_;
    std::string last_;

    void bind() {
        auto* native = widget_ ? widget_->windowHandle() : nullptr;
        if (window_ != native) {
            disconnect(windowConnection_);
            window_ = native;
            if (native)
                windowConnection_ = connect(native, &QWindow::screenChanged, this, [this](QScreen*) { schedule(); });
        }
        auto* active = native ? native->screen() : widget_ ? widget_->screen() : QGuiApplication::primaryScreen();
        if (screen_ == active) return;
        for (const auto& connection : screenConnections_) disconnect(connection);
        screenConnections_.clear();
        screen_ = active;
        if (!active) return;
        const auto update = [this] { schedule(); };
        screenConnections_.push_back(connect(active, &QScreen::geometryChanged, this, update));
        screenConnections_.push_back(connect(active, &QScreen::availableGeometryChanged, this, update));
        screenConnections_.push_back(connect(active, &QScreen::logicalDotsPerInchChanged, this, update));
        screenConnections_.push_back(connect(active, &QScreen::physicalDotsPerInchChanged, this, update));
        screenConnections_.push_back(connect(active, &QScreen::physicalSizeChanged, this, update));
    }
    void schedule() {
        if (!timer_.isActive()) timer_.start();
    }
    bool eventFilter(QObject* target, QEvent* event) override {
        if (target == widget_ && (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
                                  event->type() == QEvent::Move || event->type() == QEvent::Resize ||
                                  event->type() == QEvent::WinIdChange || event->type() == QEvent::WindowStateChange))
            schedule();
        return QObject::eventFilter(target, event);
    }

public:
    LinuxDisplayTracker(QWidget* widget, std::function<void(const json&)> changed)
        : QObject(widget), widget_(widget), timer_(this), changed_(std::move(changed)) {
        Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
        timer_.setSingleShot(true);
        timer_.setInterval(50);
        connect(&timer_, &QTimer::timeout, this, [this] {
            bind();
            auto current = snapshot();
            auto encoded = current.dump();
            if (encoded == last_) return;
            last_ = std::move(encoded);
            if (changed_) changed_(current);
        });
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen*) { schedule(); });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) { schedule(); });
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) { schedule(); });
        if (widget_) widget_->installEventFilter(this);
        bind();
        schedule();
    }
    json snapshot() const { return linuxDisplayMetrics(widget_); }
};
}
