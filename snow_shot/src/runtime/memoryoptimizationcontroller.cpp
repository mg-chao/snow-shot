#include "snow_shot/runtime/memoryoptimizationcontroller.h"
#include <QCoreApplication>
#include <QEvent>
#include <QPromise>
#include <QThreadPool>
#include <algorithm>

namespace snow_shot::runtime {
using namespace std::chrono_literals;

MemoryOptimizationController::MemoryOptimizationController(RuntimeActivityTracker& activity,
                                                           Options options, QObject* parent)
    : QObject(parent), m_activity(activity), m_options(std::move(options)),
      m_started(m_options.clock()), m_policyChanged(m_started) {
    m_timer.setInterval(5000);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MemoryOptimizationController::evaluate);
    connect(&m_trimWatcher, &QFutureWatcherBase::finished, this,
            &MemoryOptimizationController::finishTrim);
    if (auto* application = QCoreApplication::instance()) {
        application->installEventFilter(this);
        connect(application, &QCoreApplication::aboutToQuit, this,
                &MemoryOptimizationController::stop);
    }
    synchronizePolicy();
}

MemoryOptimizationController::~MemoryOptimizationController() {
    stop();
}

void MemoryOptimizationController::synchronizePolicy() {
    if (m_stopped)
        return;
    const bool enabled = m_options.enabled && m_options.enabled();
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    m_policyChanged = m_options.clock();
    m_activity.markActivity();
    m_blocked = true;
    if (enabled)
        m_timer.start();
    else
        m_timer.stop();
}

void MemoryOptimizationController::stop() {
    if (m_stopped)
        return;
    m_stopped = true;
    m_enabled = false;
    m_timer.stop();
    if (m_pendingIdlePeriod)
        m_activity.markActivity();
}

void MemoryOptimizationController::evaluate() {
    synchronizePolicy();
    if (!m_enabled || m_stopped)
        return;
    const bool safe = m_options.safe && m_options.safe();
    if (!safe) {
        m_blocked = true;
        return;
    }
    if (m_blocked) {
        m_blocked = false;
        m_activity.markActivity();
    }
    const auto now = m_options.clock();
    if (m_pendingIdlePeriod || now - m_started < 60s ||
        (m_lastAttempt && now - *m_lastAttempt < 5min))
        return;
    const auto snapshot = m_activity.snapshot();
    if (snapshot.activeCount != 0 || m_attemptedIdlePeriod == snapshot.idlePeriod)
        return;
    const bool pressure = m_options.lowMemory && m_options.lowMemory().value_or(false);
    const auto quiet = pressure ? 15s : 60s;
    if (now - std::max(snapshot.lastActivity, m_policyChanged) < quiet)
        return;
    const auto before = m_options.sample ? m_options.sample() : std::nullopt;
    constexpr std::uint64_t mib = 1024 * 1024;
    if (!before || before->memoryBytes < (pressure ? 12 : 18) * mib)
        return;
    // Sampling can race worker admission. Recheck UI/policy before preparing the worker.
    if (!m_options.enabled || !m_options.enabled() || !m_options.safe || !m_options.safe() ||
        m_stopped || !m_enabled || !m_options.trim)
        return;
    if (m_activity.snapshot().generation != snapshot.generation)
        return;

    m_pendingIdlePeriod = snapshot.idlePeriod;
    if (m_options.prepare && m_preparedIdlePeriod != snapshot.idlePeriod) {
        m_preparedIdlePeriod = snapshot.idlePeriod;
        m_options.prepare();
    }

    QPromise<std::optional<TrimAttempt>> promise;
    auto future = promise.future();
    auto reclaim = [activity = m_activity, generation = snapshot.generation,
                    clock = m_options.clock, trim = m_options.trim, sample = m_options.sample,
                    pressure, before = *before, promise = std::move(promise)]() mutable {
        promise.start();
        std::optional<TrimAttempt> attempt;
        // Every activity transition advances the generation of the original idle snapshot.
        // New work can start immediately after this check, including while trim() runs.
        if (activity.snapshot().generation == generation) {
            const auto started = clock();
            const auto result = trim();
            const auto duration = clock() - started;
            attempt = TrimAttempt{started, {pressure, before, sample(), result, duration}};
        }
        promise.addResult(std::move(attempt));
        promise.finish();
    };
    if (QThreadPool::globalInstance()->tryStart(std::move(reclaim))) {
        m_trimWatcher.setFuture(future);
    } else {
        m_pendingIdlePeriod.reset();
    }
}

void MemoryOptimizationController::finishTrim() {
    const auto idlePeriod = std::exchange(m_pendingIdlePeriod, {});
    const auto attempt = m_trimWatcher.result();
    if (!attempt || m_stopped)
        return;
    m_lastAttempt = attempt->startedAt;
    m_attemptedIdlePeriod = idlePeriod;
    if (m_options.report)
        m_options.report(attempt->report);
}

bool MemoryOptimizationController::eventFilter(QObject* watched, QEvent* event) {
    if (m_enabled && !m_stopped) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::ApplicationStateChange:
        case QEvent::WindowActivate:
        case QEvent::ContextMenu:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::Drop:
            m_activity.markActivity();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
} // namespace snow_shot::runtime
