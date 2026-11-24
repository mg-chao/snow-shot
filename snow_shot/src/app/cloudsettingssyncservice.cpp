// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/app/cloudsettingssyncservice.h"

#include "cloudsettingslayout_p.h"
#include "snow_shot/app/accountsessionservice.h"
#include "snow_shot/shortcuts/localshortcutcontext.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "snow_shot/storage/cloudsyncconfiguration.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/storage/settingsadapters.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInformation>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace snow_shot::app {
namespace {
using namespace Qt::StringLiterals;
using storage::ConfigurationWriteOrigin;
using Values = QMap<QString, QJsonValue>;

constexpr int protocolVersion = 1;
constexpr qsizetype updateByteLimit = 192 * 1024;
constexpr int maximumRoundsPerCheck = 12;
const QJsonValue missingOperation(QJsonValue::Null);
const QJsonValue undefined(QJsonValue::Undefined);
const QString ocrModelKey = u"text_recognition/model_type"_s;
const QString deferredOcrModelKey = u"deferred_ocr_model"_s;

QString platform() {
#ifdef Q_OS_MACOS
    return u"macos"_s;
#else
    return u"windows"_s;
#endif
}

QString accountKey(const AccountSessionService::SettingsSyncAccount& account) {
    if (account.accountId.isEmpty() || account.serviceUrl.isEmpty())
        return {};
    QByteArray bytes = account.serviceUrl.toUtf8();
    bytes.append('\0');
    bytes.append(account.accountId.toUtf8());
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonValue operationAt(const QJsonObject& settings, const QString& key) {
    const auto value = settings.value(key);
    return value.isObject() ? value : missingOperation;
}

QJsonObject operationFor(const QJsonValue& value, ConfigurationWriteOrigin origin) {
    if (origin == ConfigurationWriteOrigin::Reset)
        return {{u"op"_s, u"reset"_s}};
    const bool empty = (value.isArray() && value.toArray().isEmpty()) ||
                       (value.isObject() && value.toObject().isEmpty()) ||
                       (value.isString() && value.toString().isEmpty());
    return {{u"op"_s, empty ? u"clear"_s : u"set"_s}, {u"value"_s, value}};
}

bool validOperation(const QJsonObject& value) {
    const auto op = value.value(u"op"_s).toString();
    return op == u"reset" ? !value.contains(u"value"_s)
                          : (op == u"set" || op == u"clear") && value.contains(u"value"_s);
}

QJsonValue resolved(const storage::ConfigurationStore& store, const QString& key,
                    const QJsonValue& operation) {
    const auto object = operation.toObject();
    if (object.value(u"op"_s).toString() == u"reset")
        return storage::cloudSettingValue(key, store.defaultValue(key));
    return object.value(u"value"_s);
}

QJsonValue localValue(const QString& key, const QJsonValue& cloud, const Values& current) {
    return storage::cloudSettingLocalValue(key, cloud, current.value(key),
                                           current.value(u"tray/custom_icon"_s).toString());
}

struct PendingEdit {
    QJsonObject operation;
    QJsonValue base = undefined;
    QJsonValue fallback = undefined;
    quint64 sequence = 0;
    bool ifMissing = false;
    QString rejection;
    QString rejectedRevision;

    [[nodiscard]] QJsonObject serialize() const {
        QJsonObject result{{u"operation"_s, operation}, {u"sequence"_s, QString::number(sequence)}};
        result.insert(u"base"_s, base);
        result.insert(u"fallback"_s, fallback);
        if (ifMissing)
            result.insert(u"if_missing"_s, true);
        if (!rejection.isEmpty()) {
            result.insert(u"rejection"_s, rejection);
            result.insert(u"rejected_revision"_s, rejectedRevision);
        }
        return result;
    }

    static PendingEdit read(const QJsonObject& object) {
        return {object.value(u"operation"_s).toObject(),
                object.value(u"base"_s),
                object.value(u"fallback"_s),
                object.value(u"sequence"_s).toString().toULongLong(),
                object.value(u"if_missing"_s).toBool(),
                object.value(u"rejection"_s).toString(),
                object.value(u"rejected_revision"_s).toString()};
    }

    [[nodiscard]] bool ready(const QString& revision) const {
        return rejection.isEmpty() ||
               (rejection == u"snapshot_too_large" && rejectedRevision != revision);
    }
};

using EditMap = QMap<QString, PendingEdit>;

QJsonObject serializeEdits(const EditMap& edits) {
    QJsonObject result;
    for (auto it = edits.cbegin(); it != edits.cend(); ++it)
        result.insert(it.key(), it.value().serialize());
    return result;
}

EditMap readEdits(const QJsonObject& object) {
    EditMap result;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto edit = PendingEdit::read(it.value().toObject());
        if ((storage::cloudSettingSupported(it.key()) || storage::legacyMouseCloudKey(it.key())) &&
            edit.sequence != 0 && validOperation(edit.operation))
            result.insert(it.key(), edit);
    }
    return result;
}

QSet<QString> readKeySet(const QJsonValue& value) {
    QSet<QString> keys;
    for (const auto key : value.toArray())
        keys.insert(key.toString());
    return keys;
}

QJsonArray writeKeySet(const QSet<QString>& keys) {
    auto sorted = keys.values();
    sorted.sort();
    return QJsonArray::fromStringList(sorted);
}

struct Profile {
    bool enabled = true;
    bool joined = false;
    bool haveRead = false;
    bool platformInitialized = false;
    bool resume = false;
    QString revision;
    QString clientVersion;
    QDateTime lastSuccess;
    quint64 nextSequence = 0;
    QJsonObject values;
    QJsonObject seed;
    QJsonObject confirmed;
    QJsonObject layoutEdits;
    EditMap pending;
    EditMap sent;
    QSet<QString> localOnly;
    // Null means use the confirmed cloud operation. A fallback also survives when
    // the server has no record for a rejected binding.
    QJsonObject deferredBindings;

    [[nodiscard]] QJsonObject serialize() const {
        return {{u"enabled"_s, enabled},
                {u"joined"_s, joined},
                {u"read"_s, haveRead},
                {u"platform_initialized"_s, platformInitialized},
                {u"resume"_s, resume},
                {u"revision"_s, revision},
                {u"client_version"_s, clientVersion},
                {u"last_success"_s, lastSuccess.toString(Qt::ISODateWithMs)},
                {u"sequence"_s, QString::number(nextSequence)},
                {u"values"_s, values},
                {u"seed"_s, seed},
                {u"confirmed"_s, confirmed},
                {u"layout_edits"_s, layoutEdits},
                {u"pending"_s, serializeEdits(pending)},
                {u"sent"_s, serializeEdits(sent)},
                {u"deferred_bindings"_s, deferredBindings},
                {u"local_only"_s, writeKeySet(localOnly)}};
    }

