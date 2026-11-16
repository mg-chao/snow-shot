#include "macosupdatedriver_p.h"

#include <QEvent>
#include <QPointer>

namespace snow_shot::update {
struct UpdateService::Impl {
    explicit Impl(UpdateService& service, Options options) : q(service) {
        if (options.macOSDriverFactory)
            driver = options.macOSDriverFactory();
#ifdef Q_OS_MACOS
        else
            driver = detail::makeSparkleUpdateDriver();
#endif
        if (!driver)
            return;
        driver->changed = [this](const UpdateStatus& value, bool check, bool cancel, bool onQuit) {
            const bool newlyReady =
                value.state == UpdateState::Ready &&
                (status.state != UpdateState::Ready || status.version != value.version);
            status = value;
            canCheck = check;
            canCancel = cancel;
            installsOnQuit = onQuit;
            if (pending && !committed && value.state == UpdateState::Failed) {
                pending = false;
                emit q.handoffPendingChanged(false);
            }
            emit q.statusChanged();
            if (newlyReady)
                emit q.updateReady();
        };
        driver->finished = [this](const QString& operation, const QString& outcome) {
            emit q.operationFinished(operation, outcome);
        };
        driver->available = [this](const QString& version) {
            emit q.automaticUpdateAvailable(version);
        };
        driver->committed = [this] {
            if (pending && !committed) {
                committed = true;
                emit q.handoffCommitted();
            }
        };
    }
    ~Impl() {
        if (driver) {
            driver->changed = {};
            driver->finished = {};
            driver->available = {};
            driver->committed = {};
        }
    }
    static bool approve(QPointer<QObject> context, std::function<bool()> callback) {
        if (!context || !callback)
            return false;
        try {
            return callback() && context;
        } catch (...) {
            return false;
        }
    }
    UpdateService& q;
    std::unique_ptr<detail::MacOSUpdateDriver> driver;
    UpdateStatus status;
    QString mode = QStringLiteral("next_launch");
    QPointer<QObject> approvalContext;
    QPointer<QObject> validationContext;
    std::function<bool()> approval;
    std::function<bool()> validation;
    bool started = false;
    bool canCheck = false;
    bool canCancel = false;
    bool installsOnQuit = false;
    bool pending = false;
    bool committed = false;
};

UpdateService::UpdateService(Options options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, std::move(options))) {
    setObjectName(QStringLiteral("snowShotUpdateService"));
}
UpdateService::~UpdateService() = default;
const UpdateStatus& UpdateService::status() const {
    return m_impl->status;
}
UpdateCapabilities UpdateService::capabilities() const {
    const bool available = m_impl->driver && m_impl->status.state != UpdateState::Unavailable;
    return {available, available && status().downloadUrl.isEmpty(), available};
}
bool UpdateService::managedByStore() const {
    return false;
}
bool UpdateService::busy() const {
    switch (status().state) {
    case UpdateState::Checking:
    case UpdateState::Downloading:
    case UpdateState::Verifying:
    case UpdateState::Applying:
        return true;
    default:
        return false;
    }
}
bool UpdateService::canCheck() const {
    return capabilities().check && m_impl->canCheck && !busy() && !m_impl->pending;
}
bool UpdateService::canCancel() const {
    return m_impl->canCancel && status().state == UpdateState::Downloading && !m_impl->pending;
}
bool UpdateService::installsOnQuit() const {
    return m_impl->installsOnQuit;
}
bool UpdateService::handoffPending() const {
    return m_impl->pending;
}
void UpdateService::start() {
    if (m_impl->started || !m_impl->driver)
        return;
    m_impl->started = true;
    m_impl->driver->start(m_impl->mode);
}
void UpdateService::setMode(const QString& value) {
    const QString mode = value == u"download" ? QStringLiteral("check") : value;
    if (mode != u"manual" && mode != u"check" && mode != u"next_launch")
        return;
    m_impl->mode = mode;
    if (m_impl->started && m_impl->driver)
        m_impl->driver->setMode(mode);
}
void UpdateService::check(bool manual) {
    start();
    if (canCheck() && (manual || m_impl->mode != u"manual"))
        m_impl->driver->check(manual);
}
void UpdateService::download() {
    if (m_impl->driver && status().state == UpdateState::Available)
        m_impl->driver->download();
}
void UpdateService::cancel() {
    if (canCancel())
        m_impl->driver->cancel();
}
void UpdateService::requestRestart() {
    if (status().state == UpdateState::Ready && !m_impl->pending)
        emit restartRequested();
}
void UpdateService::beginApply() {
    if (!m_impl->driver || status().state != UpdateState::Ready || m_impl->pending)
        return;
    const QPointer<UpdateService> lifetime(this);
    if (!Impl::approve(m_impl->approvalContext, m_impl->approval) || !lifetime)
        return;
    m_impl->pending = true;
    emit handoffPendingChanged(true);
    if (!lifetime)
        return;
    const bool approved = Impl::approve(m_impl->validationContext, m_impl->validation);
    if (!lifetime)
        return;
    if (!approved) {
        m_impl->pending = false;
        emit handoffPendingChanged(false);
        return;
    }
    // Only an explicit restart enters this path. Arm Sparkle's relaunch before the
    // controller commits its quit; ordinary quit is observed by Sparkle's installer.
    m_impl->driver->install();
}
void UpdateService::applyAtStartup() {
    emit operationFinished(QStringLiteral("apply"), QStringLiteral("unsupported"));
}
void UpdateService::setHandoffApproval(QObject* context, std::function<bool()> callback) {
    m_impl->approvalContext = context;
    m_impl->approval = std::move(callback);
}
void UpdateService::setHandoffValidation(QObject* context, std::function<bool()> callback) {
    m_impl->validationContext = context;
    m_impl->validation = std::move(callback);
}
bool UpdateService::setRelaunchArguments(const QStringList& arguments) {
    return acceptsRelaunchArguments(arguments);
}
bool UpdateService::acceptsRelaunchArguments(const QStringList& arguments) const {
    // Sparkle relaunches the bundle without command-line intent. Never claim to
    // have accepted an argument list that cannot be replayed by its installer.
    return arguments.isEmpty() && !m_impl->committed;
}
void UpdateService::clearRelaunchArguments() {}
void UpdateService::reportBlocked(const QString& reason) {
    m_impl->status.error = reason;
    emit statusChanged();
}
// Sparkle owns native networking and its installer UI. Qt proxy/theme settings apply
// to other application services, and must not alter Sparkle's persisted preferences.
void UpdateService::setSystemProxy(bool) {}
void UpdateService::setProxyConfiguration(const QString&, const QString&) {}
void UpdateService::setProgressAppearance(const QJsonObject&) {}
bool UpdateService::event(QEvent* event) {
    if (event->type() == QEvent::LanguageChange && m_impl->driver)
        m_impl->driver->retranslate();
    return QObject::event(event);
}
} // namespace snow_shot::update
