#include "snow_shot/presentation/globalmousemanager.h"
#include "snow_shot/storage/applicationstorage.h"

#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <array>
#include <utility>

namespace snow_shot::presentation {
struct GlobalMouseManager::Impl {
    Impl(GlobalMouseManager& owner, std::unique_ptr<GlobalMouseBackend> input)
        : q(owner), backend(input ? std::move(input) : createGlobalMouseBackend()) {}

    void reload() {
        configuration.bindings.clear();
        using Action = settings::SettingsGlobalMouseAction;
        const std::array pairs{
            std::pair{Action::Screenshot, "global_mouse/screenshot"},
            std::pair{Action::ScreenshotCopy, "global_mouse/screenshot_copy"},
            std::pair{Action::ScreenshotPinToScreen, "global_mouse/screenshot_fixed"},
            std::pair{Action::ScreenshotOcr, "global_mouse/screenshot_ocr"},
            std::pair{Action::ScreenshotOcrCopy, "global_mouse/screenshot_ocr_copy"},
            std::pair{Action::ScreenshotTranslation, "global_mouse/screenshot_translation"},
            std::pair{Action::ScreenshotSave, "global_mouse/screenshot_save"},
            std::pair{Action::ScreenshotQuickSave, "global_mouse/screenshot_quick_save"},
            std::pair{Action::ScreenRecording, "global_mouse/screen_recording"}};
        auto& store = storage::ApplicationStorage::instance().configuration();
        for (const auto& [action, key] : pairs) {
            const auto value = store.value(QString::fromLatin1(key)).toObject();
            const auto binding = globalMouseBinding(
                action, {value.value(QStringLiteral("activation_key")).toVariant().toStringList(),
                         value.value(QStringLiteral("mouse_button")).toString()});
            if (binding) {
                configuration.bindings.push_back(*binding);
            }
        }
        configureBackend();
    }

    void configureBackend() {
        if (globalMouseEnabled && !fullscreenSuppressionActive) {
            backend->configure(configuration);
        } else {
            // Direct settings-button drags still need capture availability.
            backend->configure({{}, configuration.captureAvailable});
        }
    }

    void post(GlobalMouseDragEvent event, quint64 epoch) {
        QMutexLocker lock(&mutex);
        if (epoch != generation) {
            return;
        }
        using Kind = GlobalMouseDragEvent::Kind;
        if (!pending.isEmpty() && pending.last().kind == Kind::Update &&
            pending.last().id == event.id && event.kind != Kind::Begin) {
            pending.last() = event;
            if (event.kind == Kind::Update)
                return;
        } else {
            pending.push_back(event);
        }
        if (!deliveryQueued) {
            deliveryQueued = true;
            QMetaObject::invokeMethod(&q, [this, epoch] { deliver(epoch); }, Qt::QueuedConnection);
        }
    }

    void deliver(quint64 epoch) {
        using Kind = GlobalMouseDragEvent::Kind;
        for (;;) {
            GlobalMouseDragEvent event;
            bool requestFrame = false;
            {
                QMutexLocker lock(&mutex);
                if (epoch != generation || !started)
                    return;
                if (pending.isEmpty()) {
                    deliveryQueued = false;
                    return;
                }
                event = pending.front();
                if (event.kind == Kind::Update && event.id == activeId && pending.size() == 1) {
                    // Leave the endpoint in the mailbox until the display frame consumes it.
                    deliveryQueued = false;
                    if (moveAnnounced)
                        return;
                    moveAnnounced = true;
                    requestFrame = true;
                } else {
                    pending.removeFirst();
                    moveAnnounced = false;
                }
            }
            if (requestFrame) {
                emit q.dragUpdatePending(event.id, event.position);
                return;
            }
            if (event.kind == Kind::Begin) {
                activeId = pendingCaptureId = event.id;
                pendingCaptureEvent = event;
            } else if (event.kind == Kind::Cancel && pendingCaptureId == event.id) {
                activeId = pendingCaptureId = 0;
            } else if (event.id != activeId) {
                continue;
            } else if (event.kind == Kind::Finish) {
                activeId = 0;
            }
            const QPointer<GlobalMouseManager> lifetime(&q);
            emit q.dragEvent(event);
            if (!lifetime)
                return;
        }
    }