    static Profile read(const QJsonObject& object) {
        Profile profile;
        profile.enabled = object.value(u"enabled"_s).toBool(true);
        profile.joined = object.value(u"joined"_s).toBool();
        profile.haveRead = object.value(u"read"_s).toBool();
        profile.platformInitialized = object.value(u"platform_initialized"_s).toBool();
        profile.resume = object.value(u"resume"_s).toBool();
        profile.revision = object.value(u"revision"_s).toString();
        profile.clientVersion = object.value(u"client_version"_s).toString();
        profile.lastSuccess =
            QDateTime::fromString(object.value(u"last_success"_s).toString(), Qt::ISODateWithMs);
        profile.nextSequence = object.value(u"sequence"_s).toString().toULongLong();
        profile.values = object.value(u"values"_s).toObject();
        profile.seed = object.value(u"seed"_s).toObject();
        profile.confirmed = object.value(u"confirmed"_s).toObject();
        profile.layoutEdits = object.value(u"layout_edits"_s).toObject();
        profile.pending = readEdits(object.value(u"pending"_s).toObject());
        profile.sent = readEdits(object.value(u"sent"_s).toObject());
        const auto deferred = object.value(u"deferred_bindings"_s);
        profile.deferredBindings = deferred.toObject();
        for (const auto& key : readKeySet(deferred))
            profile.deferredBindings.insert(key, missingOperation);
        profile.localOnly = readKeySet(object.value(u"local_only"_s));
        return profile;
    }
};

QStringView bindingScope(const QString& key) {
    const auto scope = QStringView(key).left(key.indexOf(u'/'));
    if (scope == u"global_mouse" || mouse::isKey(key))
        return scope;
    const auto* schema = storage::ConfigurationSchema::entry(key);
    if (schema && schema->valueKind == storage::ConfigurationValueKind::ShortcutList)
        return scope;
    return {};
}

struct BindingChange {
    QString scope;
    QJsonValue beforeMouse;
    QJsonValue afterMouse;
    shortcuts::ShortcutBindingList before;
    shortcuts::ShortcutBindingList after;
};

shortcuts::ShortcutPolicy shortcutPolicy(QStringView scope) {
    return scope == u"global_shortcuts" ? shortcuts::ShortcutPolicy::Native
                                        : shortcuts::ShortcutPolicy::Application;
}

bool bindingsDependOnEachOther(const QString& first, const BindingChange& a, const QString& second,
                               const BindingChange& b) {
    if (!shortcuts::localShortcutKeysMayConflict(first, second))
        return false;
    if (mouse::isKey(first)) {
        const auto overlaps = [](const QJsonValue& a, const QJsonValue& b) {
            return mouse::conflict(mouse::decode(a).value_or(mouse::Binding{}),
                                   mouse::decode(b).value_or(mouse::Binding{}));
        };
        return overlaps(a.afterMouse, b.beforeMouse) || overlaps(b.afterMouse, a.beforeMouse);
    }
    if (a.scope == u"global_mouse")
        return (a.afterMouse == b.beforeMouse && !a.afterMouse.toObject().isEmpty()) ||
               (b.afterMouse == a.beforeMouse && !b.afterMouse.toObject().isEmpty());
    const auto overlaps = [policy = shortcutPolicy(a.scope)](const auto& after,
                                                             const auto& before) {
        for (const auto& newBinding : after) {
            for (const auto& oldBinding : before) {
                if (shortcuts::bindingsConflict(newBinding, oldBinding, policy))
                    return true;
            }
        }
        return false;
    };
    return overlaps(a.after, b.before) || overlaps(b.after, a.before);
}

// Only changes that consume a binding another change releases must move together.
QList<QStringList> changeGroups(const Values& changes, const Values& before) {
    QMap<QString, BindingChange> bindings;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const auto scope = bindingScope(it.key());
        if (scope.isEmpty())
            continue;
        BindingChange entry;
        entry.scope = scope.toString();
        const auto previous = before.value(it.key());
        if (scope == u"global_mouse" || mouse::isKey(it.key())) {
            entry.beforeMouse = previous;
            entry.afterMouse = it.value();
        } else {
            const auto parse = [key = it.key(), policy = shortcutPolicy(scope)](const auto& value) {
                return shortcuts::shortcutBindingsFromJson(
                    value, storage::ConfigurationSchema::allowsModifierOnlyShift(key), -1, nullptr,
                    nullptr, key == u"screenshot_shortcuts/toggle_guides", policy);
            };
            entry.before = parse(previous);
            entry.after = parse(it.value());
        }
        bindings.insert(it.key(), std::move(entry));
    }
    QList<QStringList> groups;
    QSet<QString> assigned;
    const auto keys = changes.keys();
    for (const auto& key : keys) {
        if (assigned.contains(key))
            continue;
        assigned.insert(key);
        QStringList group{key};
        for (qsizetype i = 0; i < group.size(); ++i) {
            const auto current = group.at(i);
            const auto binding = bindings.constFind(current);
            if (binding == bindings.cend())
                continue;
            for (auto other = bindings.cbegin(); other != bindings.cend(); ++other) {
                if (assigned.contains(other.key()))
                    continue;
                if (bindingsDependOnEachOther(current, *binding, other.key(), *other)) {
                    group.append(other.key());
                    assigned.insert(other.key());
                }
            }
        }
        groups.append(group);
    }
    return groups;
}
} // namespace

class CloudSettingsSyncService::Impl {
  public:
    Impl(CloudSettingsSyncService& q, AccountSessionService& account,
         storage::ConfigurationStore& store, Options options)
        : q(q), account(account), store(store), options(std::move(options)) {
        // Worker commits journal synchronously. Network and storage completions use the
        // same store mutation lock when reconciling or checkpointing the account profile.
        store.afterPendingChanges([this] {
            restoreJournal();
            this->store.setCloudSyncJournal(
                [this](auto origin, const auto& before, const auto& after) {
                    return localEdits(origin, before, after);
                },
                [this] { return journalState(); });
        });
        for (auto* timer : {&edits, &retry, &tray, &preferenceRetry})
            timer->setSingleShot(true);
        QObject::connect(&preferenceRetry, &QTimer::timeout, &q,
                         [this] { persistSyncPreference(); });
        QObject::connect(&store, &storage::ConfigurationStore::writesResumed, &q, [this] {
            if (preferenceRetry.isActive())
                persistSyncPreference();
        });
        QObject::connect(&edits, &QTimer::timeout, &q, [this] {
            if (running)
                editsDue = true;
            else
                start();
        });
        QObject::connect(&retry, &QTimer::timeout, &q, [this] { start(); });
        QObject::connect(&tray, &QTimer::timeout, &q, [this] {
            unsupported = false;
            scheduleTray();
            start();
        });
        const auto changeAccount = [this](bool discard) {
            this->store.afterPendingChanges([&] { accountChanged(discard); });
        };
        QObject::connect(&account, &AccountSessionService::settingsSyncAccountChanged, &q,
                         changeAccount);
        QTimer::singleShot(0, &q, [changeAccount] { changeAccount(false); });
    }

    ~Impl() {
        store.setCloudSyncJournal({}, {});
        account.cancelSettingsSyncRequest();
    }

    [[nodiscard]] bool enabled() const {
        return owner.isEmpty() ? unspecificEnabled : profile.enabled;
    }

    [[nodiscard]] bool signedIn() const {
        return account.signedIn();
    }

    [[nodiscard]] bool checking() const {
        return running;
    }

    [[nodiscard]] QDateTime lastSuccess() const {
        return owner.isEmpty() ? QDateTime{} : lastCompletedSync;
    }

    void setEnabled(bool enable) {
        if (!account.signedIn() || enabled() == enable)
            return;
        if (owner.isEmpty()) {
            unspecificEnabled = enable;
            emit q.changed();
            return;
        }
        store.afterPendingChanges([&] {
            invalidate();
            profile.enabled = enable;
            profile.resume = true;
            persistSyncPreference();
        });
        emit q.changed();
        if (enable) {
            observeConnectivity();
            scheduleTray();
            start(true);
        }
    }

    void settingsOpened() {
        if (recentCheck.isValid() && recentCheck.elapsed() < options.settingsCheckIntervalMs)
            return;
        if (!retry.isActive())
            start();
    }

    void start(bool manual = false) {
        if (!sessionAvailable)
            return;
        store.afterPendingChanges([&] { startCheck(manual); });
    }

  private:
    void restoreJournal() {
        const auto state = store.cloudSyncState();
        haveBaseline = state.value(u"version"_s).toInt() == 1;
        if (haveBaseline) {
            deferredOcrModel = state.value(deferredOcrModelKey).toString();
            signedOut = state.value(u"signed_out"_s).toObject();
            signedOutLocalOnly = readKeySet(state.value(u"signed_out_local_only"_s));
            profiles = state.value(u"profiles"_s).toObject();
            owner = state.value(u"owner"_s).toString();
            profile = Profile::read(profiles.take(owner).toObject());
            lastCompletedSync = profile.lastSuccess;
        }
    }

    void startCheck(bool manual) {
        if (!sessionAvailable || !enabled() ||
            (owner.isEmpty() && !account.settingsSyncAccount().canRestore))
            return;
        if (manual) {
            forceEdits = true;
            retry.stop();
        }
        if (running || retry.isActive())
            return;
        if (!account.signedIn()) {
            if (account.settingsSyncAccount().canRestore) {
                account.restoreSettingsSyncSession();
                scheduleRetry();
            }
            return;
        }
        if (owner.isEmpty() || (unsupported && !manual))
            return;
        if (restoreNeeded && !restoreProfileValues()) {
            scheduleRetry();
            return;
        }
        if (!store.isWritable()) {
            scheduleRetry();
            return;
        }
        running = true;
        editsDue = false;
        rounds = 0;
        const bool pendingMouseMigration = std::any_of(
            profile.pending.keyBegin(), profile.pending.keyEnd(), storage::legacyMouseCloudKey);
        readCloud(profile.resume || pendingMouseMigration || !profile.sent.isEmpty() ||
                  profile.clientVersion != QCoreApplication::applicationVersion());
    }

    // Disk retries also run while sync is paused. Always save the latest account profiles.
    void persistSyncPreference() {
        preferenceRetry.stop();
        bool saved = false;
        store.afterPendingChanges([&] { saved = restoreNeeded ? restoreProfileValues() : save(); });
        if (!saved) {
            preferenceRetry.start(options.retryInitialMs);
            return;
        }
        store.flushAsync(&q, [this](auto result) {
            if (!result.success)
                preferenceRetry.start(options.retryInitialMs);
        });
    }

    void observeConnectivity() {
        if (!options.observeConnectivity || std::exchange(connectivityObserved, true) ||
            !QNetworkInformation::loadDefaultBackend())
            return;
        if (auto* information = QNetworkInformation::instance()) {
            QObject::connect(information, &QNetworkInformation::reachabilityChanged, &q,
                             [this](QNetworkInformation::Reachability reachability) {
                                 if (reachability != QNetworkInformation::Reachability::Online &&
                                     reachability != QNetworkInformation::Reachability::Local &&
                                     reachability != QNetworkInformation::Reachability::Site)
                                     return;
                                 unsupported = false;
                                 retry.stop();
                                 start();
                             });
        }
    }

    void invalidate() {
        ++generation;
        account.cancelSettingsSyncRequest();
        running = false;
        forceEdits = false;
        editsDue = false;
        profile.lastSuccess = lastCompletedSync;
        unsupported = false;
        failures = 0;
        edits.stop();
        retry.stop();
        tray.stop();
        recentCheck.invalidate();
    }

    void scheduleTray() {
        if (!sessionAvailable || !enabled() || !account.signedIn() || owner.isEmpty())
            return;
        const int range = std::max(1, options.trayIntervalMs / 10);
        tray.start(options.trayIntervalMs + QRandomGenerator::global()->bounded(-range, range));
    }

    void scheduleRetry() {
        const int multiplier = 1 << std::min(failures, 8);
        ++failures;
        const int timeout = std::min(options.retryInitialMs * multiplier, options.trayIntervalMs);
        retry.start(std::max(1, timeout));
    }

    [[nodiscard]] bool flush() {
        return !store.isDirty() || store.flushNow().success;
    }