    GlobalMouseManager& q;
    std::unique_ptr<GlobalMouseBackend> backend;
    GlobalMouseConfiguration configuration;
    GlobalMousePermissionState permission;
    QMetaObject::Connection configurationConnection;
    QMutex mutex;
    QVector<GlobalMouseDragEvent> pending;
    quint64 generation = 0;
    quint64 activeId = 0;
    // Release stops frame pacing, but capture preparation may still need cancellation.
    quint64 pendingCaptureId = 0;
    GlobalMouseDragEvent pendingCaptureEvent;
    bool moveAnnounced = false;
    bool deliveryQueued = false;
    bool started = false;
    bool globalMouseEnabled = true;
    bool fullscreenSuppressionActive = false;
};

GlobalMouseManager::GlobalMouseManager(QObject* parent) : GlobalMouseManager(nullptr, parent) {}
GlobalMouseManager::GlobalMouseManager(std::unique_ptr<GlobalMouseBackend> backend, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(backend))) {}
GlobalMouseManager::~GlobalMouseManager() {
    disconnect(this, nullptr, nullptr, nullptr);
    shutdown();
}

void GlobalMouseManager::initialize() {
    auto& impl = *m_impl;
    if (impl.started) {
        return;
    }
    impl.started = true;
    const quint64 epoch = impl.generation;
    impl.reload();
    impl.configurationConnection = connect(
        &storage::ApplicationStorage::instance().configuration(),
        &storage::ConfigurationStore::valuesCommitted, this,
        [this](storage::ConfigurationWriteOrigin, const auto& previous, const auto& values) {
            const QString prefix = QStringLiteral("global_mouse/");
            for (auto it = values.lowerBound(prefix);
                 it != values.cend() && it.key().startsWith(prefix); ++it) {
                if (previous.value(it.key()) != it.value()) {
                    m_impl->reload();
                    break;
                }
            }
        });
    impl.backend->setPermissionRefreshHandler([this, epoch] {
        QMetaObject::invokeMethod(
            this,
            [this, epoch] {
                if (m_impl->started && epoch == m_impl->generation)
                    emit permissionRefreshRequested();
            },
            Qt::QueuedConnection);
    });
    impl.backend->setStateHandler([this, epoch](GlobalMousePermissionState state) {
        QMetaObject::invokeMethod(
            this,
            [this, epoch, state] {
                if (!m_impl->started || epoch != m_impl->generation || m_impl->permission == state)
                    return;
                m_impl->permission = state;
                emit permissionStateChanged(state);
                using Status = GlobalMousePermissionState::Status;
                if (state.status == Status::ListenRequired ||
                    state.status == Status::AccessibilityRequired ||
                    state.status == Status::Unavailable)
                    emit operationFailed(globalMousePermissionMessage(state));
            },
            Qt::QueuedConnection);
    });
    impl.permission = impl.backend->permissionState();
    emit permissionStateChanged(impl.permission);
    impl.backend->start(
        [this, epoch](GlobalMouseDragEvent event) { m_impl->post(event, epoch); },
        [this, epoch](quint32 code) {
            QMetaObject::invokeMethod(
                this,
                [this, code, epoch]() {
                    if (m_impl->started && epoch == m_impl->generation) {
                        qWarning("Global mouse input initialization failed: %u", code);
                        emit operationFailed(
                            tr("Global mouse input is unavailable (error %1)").arg(code));
                    }
                },
                Qt::QueuedConnection);
        });
}

Qt::KeyboardModifiers GlobalMouseManager::heldActivationModifiers() const {
    const auto held = m_impl->backend->heldActivationModifiers();
    Qt::KeyboardModifiers modifiers;
    modifiers.setFlag(Qt::ShiftModifier, held.testFlag(GlobalMouseModifier::Shift));
    modifiers.setFlag(Qt::AltModifier, held.testFlag(GlobalMouseModifier::Alt));
#ifdef Q_OS_MACOS
    modifiers.setFlag(Qt::ControlModifier, held.testFlag(GlobalMouseModifier::Command));
    modifiers.setFlag(Qt::MetaModifier, held.testFlag(GlobalMouseModifier::Control));
#else
    modifiers.setFlag(Qt::ControlModifier, held.testFlag(GlobalMouseModifier::Control));
    modifiers.setFlag(Qt::MetaModifier, held.testFlag(GlobalMouseModifier::Super));
#endif
    return modifiers;
}

void GlobalMouseManager::deliverPendingDrag(quint64 id, bool includeTerminal) {
    using Kind = GlobalMouseDragEvent::Kind;
    GlobalMouseDragEvent event;
    {
        QMutexLocker lock(&m_impl->mutex);
        auto& pending = m_impl->pending;
        if (!id || id != m_impl->pendingCaptureId || pending.isEmpty() || pending.front().id != id)
            return;
        const auto kind = pending.front().kind;
        if (kind == Kind::Begin || (!includeTerminal && kind != Kind::Update) ||
            (kind != Kind::Cancel && id != m_impl->activeId))
            return;
        event = pending.takeFirst();
        // Escape can arrive after button-up while the captured pixels are still pending.
        if (includeTerminal && event.kind == Kind::Finish && !pending.isEmpty() &&
            pending.front().id == id && pending.front().kind == Kind::Cancel)
            event = pending.takeFirst();
        m_impl->moveAnnounced = false;
        if (event.kind != Kind::Update)
            m_impl->activeId = 0;
        if (event.kind == Kind::Cancel)
            m_impl->pendingCaptureId = 0;
    }
    emit dragEvent(event);
}