    void checkpoint(std::function<void(bool)> complete) {
        if (!store.isDirty()) {
            complete(true);
            return;
        }
        const auto checkpointGeneration = generation;
        store.flushAsync(&q,
                         [this, checkpointGeneration, complete = std::move(complete)](auto result) {
                             store.afterPendingChanges([&] {
                                 if (checkpointGeneration == generation && running)
                                     complete(result.success);
                             });
                         });
    }

    void ensureBaseline(const Values& before = {}) {
        if (haveBaseline)
            return;
        const auto current = store.snapshot();
        for (const auto& key : storage::cloudSettingKeys()) {
            if (storage::cloudSettingSupported(key))
                signedOut.insert(key, storage::cloudSettingValue(key, before.contains(key)
                                                                          ? before.value(key)
                                                                          : current.value(key)));
        }
        haveBaseline = true;
    }

    QJsonObject journalState() const {
        auto accounts = profiles;
        if (!owner.isEmpty())
            accounts.insert(owner, profile.serialize());
        return {{u"version"_s, 1},
                {u"signed_out"_s, signedOut},
                {u"signed_out_local_only"_s, writeKeySet(signedOutLocalOnly)},
                {deferredOcrModelKey, deferredOcrModel},
                {u"owner"_s, owner},
                {u"profiles"_s, accounts}};
    }

    bool save(Values remote = {}) {
        const auto colorToggle = remote.constFind(u"drawing/sync_tool_colors"_s);
        if (colorToggle != remote.cend() && colorToggle.value() != store.value(colorToggle.key()) &&
            !storage::applyDrawingColorPolicy(store, remote))
            return false;
        const auto previousOcrModel = deferredOcrModel;
        if (remote.contains(ocrModelKey))
            deferredOcrModel = remote.value(ocrModelKey).toString();
        const bool saved = store.setCloudSyncState(journalState(), remote);
        if (!saved)
            deferredOcrModel = previousOcrModel;
        return saved;
    }

    void accountChanged(bool discard) {
        const auto current = account.settingsSyncAccount();
        const auto key = accountKey(current);
        sessionAvailable = current.authenticated || current.canRestore;
        const bool waitingForId = key.isEmpty() && !current.authenticated && current.canRestore;
        const bool sameOwner = key == owner || (waitingForId && !discard);
        if (discard || !sameOwner) {
            invalidate();
            if (!owner.isEmpty()) {
                if (discard || !key.isEmpty()) {
                    for (auto it = profile.pending.cbegin(); it != profile.pending.cend(); ++it)
                        profile.localOnly.insert(it.key());
                    profile.pending.clear();
                    profile.sent.clear();
                }
                profile.resume = true;
                profiles.insert(owner, profile.serialize());
            }
            const auto oldOwner = std::exchange(owner, key);
            if (!oldOwner.isEmpty() || !owner.isEmpty()) {
                ensureBaseline();
                if (owner.isEmpty()) {
                    profile = {};
                    restoreNeeded = false;
                } else if (profiles.contains(owner)) {
                    profile = Profile::read(profiles.take(owner).toObject());
                } else {
                    profile = {};
                    profile.values = signedOut;
                    profile.seed = signedOut;
                    profile.localOnly = signedOutLocalOnly;
                }
                if (!(owner.isEmpty() ? save() : restoreProfileValues()))
                    preferenceRetry.start(options.retryInitialMs);
            }
            if (discard)
                static_cast<void>(flush());
            lastCompletedSync = profile.lastSuccess;
            unspecificEnabled = true;
        }
        emit q.changed();
        const bool restored = current.authenticated && !wasAuthenticated;
        wasAuthenticated = current.authenticated;
        if (sessionAvailable && enabled())
            observeConnectivity();
        if (enabled() && (discard || !sameOwner || restored)) {
            retry.stop();
            scheduleTray();
            start();
        } else if (!current.authenticated && current.canRestore && enabled() && !retry.isActive()) {
            scheduleRetry();
        }
    }

    [[nodiscard]] bool restoreProfileValues() {
        const auto current = store.snapshot();
        // Older account profiles have only the shared mouse preferences. Restore a
        // complete set before cloud migration so it cannot seed another account's bindings.
        Values preferences;
        for (auto it = profile.values.constBegin(); it != profile.values.constEnd(); ++it)
            preferences.insert(it.key(), it.value());
        mouse::migrate(preferences);
        mouse::reconcile(preferences);

        Values patch;
        for (auto it = preferences.cbegin(); it != preferences.cend(); ++it) {
            const auto local = current.value(it.key());
            const auto target = localValue(it.key(), it.value(), current);
            if (!target.isUndefined() && target != local)
                patch.insert(it.key(), target);
        }
        restoreNeeded = !save(patch);
        return !restoreNeeded;
    }

    bool localEdits(ConfigurationWriteOrigin origin, const Values& before, const Values& after) {
        // Before the first account, the live configuration is the signed-out baseline.
        // An import can still mark implicit defaults as ineligible for first-use seeding.
        if (!haveBaseline && owner.isEmpty() && origin != ConfigurationWriteOrigin::ImportDefaults)
            return false;
        bool queueChanged = false;
        bool valuesChanged = false;
        for (auto it = after.cbegin(); it != after.cend(); ++it) {
            const auto& key = it.key();
            if (!storage::cloudSettingSupported(key))
                continue;
            if (!haveBaseline) {
                ensureBaseline(before);
                valuesChanged = true;
            }
            if (key == ocrModelKey && !deferredOcrModel.isEmpty()) {
                deferredOcrModel.clear();
                valuesChanged = true;
            }
            const auto value = storage::cloudSettingValue(key, it.value());
            const auto oldLocal = before.value(key);
            const bool exportTasks = key.startsWith(u"export_tasks/"_s);
            const auto previous = oldLocal != it.value() && (!owner.isEmpty() || exportTasks)
                                      ? storage::cloudSettingValue(key, oldLocal)
                                      : value;
            if (exportTasks && value == previous && oldLocal != it.value() &&
                origin != ConfigurationWriteOrigin::Reset)
                continue;
            if (owner.isEmpty()) {
                if (signedOut.value(key) != value) {
                    signedOut.insert(key, value);
                    valuesChanged = true;
                }
                const bool localOnly = origin == ConfigurationWriteOrigin::ImportDefaults;
                if (signedOutLocalOnly.contains(key) != localOnly) {
                    if (localOnly)
                        signedOutLocalOnly.insert(key);
                    else
                        signedOutLocalOnly.remove(key);
                    valuesChanged = true;
                }
                continue;
            }
            profile.values.insert(key, value);
            valuesChanged = true;
            const auto deferredFallback = profile.deferredBindings.take(key);
            if (!profile.enabled || origin == ConfigurationWriteOrigin::ImportDefaults) {
                profile.localOnly.insert(key);
                profile.pending.remove(key);
                profile.layoutEdits.remove(key);
                continue;
            }
            if (value.isUndefined()) {
                profile.pending.remove(key);
                profile.localOnly.insert(key);
                continue;
            }
            profile.localOnly.remove(key);
            const auto operation = operationFor(value, origin);
            const auto rawCloud = resolved(store, key, operationAt(profile.confirmed, key));
            if (storage::cloudSettingLayout(key) &&
                (profile.layoutEdits.contains(key) ||
                 (!rawCloud.isUndefined() &&
                  !storage::cloudSettingLayoutComplete(key, rawCloud)))) {
                auto override = profile.layoutEdits.value(key).toObject();
                if (override.isEmpty()) {
                    override.insert(u"base"_s, rawCloud);
                    override.insert(u"projection"_s, previous);
                }
                override.insert(u"operation"_s, operation);
                profile.layoutEdits.insert(key, override);
                profile.pending.remove(key);
                continue;
            }
            const auto fallback = deferredFallback.isArray() || deferredFallback.isObject()
                                      ? deferredFallback
                                      : previous;
            queueChanged = enqueue(key, operation, fallback) || queueChanged;
        }
        if (queueChanged)
            QMetaObject::invokeMethod(&q, [this, editGeneration = generation] {
                if (generation == editGeneration)
                    edits.start(options.editDelayMs);
            });
        return valuesChanged;
    }

    bool enqueue(const QString& key, const QJsonObject& operation, const QJsonValue& fallback) {
        auto it = profile.pending.find(key);
        if (it != profile.pending.end() && it->operation == operation && !it->ifMissing)
            return false;
        PendingEdit next;
        if (it != profile.pending.end() && !it->ifMissing) {
            next.base = it->base;
            next.fallback = it->fallback;
        } else {
            if (profile.haveRead)
                next.base = operationAt(profile.confirmed, key);
            next.fallback = fallback;
        }
        next.operation = operation;
        next.sequence = ++profile.nextSequence;
        profile.pending.insert(key, next);
        return true;
    }

    template <typename Callback>
    void request(const QUrlQuery& query, const QJsonObject& body, Callback receive) {
        const auto sentGeneration = generation;
        account.requestSettingsSync(query, body,
                                    [this, sentGeneration, receive = std::move(receive)](
                                        const QJsonObject& data, int status) {
                                        store.afterPendingChanges([&] {
                                            if (sentGeneration == generation && running)
                                                receive(data, status);
                                        });
                                    });
    }

    void readCloud(bool full) {
        if (++rounds > maximumRoundsPerCheck) {
            fail();
            return;
        }
        QUrlQuery query;
        query.addQueryItem(u"version"_s, QString::number(protocolVersion));
        query.addQueryItem(u"platform"_s, platform());
        if (!full && !profile.revision.isEmpty())
            query.addQueryItem(u"revision"_s, profile.revision);
        request(query, {}, [this, full](const QJsonObject& data, int status) {
            if (status == 404 || status == 405 || status == 501) {
                unsupported = true;
                recentCheck.start();
                finish();
                return;
            }
            if (status != 200 || data.value(u"version"_s).toInt() != protocolVersion ||
                !data.value(u"platform_initialized"_s).isBool()) {
                fail();
                return;
            }
            unsupported = false;
            const auto state = data.value(u"state"_s).toString();
            const auto revision = data.value(u"revision"_s).toString();
            const bool initialized = data.value(u"platform_initialized"_s).toBool();
            auto cloud = data.value(u"settings"_s);
            if (state == u"missing" && data.value(u"revision"_s).isNull() && !initialized &&
                cloud.isObject() && cloud.toObject().isEmpty()) {
                cloud = QJsonObject{};
            } else if (state == u"unchanged" && !full && !revision.isEmpty() && profile.haveRead &&
                       revision == profile.revision) {
                if (profile.layoutEdits.isEmpty() && profile.deferredBindings.isEmpty()) {
                    recentCheck.start();
                    if (profile.pending.isEmpty() && profile.platformInitialized)
                        success(true);
                    else
                        processUploads();
                    return;
                }
                cloud = profile.confirmed;
            } else if (state != u"snapshot" || revision.isEmpty() || revision.size() > 128 ||
                       !cloud.isObject()) {
                fail();
                return;
            }
            if (!acceptSnapshot(cloud.toObject(), revision, initialized)) {
                fail();
                return;
            }
            recentCheck.start();
            processUploads();
        });
    }

    QList<QStringList> pendingGroups() const {
        Values changes;
        Values before;
        for (auto it = profile.pending.cbegin(); it != profile.pending.cend(); ++it) {
            changes.insert(it.key(), resolved(store, it.key(), it->operation));
            const auto base = resolved(store, it.key(), it->base);
            before.insert(it.key(), base.isUndefined() ? it->fallback : base);
        }
        return changeGroups(changes, before);
    }

    Values reconcilePending(const QJsonObject& cloud) {
        // A lost acknowledgement can advance the base of a newer edit to the same key.
        for (auto it = profile.pending.begin(); it != profile.pending.end(); ++it) {
            const auto incoming = operationAt(cloud, it.key());
            const auto sent = profile.sent.constFind(it.key());
            if (sent != profile.sent.cend() && sent->operation == incoming.toObject() &&
                sent->sequence != it->sequence)
                it->base = incoming;
        }
        profile.sent.clear();
        Values resolvedEdits;
        for (const auto& group : pendingGroups()) {
            const bool conflict =
                std::any_of(group.cbegin(), group.cend(), [&](const QString& key) {
                    const auto& edit = *profile.pending.constFind(key);
                    const auto incoming = operationAt(cloud, key);
                    return edit.operation != incoming.toObject() &&
                           ((edit.ifMissing && !incoming.isNull()) ||
                            (!edit.base.isUndefined() && edit.base != incoming));
                });
            for (const auto& key : group) {
                auto it = profile.pending.find(key);
                const auto incoming = operationAt(cloud, key);
                if (conflict || it->operation == incoming.toObject()) {
                    resolvedEdits.insert(key, it->fallback);
                    profile.pending.erase(it);
                } else {
                    it->base = incoming;
                }
            }
        }
        return resolvedEdits;
    }