void GlobalMouseManager::beginButtonDrag(settings::SettingsGlobalMouseAction action) {
    if (m_impl->started && m_impl->configuration.captureAvailable) {
        m_impl->backend->beginButtonDrag(action);
    }
}

GlobalMousePermissionState GlobalMouseManager::permissionState() const {
    return m_impl->permission;
}

void GlobalMouseManager::usePermissionSnapshot(bool listen, bool accessibility) {
    m_impl->backend->usePermissionSnapshot(listen, accessibility);
}
void GlobalMouseManager::refreshPermission() {
    m_impl->backend->refreshPermission();
}

void GlobalMouseManager::requestPermission() {
    m_impl->backend->requestPermission();
    refreshPermission();
}

void GlobalMouseManager::openPermissionSettings() {
    m_impl->backend->openPermissionSettings();
}

void GlobalMouseManager::shutdown() {
    if (!m_impl->started) {
        return;
    }
    const auto pendingId = m_impl->pendingCaptureId;
    auto cancellation = m_impl->pendingCaptureEvent;
    cancellation.kind = GlobalMouseDragEvent::Kind::Cancel;
    m_impl->started = false;
    disconnect(m_impl->configurationConnection);
    m_impl->backend->stop();
    {
        QMutexLocker lock(&m_impl->mutex);
        ++m_impl->generation;
        m_impl->activeId = 0;
        m_impl->pendingCaptureId = 0;
        m_impl->moveAnnounced = false;
        m_impl->deliveryQueued = false;
        m_impl->pending.clear();
    }
    m_impl->permission = {};
    emit permissionStateChanged(m_impl->permission);
    // Complete teardown before invoking subscribers, which may restart the manager.
    if (pendingId != 0)
        emit dragEvent(cancellation);
}

bool GlobalMouseManager::globalMouseEnabled() const {
    return m_impl->globalMouseEnabled;
}

void GlobalMouseManager::setGlobalMouseEnabled(bool enabled) {
    if (m_impl->globalMouseEnabled == enabled)
        return;
    m_impl->globalMouseEnabled = enabled;
    m_impl->configureBackend();
    emit globalMouseEnabledChanged(enabled);
}

void GlobalMouseManager::setFocusedFullscreenSuppressionActive(bool active) {
    if (m_impl->fullscreenSuppressionActive == active)
        return;
    m_impl->fullscreenSuppressionActive = active;
    m_impl->configureBackend();
}

void GlobalMouseManager::setCaptureAvailable(bool available) {
    m_impl->configuration.captureAvailable = available;
    m_impl->configureBackend();
}
void GlobalMouseManager::cancelGesture(quint64 id) {
    if (m_impl->pendingCaptureId == id) {
        m_impl->pendingCaptureId = 0;
        m_impl->activeId = 0;
        QMutexLocker lock(&m_impl->mutex);
        m_impl->moveAnnounced = false;
        m_impl->pending.removeIf([id](const auto& event) { return event.id == id; });
    }
    m_impl->backend->cancel(id);
}
QString globalMousePermissionMessage(const GlobalMousePermissionState& state) {
    using Status = GlobalMousePermissionState::Status;
    switch (state.status) {
    case Status::Ready:
        return GlobalMouseManager::tr("Global mouse gestures are ready");
    case Status::ListenRequired:
        return GlobalMouseManager::tr("Allow Snow Shot in System Settings > Privacy & Security > "
                                      "Input Monitoring to use global mouse gestures");
    case Status::AccessibilityRequired:
        return GlobalMouseManager::tr("Allow Snow Shot in System Settings > Privacy & Security > "
                                      "Accessibility to use global mouse gestures");
    case Status::Unavailable:
        return GlobalMouseManager::tr(
            "Global mouse input is unavailable. Check permissions and retry");
    case Status::Suspended:
        return GlobalMouseManager::tr(
            "Global mouse gestures are paused while this session is inactive");
    case Status::Unknown:
        return GlobalMouseManager::tr("Checking global mouse permissions...");
    }
    return {};
}
} // namespace snow_shot::presentation