    void seedMissingKeys(const QJsonObject& cloud) {
        for (auto it = profile.seed.constBegin(); it != profile.seed.constEnd(); ++it) {
            const auto key = it.key();
            if (!storage::cloudSettingSupported(key) || cloud.contains(key) ||
                profile.pending.contains(key) || profile.localOnly.contains(key) ||
                profile.layoutEdits.contains(key))
                continue;
            const auto value = it.value();
            const auto fallback = storage::cloudSettingValue(key, store.defaultValue(key));
            if (value == fallback || value.isUndefined() ||
                (storage::cloudSettingLayout(key) &&
                 !storage::cloudSettingLayoutComplete(key, value)))
                continue;
            PendingEdit edit;
            edit.operation = operationFor(value, ConfigurationWriteOrigin::Explicit);
            edit.base = missingOperation;
            edit.fallback = fallback;
            edit.sequence = ++profile.nextSequence;
            edit.ifMissing = true;
            profile.pending.insert(key, edit);
        }
        profile.seed = {};
    }

    QJsonValue mergeLayout(const QString& key, const QJsonValue& raw, const QJsonValue& current) {
        const auto edit = profile.layoutEdits.value(key).toObject();
        if (edit.isEmpty())
            return raw;
        if (!storage::cloudSettingLayoutComplete(key, raw) ||
            !storage::cloudSettingLayoutComplete(key, edit.value(u"base"_s)))
            return undefined;
        const auto local = resolved(store, key, edit.value(u"operation"_s));
        const auto merged = detail::mergeCloudLayout(edit.value(u"base"_s), raw,
                                                     edit.value(u"projection"_s), local);
        if (merged.isUndefined() || !storage::cloudSettingLayoutComplete(key, merged))
            return undefined;
        profile.layoutEdits.remove(key);
        if (merged != raw)
            enqueue(key, operationFor(merged, ConfigurationWriteOrigin::Explicit), current);
        return merged;
    }

    Values validChanges(const Values& current, const Values& candidates) {
        // Validate and stage each group against the results already selected for this batch.
        Values result;
        auto effective = current;
        for (const auto& group : changeGroups(candidates, current)) {
            Values changes;
            for (const auto& key : group)
                changes.insert(key, candidates.value(key));
            if (!bindingScope(group.first()).isEmpty() &&
                !storage::cloudSettingShortcutChangesValid(effective, changes))
                continue;
            for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
                effective.insert(it.key(), it.value());
                result.insert(it.key(), it.value());
            }
        }
        return result;
    }

    void migrateMousePreferences(const QJsonObject& cloud, const Values& current,
                                 Values& candidates) {
        for (auto category : {mouse::Category::Screenshot, mouse::Category::Pinned}) {
            const auto defaults = mouse::factoryValues(category);
            const auto present = [&](const auto& keys, const auto& predicate) {
                return std::any_of(keys.keyBegin(), keys.keyEnd(), predicate);
            };
            const QString prefix =
                category == mouse::Category::Screenshot ? u"screenshot/"_s : u"pin_to_screen/"_s;
            Values legacy;
            for (const auto& suffix : {u"double_click_action"_s, u"middle_mouse_button_action"_s}) {
                const QString key = prefix + suffix;
                // Reconciliation has already dropped acknowledged or conflicting legacy edits.
                const auto edit = profile.pending.take(key);
                const auto operation =
                    edit.operation.isEmpty() ? cloud.value(key).toObject() : edit.operation;
                if (!operation.isEmpty()) {
                    legacy[key] = operation.value(u"op"_s) == u"reset"_s
                                      ? store.defaultValue(key)
                                      : operation.value(u"value"_s);
                }
            }
            if (present(defaults, [&](const auto& key) { return cloud.contains(key); })) {
                // The platform already has modern settings. Missing cloud entries use
                // factory values unless this profile has recorded a modern local value.
                for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
                    const auto& key = it.key();
                    if (!cloud.contains(key) && !profile.values.contains(key) &&
                        !profile.pending.contains(key) && !profile.localOnly.contains(key) &&
                        it.value() != current.value(key))
                        candidates.insert(key, it.value());
                }
                continue;
            }
            const bool customized = present(defaults, [&](const auto& key) {
                return current.value(key) != defaults.value(key);
            });
            if (legacy.isEmpty() && !customized)
                continue;
            mouse::migrate(legacy);
            // A legacy profile is projected into the live store before the first read. Only
            // bindings actually recorded in the modern profile are evidence of a local choice.
            QList<mouse::Binding> localBindings;
            const auto keepLocal = [&](const QString& key) {
                return profile.pending.contains(key) || profile.localOnly.contains(key) ||
                       (profile.joined && profile.values.contains(key));
            };
            for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
                if (keepLocal(it.key())) {
                    if (const auto binding = mouse::decode(current.value(it.key())))
                        localBindings.append(*binding);
                }
            }
            for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
                const QString& key = it.key();
                if (profile.localOnly.contains(key) || profile.pending.contains(key))
                    continue;
                auto value = current.value(key);
                if (!keepLocal(key) && legacy.contains(key)) {
                    value = legacy.value(key);
                    const auto binding = mouse::decode(value);
                    if (binding && std::any_of(localBindings.cbegin(), localBindings.cend(),
                                               [&](const auto& local) {
                                                   return mouse::conflict(*binding, local);
                                               }))
                        value = mouse::encode({});
                }
                if (value != current.value(key))
                    candidates.insert(key, value);
                PendingEdit edit;
                edit.operation =
                    operationFor(value, value == it.value() ? ConfigurationWriteOrigin::Reset
                                                            : ConfigurationWriteOrigin::Explicit);
                edit.base = missingOperation;
                edit.fallback = it.value();
                edit.sequence = ++profile.nextSequence;
                edit.ifMissing = true;
                profile.pending.insert(key, edit);
            }
        }
    }

    bool acceptSnapshot(const QJsonObject& raw, const QString& revision, bool initialized,
                        Values resolvedEdits = {}) {
        const auto previous = profile;
        QJsonObject cloud;
        static const QSet<QString> knownKeys(storage::cloudSettingKeys().cbegin(),
                                             storage::cloudSettingKeys().cend());
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
            if (validOperation(it.value().toObject()))
                cloud.insert(it.key(), it.value());
            else if (knownKeys.contains(it.key()))
                return false;
        }
        profile.revision = revision;
        profile.platformInitialized = initialized;
        profile.haveRead = true;
        profile.clientVersion = QCoreApplication::applicationVersion();
        profile.confirmed = cloud;
        for (auto it = profile.deferredBindings.begin(); it != profile.deferredBindings.end();) {
            const QString key = it.key();
            const QJsonValue fallback = it.value();
            if (!storage::cloudSettingSupported(key) ||
                (fallback.isNull() && !cloud.contains(key))) {
                it = profile.deferredBindings.erase(it);
                continue;
            }
            if (!fallback.isNull() && !resolvedEdits.contains(key))
                resolvedEdits.insert(key, fallback);
            ++it;
        }
        for (auto it = profile.pending.begin(); it != profile.pending.end();) {
            const auto key = it.key();
            const auto value = resolved(store, key, operationAt(cloud, key));
            if (!storage::cloudSettingLayout(key) || value.isUndefined() ||
                storage::cloudSettingLayoutComplete(key, value)) {
                ++it;
                continue;
            }
            const auto base = resolved(store, key, operationAt(previous.confirmed, key));
            profile.layoutEdits.insert(key,
                                       QJsonObject{{u"base"_s, base.isUndefined() ? value : base},
                                                   {u"projection"_s, it->fallback},
                                                   {u"operation"_s, it->operation}});
            it = profile.pending.erase(it);
        }
        resolvedEdits.insert(reconcilePending(cloud));
        if (!profile.joined)
            seedMissingKeys(cloud);

        const auto current = store.snapshot();
        Values candidates;
        migrateMousePreferences(cloud, current, candidates);
        // A rolled-back swap member may never have had a cloud record.
        for (auto it = resolvedEdits.cbegin(); it != resolvedEdits.cend(); ++it) {
            if (cloud.contains(it.key()) || profile.pending.contains(it.key()))
                continue;
            const auto local = localValue(it.key(), it.value(), current);
            if (!local.isUndefined() && local != current.value(it.key()))
                candidates.insert(it.key(), local);
            else
                profile.deferredBindings.remove(it.key());
        }
        for (auto it = cloud.constBegin(); it != cloud.constEnd(); ++it) {
            const auto key = it.key();
            if (!storage::cloudSettingSupported(key) || profile.pending.contains(key) ||
                (previous.joined && !previous.resume &&
                 previous.clientVersion == profile.clientVersion && !resolvedEdits.contains(key) &&
                 it.value() == previous.confirmed.value(key) &&
                 !profile.layoutEdits.contains(key) && !profile.deferredBindings.contains(key)))
                continue;
            const auto oldLocal = current.value(key);
            auto value = resolved(store, key, it.value());
            if (profile.layoutEdits.contains(key))
                value = mergeLayout(key, value, oldLocal);
            const auto local = localValue(key, value, current);
            if (!local.isUndefined() && local != oldLocal)
                candidates.insert(key, local);
            else
                profile.deferredBindings.remove(key);
        }
        const auto changes = validChanges(current, candidates);
        for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
            if (changes.contains(it.key()))
                profile.deferredBindings.remove(it.key());
            else if (!bindingScope(it.key()).isEmpty()) {
                const auto fallback = resolvedEdits.value(it.key(), missingOperation);
                profile.deferredBindings.insert(it.key(), fallback.isUndefined() ? missingOperation
                                                                                 : fallback);
            }
        }
        for (auto it = changes.cbegin(); it != changes.cend(); ++it)
            profile.values.insert(it.key(), storage::cloudSettingValue(it.key(), it.value()));
        for (const auto& action : mouse::definitions()) {
            const auto key = action.key();
            if (!profile.values.contains(key) && storage::cloudSettingSupported(key))
                profile.values.insert(key, storage::cloudSettingValue(key, current.value(key)));
        }
        profile.joined = true;
        profile.resume = false;
        if (!save(changes)) {
            profile = previous;
            return false;
        }
        return true;
    }

    QJsonArray prepareUpload(bool allowExplicit) {
        QJsonArray groups;
        qsizetype totalBytes = 1024;
        for (const auto& group : pendingGroups()) {
            QJsonArray items;
            for (const auto& key : group) {
                const auto& edit = profile.pending.value(key);
                if ((!allowExplicit && !edit.ifMissing) || !edit.ready(profile.revision))
                    break;
                auto item = edit.operation;
                item.insert(u"key"_s, key);
                if (edit.ifMissing)
                    item.insert(u"if_missing"_s, true);
                items.append(item);
            }
            if (items.size() != group.size())
                continue;
            const QJsonObject entry{{u"changes"_s, items}};
            const auto bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact).size() + 1;
            if (bytes + 1024 > updateByteLimit) {
                for (const auto& key : group)
                    profile.pending[key].rejection = u"invalid_value"_s;
                continue;
            }
            if (totalBytes + bytes > updateByteLimit)
                break;
            for (const auto& key : group)
                profile.sent.insert(key, profile.pending.value(key));
            groups.append(entry);
            totalBytes += bytes;
        }
        return groups;
    }

    void processUploads() {
        const bool allowEdits = forceEdits || !edits.isActive();
        const auto groups = prepareUpload(allowEdits);
        if (groups.isEmpty() && profile.platformInitialized) {
            const bool outstanding = std::any_of(
                profile.pending.cbegin(), profile.pending.cend(), [allowEdits](const auto& edit) {
                    return allowEdits || edit.ifMissing || !edit.rejection.isEmpty();
                });
            if (outstanding || !profile.deferredBindings.isEmpty()) {
                if (!save()) {
                    fail();
                    return;
                }
                checkpoint([this](bool saved) {
                    if (saved)
                        finish();
                    else
                        fail();
                });
            } else {
                success();
            }
            return;
        }
        if (++rounds > maximumRoundsPerCheck || !save()) {
            fail();
            return;
        }
        const QJsonObject update{{u"version"_s, protocolVersion},
                                 {u"platform"_s, platform()},
                                 {u"base_revision"_s, profile.revision.isEmpty()
                                                          ? missingOperation
                                                          : QJsonValue(profile.revision)},
                                 {u"initialize"_s, !profile.platformInitialized},
                                 {u"groups"_s, groups}};
        checkpoint([this, update](bool saved) {
            if (!saved) {
                fail();
                return;
            }
            request({}, update, [this](const QJsonObject& data, int status) {
                if (status != 200 || data.value(u"version"_s).toInt() != protocolVersion) {
                    fail();
                    return;
                }
                const auto state = data.value(u"state"_s).toString();
                if (state == u"conflict") {
                    readCloud(true);
                    return;
                }
                if (state != u"applied" || !acceptUpload(data)) {
                    fail();
                    return;
                }
                processUploads();
            });
        });
    }

    bool acceptUpload(const QJsonObject& data) {
        const auto revision = data.value(u"revision"_s).toString();
        if (revision.isEmpty() || revision.size() > 128 ||
            !data.value(u"platform_initialized"_s).isBool() ||
            !data.value(u"accepted"_s).isArray() || !data.value(u"rejected"_s).isArray())
            return false;
        const auto accepted = readKeySet(data.value(u"accepted"_s));
        QMap<QString, QString> rejected;
        for (const auto item : data.value(u"rejected"_s).toArray()) {
            const auto object = item.toObject();
            const auto key = object.value(u"key"_s).toString();
            const auto reason = object.value(u"reason"_s).toString();
            if (!profile.sent.contains(key) || accepted.contains(key) || reason.isEmpty())
                return false;
            rejected.insert(key, reason);
        }
        if (accepted.size() + rejected.size() != profile.sent.size())
            return false;
        if (std::any_of(accepted.cbegin(), accepted.cend(),
                        [this](const auto& key) { return !profile.sent.contains(key); }))
            return false;

        const auto previous = profile;
        profile.revision = revision;
        profile.platformInitialized = data.value(u"platform_initialized"_s).toBool();
        Values resolvedEdits;
        for (auto it = profile.sent.cbegin(); it != profile.sent.cend(); ++it) {
            const auto key = it.key();
            auto pending = profile.pending.find(key);
            const bool isCurrent =
                pending != profile.pending.end() && pending->sequence == it->sequence;
            if (accepted.contains(key)) {
                profile.confirmed.insert(key, it->operation);
                if (isCurrent)
                    profile.pending.erase(pending);
                else if (pending != profile.pending.end())
                    pending->base = it->operation;
                continue;
            }
            if (!isCurrent)
                continue;
            const auto reason = rejected.value(key);
            if (reason == u"already_present" || reason == u"binding_conflict") {
                resolvedEdits.insert(key, it->fallback);
                profile.pending.erase(pending);
            } else {
                pending->rejection = reason;
                pending->rejectedRevision = accepted.isEmpty() ? revision : previous.revision;
            }
        }
        profile.sent.clear();
        const bool saved =
            resolvedEdits.isEmpty() && profile.deferredBindings.isEmpty()
                ? save()
                : acceptSnapshot(profile.confirmed, revision, profile.platformInitialized,
                                 std::move(resolvedEdits));
        if (!saved)
            profile = previous;
        return saved;
    }

    void success(bool unchanged = false) {
        profile.lastSuccess = options.utcNow().toUTC();
        const bool timestampOnly = unchanged && lastCompletedSync.isValid() && !store.isDirty();
        if (!(timestampOnly ? store.deferCloudSyncStatus() : save())) {
            profile.lastSuccess = lastCompletedSync;
            fail();
            return;
        }
        checkpoint([this](bool saved) {
            if (!saved) {
                profile.lastSuccess = lastCompletedSync;
                save();
                fail();
                return;
            }
            lastCompletedSync = profile.lastSuccess;
            recentCheck.start();
            finish();
            emit q.changed();
        });
    }

    void fail() {
        running = false;
        forceEdits = false;
        if (enabled() && sessionAvailable)
            scheduleRetry();
    }

    void finish() {
        running = false;
        const bool expedite =
            std::exchange(forceEdits, false) && edits.isActive() && !profile.pending.isEmpty();
        failures = 0;
        retry.stop();
        const bool due = std::exchange(editsDue, false) && !profile.pending.isEmpty();
        if (expedite)
            edits.stop();
        if (due || expedite)
            QTimer::singleShot(0, &q, [this, expedite] { start(expedite); });
    }

    CloudSettingsSyncService& q;
    AccountSessionService& account;
    storage::ConfigurationStore& store;
    Options options;
    QTimer edits;
    QTimer retry;
    QTimer tray;
    QTimer preferenceRetry;
    QElapsedTimer recentCheck;
    QDateTime lastCompletedSync;
    bool haveBaseline = false;
    bool wasAuthenticated = false;
    bool sessionAvailable = false;
    bool connectivityObserved = false;
    bool unspecificEnabled = true;
    bool running = false;
    bool editsDue = false;
    bool forceEdits = false;
    bool unsupported = false;
    bool restoreNeeded = false;
    int failures = 0;
    int rounds = 0;
    quint64 generation = 0;
    QString owner;
    QString deferredOcrModel;
    QJsonObject signedOut;
    QSet<QString> signedOutLocalOnly;
    QJsonObject profiles;
    Profile profile;
};

CloudSettingsSyncService::CloudSettingsSyncService(AccountSessionService& account,
                                                   storage::ConfigurationStore& store,
                                                   QObject* parent)
    : CloudSettingsSyncService(account, store, {}, parent) {}

CloudSettingsSyncService::CloudSettingsSyncService(AccountSessionService& account,
                                                   storage::ConfigurationStore& store,
                                                   Options options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, account, store, std::move(options))) {}

CloudSettingsSyncService::~CloudSettingsSyncService() = default;

bool CloudSettingsSyncService::signedIn() const {
    return m_impl->signedIn();
}

bool CloudSettingsSyncService::enabled() const {
    return m_impl->enabled();
}

bool CloudSettingsSyncService::checking() const {
    return m_impl->checking();
}

bool CloudSettingsSyncService::shouldDeferOcrModelDownload(
    const storage::ConfigurationStore& store) {
    const auto model = store.cloudSyncState().value(deferredOcrModelKey).toString();
    return !model.isEmpty() && model == store.value(ocrModelKey).toString();
}

QDateTime CloudSettingsSyncService::lastSuccessfulSync() const {
    return m_impl->lastSuccess();
}

void CloudSettingsSyncService::setEnabled(bool enabled) {
    m_impl->setEnabled(enabled);
}

void CloudSettingsSyncService::syncNow() {
    m_impl->start(true);
}

void CloudSettingsSyncService::settingsOpened() {
    m_impl->settingsOpened();
}
} // namespace snow_shot::app
