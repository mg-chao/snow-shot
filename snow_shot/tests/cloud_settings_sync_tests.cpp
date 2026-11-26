// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/app/accountsessionservice.h"
#include "snow_shot/app/cloudsettingssyncservice.h"
#include "snow_shot/network/snowshotapiclient.h"
#include "snow_shot/storage/cloudsyncconfiguration.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/storage/exporttasks.h"
#include "snow_shot/presentation/translationpagecontroller.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUrlQuery>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
#include <utility>

void runCloudSettingsLayoutTests();

namespace {
using namespace Qt::StringLiterals;
using snow_shot::app::AccountSessionService;
using snow_shot::app::CloudSettingsSyncService;
using snow_shot::storage::ConfigurationStore;
using snow_shot::storage::ConfigurationWriteOrigin;

const auto theme = u"interface/theme_mode"_s;
const auto font = u"interface/app_font_size"_s;
const auto delay = u"screenshot/delay_seconds"_s;
const auto tasks = u"export_tasks/screenshot_copy"_s;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void waitUntil(const std::function<bool()>& done, const char* message) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 4000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(done(), message);
}

class Service {
  public:
    QTcpServer listener;
    QMap<QString, QJsonObject> snapshots;
    QMap<QString, int> revisions;
    int reads = 0;
    int writes = 0;
    int unchanged = 0;
    int unavailable = 0;
    QString permanentlyRejectedKey;
    bool holdRead = false;
    bool holdWrite = false;
    QList<std::function<void()>> held;
    QJsonObject lastUpdate;

    Service() {
        require(listener.listen(QHostAddress::LocalHost), "fake account server listens");
        QObject::connect(&listener, &QTcpServer::newConnection, &listener, [this] {
            while (auto* connection = listener.nextPendingConnection()) {
                QObject::connect(connection, &QTcpSocket::disconnected, connection,
                                 &QObject::deleteLater);
                QObject::connect(
                    connection, &QTcpSocket::readyRead, connection,
                    [this, connection, buffer = QByteArray{}]() mutable {
                        buffer += connection->readAll();
                        const auto end = buffer.indexOf("\r\n\r\n");
                        if (end < 0)
                            return;
                        const auto headers = buffer.left(end).split('\n');
                        qsizetype size = 0;
                        QString account;
                        for (const auto& header : headers) {
                            if (header.toLower().startsWith("content-length:"))
                                size = header.mid(header.indexOf(':') + 1).trimmed().toInt();
                            if (header.startsWith("Authorization: Bearer token-"))
                                account = QString::fromUtf8(header.sliced(28).trimmed());
                        }
                        if (buffer.size() < end + 4 + size)
                            return;
                        const auto request = headers.first().split(' ');
                        const auto data =
                            QJsonDocument::fromJson(buffer.sliced(end + 4, size)).object();
                        handle(connection, request.at(0), QUrl::fromEncoded(request.at(1)), account,
                               data);
                        buffer.clear();
                    });
            }
        });
    }

    [[nodiscard]] QString origin() const {
        return u"http://127.0.0.1:"_s + QString::number(listener.serverPort());
    }

    void set(const QString& account, const QString& key, const QJsonValue& value) {
        snapshots[account].insert(key, QJsonObject{{u"op"_s, u"set"_s}, {u"value"_s, value}});
        ++revisions[account];
    }

    [[nodiscard]] QJsonObject operation(const QString& account, const QString& key) const {
        return snapshots.value(account).value(key).toObject();
    }

    void release() {
        const auto responses = std::exchange(held, {});
        for (const auto& respond : responses)
            respond();
    }

  private:
    static void respond(QTcpSocket* socket, int status, const QJsonObject& data) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState)
            return;
        const auto payload = QJsonDocument(QJsonObject{{u"code"_s, 0}, {u"data"_s, data}})
                                 .toJson(QJsonDocument::Compact);
        socket->write(
            "HTTP/1.1 " + QByteArray::number(status) +
            " Reply\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
            QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
        socket->disconnectFromHost();
    }

    QJsonObject snapshot(const QString& account, const QString& clientRevision) {
        if (!snapshots.contains(account))
            return {{u"version"_s, 1},
                    {u"state"_s, u"missing"_s},
                    {u"revision"_s, QJsonValue()},
                    {u"platform_initialized"_s, false},
                    {u"settings"_s, QJsonObject{}}};
        const auto revision = QString::number(revisions.value(account));
        const bool matches = clientRevision == revision;
        QJsonObject data{{u"version"_s, 1},
                         {u"state"_s, matches ? u"unchanged"_s : u"snapshot"_s},
                         {u"revision"_s, revision},
                         {u"platform_initialized"_s, true}};
        if (!matches)
            data.insert(u"settings"_s, snapshots.value(account));
        else
            ++unchanged;
        return data;
    }

    QJsonObject update(const QString& account, const QJsonObject& input) {
        const bool exists = snapshots.contains(account);
        const auto expected =
            exists ? QJsonValue(QString::number(revisions.value(account))) : QJsonValue();
        if (input.value(u"base_revision"_s) != expected)
            return {{u"version"_s, 1},
                    {u"state"_s, u"conflict"_s},
                    {u"revision"_s, expected},
                    {u"platform_initialized"_s, exists}};
        QJsonArray accepted;
        QJsonArray rejected;
        bool changed = !exists;
        auto& values = snapshots[account];
        const auto resolvedValue = [](const QString& key, const QJsonObject& operation) {
            return operation.value(u"op"_s) == u"reset"_s
                       ? snow_shot::storage::ConfigurationSchema::defaultValue(key)
                       : operation.value(u"value"_s);
        };
        QMap<QString, QJsonValue> current;
        for (auto it = values.begin(); it != values.end(); ++it)
            current.insert(it.key(), resolvedValue(it.key(), it.value().toObject()));
        for (const auto group : input.value(u"groups"_s).toArray()) {
            QJsonObject operations;
            QMap<QString, QJsonValue> changes;
            QString reason;
            for (const auto raw : group.toObject().value(u"changes"_s).toArray()) {
                auto item = raw.toObject();
                const auto key = item.take(u"key"_s).toString();
                const bool ifMissing = item.take(u"if_missing"_s).toBool();
                if (reason.isEmpty()) {
                    if (key == permanentlyRejectedKey)
                        reason = u"invalid_value"_s;
                    else if (ifMissing && values.contains(key))
                        reason = u"already_present"_s;
                }
                if (!current.contains(key))
                    current.insert(key, QJsonValue::Undefined);
                operations.insert(key, item);
                changes.insert(key, resolvedValue(key, item));
            }
            if (reason.isEmpty() &&
                !snow_shot::storage::cloudSettingShortcutChangesValid(current, changes))
                reason = u"binding_conflict"_s;
            for (auto it = operations.begin(); it != operations.end(); ++it) {
                if (!reason.isEmpty()) {
                    rejected.append(QJsonObject{{u"key"_s, it.key()}, {u"reason"_s, reason}});
                } else {
                    changed = changed || values.value(it.key()) != it.value();
                    values.insert(it.key(), it.value());
                    current.insert(it.key(), changes.value(it.key()));
                    accepted.append(it.key());
                }
            }
        }
        if (changed)
            ++revisions[account];
        return {{u"version"_s, 1},
                {u"state"_s, u"applied"_s},
                {u"revision"_s, QString::number(revisions.value(account))},
                {u"platform_initialized"_s, true},
                {u"accepted"_s, accepted},
                {u"rejected"_s, rejected}};
    }

    void handle(QTcpSocket* connection, const QByteArray& method, const QUrl& url,
                const QString& account, const QJsonObject& body) {
        if (url.path() == u"/api/v2/auth/login") {
            const auto id = body.value(u"username"_s).toString();
            respond(connection, 200,
                    {{u"account_id"_s, id},
                     {u"access_token"_s, u"token-"_s + id},
                     {u"refresh_token"_s, u"refresh-"_s + id}});
            return;
        }
        if (url.path() != u"/api/v2/account/settings-sync") {
            respond(connection, 200, {});
            return;
        }
        require(!account.isEmpty(), "settings transport carries its account bearer token");
        const bool read = method == "GET";
        if (read) {
            ++reads;
            require(QUrlQuery(url).queryItemValue(u"version"_s) == u"1",
                    "GET advertises protocol version");
        } else {
            ++writes;
            lastUpdate = body;
        }
        auto send = [this, socket = QPointer(connection), read, url, account, body] {
            if (unavailable) {
                respond(socket, unavailable, {});
                return;
            }
            const auto data = read ? snapshot(account, QUrlQuery(url).queryItemValue(u"revision"_s))
                                   : update(account, body);
            respond(socket, 200, data);
        };
        if ((read && holdRead) || (!read && holdWrite))
            held.append(std::move(send));
        else
            send();
    }
};

class Device {
  public:
    QDateTime now = QDateTime::fromString(u"2025-06-15T12:00:00Z"_s, Qt::ISODate);
    ConfigurationStore store;
    SnowShotApiClient client;
    AccountSessionService account;
    CloudSettingsSyncService sync;

    Device(const QString& path, const Service& service, bool writable = true,
           int retryInitialMs = 10'000)
        : store(path, true, writable, 100), client(service.origin()),
          account(client, {.persistSecrets = false}), sync(account, store,
                                                           {.editDelayMs = 40,
                                                            .trayIntervalMs = 1'800'000,
                                                            .retryInitialMs = retryInitialMs,
                                                            .observeConnectivity = false,
                                                            .utcNow = [this] { return now; }}) {}

    void signIn(const QString& id) {
        account.signIn(id, u"password"_s);
        waitUntil([this] { return account.signedIn(); }, "account signed in");
    }

    void signInAndSync(const QString& id) {
        signIn(id);
        waitUntil([this] { return sync.lastSuccessfulSync().isValid() && !sync.checking(); },
                  "complete the signed-in account's first sync");
    }

    void check() {
        waitUntil([this] { return !sync.checking(); }, "previous check settles");
        now = now.addSecs(7);
        sync.syncNow();
        waitUntil([this] { return sync.lastSuccessfulSync() == now && !sync.checking(); },
                  "manual check succeeds");
    }
};

void accountLifecycle(const QString& path) {
    Service service;
    service.set(u"A"_s, theme, u"dark"_s);
    service.set(u"A"_s, tasks, QJsonArray{u"auto_save"_s});
    Device device(path, service);
    require(device.store.setValue(theme, u"light"_s), "signed-out theme set");
    QCoreApplication::processEvents();
    device.sync.settingsOpened();
    device.sync.syncNow();
    require(!device.sync.checking() && service.reads == 0 && service.writes == 0 &&
                device.store.flushNow().success && device.store.cloudSyncState().isEmpty(),
            "a fresh signed-out client leaves the cloud journal empty");
    const QSet<QString> imported{theme};
    require(device.store.setValue(font, 22) &&
                device.store.applySnapshot({{theme, u"light"_s}}, 0, &imported) &&
                device.store.flushNow().success &&
                ConfigurationStore(path, true, false)
                    .cloudSyncState()
                    .value(u"signed_out_local_only"_s)
                    .toArray()
                    .contains(font),
            "a signed-out import checkpoints implicit defaults before the first account");
    service.holdRead = true;
    device.signIn(u"A"_s);
    waitUntil([&] { return service.held.size() == 1; }, "first sync held");
    require(device.store.setValue(font, 17) && device.store.setValue(font, 19),
            "edits arrive during first read");
    service.holdRead = false;
    service.release();
    waitUntil(
        [&] {
            return device.sync.lastSuccessfulSync().isValid() &&
                   service.operation(u"A"_s, font).value(u"value"_s).toInt() == 19 &&
                   !device.sync.checking();
        },
        "first sync and the debounced edit succeed");
    require(device.store.value(theme) == QJsonValue(u"dark"_s), "cloud theme adopted");
    require(service.operation(u"A"_s, font).value(u"value"_s).toInt() == 19 && service.writes == 1,
            "first-session edits coalesce and survive the initial read");

    const int reads = service.reads;
    const int unchanged = service.unchanged;
    device.now = device.now.addSecs(7);
    device.sync.syncNow();
    device.sync.syncNow();
    waitUntil(
        [&] { return device.sync.lastSuccessfulSync() == device.now && !device.sync.checking(); },
        "unchanged check succeeds");
    require(service.reads == reads + 1 && service.unchanged == unchanged + 1,
            "manual clicks share one revision-only read");
    device.sync.settingsOpened();
    require(!device.sync.checking(), "Settings can reuse a recent confirmed check");

    service.holdRead = true;
    device.sync.syncNow();
    waitUntil([&] { return service.held.size() == 1; }, "manual read held");
    device.sync.setEnabled(false);
    service.set(u"A"_s, theme, u"system"_s);
    service.holdRead = false;
    service.release();
    require(!device.sync.enabled() && device.store.value(theme) == QJsonValue(u"dark"_s),
            "pausing protects local state against an earlier response");
    require(device.store.setValue(theme, u"light"_s) && device.store.setValue(delay, 8),
            "paused edits work locally");
    const auto previous = device.sync.lastSuccessfulSync();
    device.now = device.now.addSecs(7);
    device.sync.setEnabled(true);
    waitUntil([&] { return device.sync.lastSuccessfulSync() == device.now; },
              "resuming checks the latest cloud");
    require(previous < device.sync.lastSuccessfulSync() &&
                device.store.value(theme) == QJsonValue(u"system"_s) &&
                device.store.value(delay).toInt() == 8 &&
                service.operation(u"A"_s, delay).isEmpty(),
            "resuming adopts present cloud keys and preserves absent paused edits locally");
    require(device.store.setValue(delay, 8), "explicit choice of retained value");
    waitUntil([&] { return !service.operation(u"A"_s, delay).isEmpty(); },
              "re-edit admits previously paused key");

    device.account.signOut();
    require(device.store.value(theme) == QJsonValue(u"system"_s), "sign-out keeps active settings");
    require(device.store.setValue(font, device.store.value(font)) &&
                device.store.flushNow().success,
            "record an explicit signed-out choice in the independent baseline");
    require(device.store.setValue(font, device.store.value(font)) && !device.store.isDirty(),
            "repeating a signed-out choice needs no journal checkpoint");
    require(device.store.setValue(tasks, QJsonArray{u"auto_save"_s, u"upload_to_cloud"_s}),
            "only automatic upload changes while signed out");
    require(device.store.setValue(font, 21), "signed-out font change");
    device.now = device.now.addSecs(7);
    device.signIn(u"B"_s);
    waitUntil([&] { return device.sync.lastSuccessfulSync() == device.now; },
              "second account initialized");
    require(device.store.value(theme) == QJsonValue(u"light"_s) &&
                device.store.value(font).toInt() == 21,
            "new account starts from independent signed-out preferences");
    require(service.operation(u"B"_s, delay).isEmpty(),
            "first account's pending or local edits do not leak");
    require(service.operation(u"B"_s, tasks).isEmpty() &&
                device.store.value(tasks) == QJsonArray{u"upload_to_cloud"_s},
            "the device keeps automatic upload and excludes the previous account's export tasks");
}

void unchangedChecksShareNextFlush(const QString& path) {
    Service service;
    Device device(path, service);
    device.signIn(u"A"_s);
    waitUntil([&] { return device.sync.lastSuccessfulSync().isValid() && !device.sync.checking(); },
              "first sync reaches its durable checkpoint");
    const auto persistedTime = [&] {
        const ConfigurationStore reloaded(path, true, false);
        const auto state = reloaded.cloudSyncState();
        const auto profile = state.value(u"profiles"_s)
                                 .toObject()
                                 .value(state.value(u"owner"_s).toString())
                                 .toObject();
        return QDateTime::fromString(profile.value(u"last_success"_s).toString(),
                                     Qt::ISODateWithMs);
    };
    const auto first = device.sync.lastSuccessfulSync();
    require(persistedTime() == first, "a substantive sync persists its success time");
    QFile file(path);
    require(file.open(QIODevice::ReadWrite) &&
                file.setFileTime(QDateTime::fromMSecsSinceEpoch(1'600'000'000'000LL),
                                 QFileDevice::FileModificationTime),
            "mark the last configuration write before polling");
    file.close();
    const auto modified = QFileInfo(path).lastModified();
    const int unchanged = service.unchanged;
    device.check();
    device.check();
    require(service.unchanged == unchanged + 2 && device.sync.lastSuccessfulSync() > first &&
                !device.store.isDirty() && persistedTime() == first &&
                QFileInfo(path).lastModified() == modified,
            "unchanged checks advance live status without rewriting configuration");
    require(device.store.flushNow().success && persistedTime() == device.now,
            "an explicit shutdown checkpoint includes the latest check time");
    device.check();
    const auto localKey = u"screen_recording/enable_microphone"_s;
    require(device.store.setValue(localKey, !device.store.value(localKey).toBool()),
            "a device-only preference schedules a normal save");
    waitUntil([&] { return !device.store.isDirty(); }, "normal debounced save completes");
    require(persistedTime() == device.now, "the normal save includes the deferred timestamp");
}

void signOutDuringStorageMove(const QString& path) {
    Service service;
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    require(device.store.setValue(font, 27), "edit immediately before suspending storage");
    device.store.suspendWrites(true);
    device.account.signOut();
    require(device.store.flushNow().success, "checkpoint accepted edits during suspension");
    const ConfigurationStore checkpoint(path, true, false);
    const auto state = checkpoint.cloudSyncState();
    const auto owner = state.value(u"owner"_s).toString();
    const auto profile = state.value(u"profiles"_s).toObject().value(owner).toObject();
    const auto pending = profile.value(u"pending"_s).toObject().value(font).toObject();
    require(!owner.isEmpty() && checkpoint.value(font).toInt() == 27 &&
                pending.value(u"operation"_s).toObject().value(u"value"_s).toInt() == 27,
            "the checkpoint keeps the preference and its original account's pending intent");
    device.store.suspendWrites(false);
    waitUntil([&] { return !device.store.isDirty(); }, "resumed storage persists sign-out");
    require(ConfigurationStore(path, true, false)
                .cloudSyncState()
                .value(u"owner"_s)
                .toString()
                .isEmpty(),
            "the deferred sign-out is saved after writes resume");
}

void workerEditsAcrossSignOut(const QString& path) {
    Service service;
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    std::thread writer([&] {
        require(device.store.setValue(font, 27), "worker commits a setting for account A");
    });
    writer.join();
    device.account.signOut();
    device.signIn(u"B"_s);
    device.check();
    require(device.store.value(font) == device.store.defaultValue(font) &&
                service.operation(u"B"_s, font).isEmpty(),
            "a worker write before sign-out cannot seed the next account");
    device.account.signOut();
    device.signIn(u"A"_s);
    device.check();
    require(device.store.value(font).toInt() == 27 && service.operation(u"A"_s, font).isEmpty(),
            "sign-out retains the worker edit in its original account's local profile");

    device.sync.setEnabled(false);
    std::thread pausedWriter(
        [&] { require(device.store.setValue(delay, 8), "worker edits a paused profile"); });
    pausedWriter.join();
    device.sync.setEnabled(true);
    device.check();
    require(device.store.value(delay).toInt() == 8 && service.operation(u"A"_s, delay).isEmpty(),
            "a queued edit made while paused stays local after enabling sync");
}

void workerEditDuringSnapshot(const QString& path) {
    Service service;
    service.set(u"A"_s, font, 22);
    service.holdRead = true;
    Device device(path, service);
    device.signIn(u"A"_s);
    waitUntil([&] { return service.held.size() == 1; }, "initial snapshot is in flight");
    bool edited = false;
    for (auto* reply : device.account.findChildren<QNetworkReply*>()) {
        if (reply->url().path().endsWith(u"/settings-sync")) {
            QObject::connect(reply, &QIODevice::readyRead, reply, [&] {
                if (std::exchange(edited, true))
                    return;
                std::thread writer(
                    [&] { require(device.store.setValue(font, 27), "worker edits font"); });
                writer.join();
            });
        }
    }
    service.holdRead = false;
    service.release();
    waitUntil(
        [&] {
            return edited && service.operation(u"A"_s, font).value(u"value"_s).toInt() == 27 &&
                   !device.sync.checking();
        },
        "the first snapshot preserves and uploads the concurrent edit");
    require(device.store.value(font).toInt() == 27, "the device and cloud agree after the upload");
}

void workerCheckpointSurvivesRestart(const QString& path) {
    Service service;
    {
        Device device(path, service);
        device.signIn(u"A"_s);
        device.check();
        std::thread writer([&] {
            require(device.store.setValue(font, 27) && device.store.flushNow().success,
                    "worker checkpoints its edit before the owner thread can process events");
        });
        writer.join();
    }
    Device restarted(path, service);
    restarted.signIn(u"A"_s);
    restarted.check();
    require(restarted.store.value(font).toInt() == 27 &&
                service.operation(u"A"_s, font).value(u"value"_s).toInt() == 27,
            "restart restores and uploads the worker's checkpointed preference");
}

void ocrModelDownloadOrigin(const QString& path) {
    const auto model = u"text_recognition/model_type"_s;
    Service service;
    service.set(u"A"_s, model, u"medium"_s);
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    const auto deferredOnDisk = [&] {
        ConfigurationStore checkpoint(path, true, false);
        return CloudSettingsSyncService::shouldDeferOcrModelDownload(checkpoint);
    };
    require(deferredOnDisk(), "a synchronized OCR model remains deferred across restart");
    require(device.store.setValue(model, u"medium"_s) && device.store.flushNow().success &&
                !deferredOnDisk(),
            "explicit model selection restores local hot start across restart");
}

void restorePausedAccount(const QString& path) {
    Service service;
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    device.sync.setEnabled(false);
    require(device.store.setValue(font, 24), "paused account chooses a font size");
    device.account.signOut();
    device.signIn(u"B"_s);
    device.check();
    require(device.store.setValue(font, 18), "second account chooses a different font size");
    device.account.signOut();
    device.store.suspendWrites(true);
    const int reads = service.reads;
    const int writes = service.writes;
    device.signIn(u"A"_s);
    require(!device.sync.enabled() && device.store.value(font).toInt() == 18,
            "profile restoration waits for writable storage");
    device.store.suspendWrites(false);
    waitUntil([&] { return device.store.value(font).toInt() == 24 && !device.store.isDirty(); },
              "resuming writes restores and persists the paused account's local settings");
    require(service.reads == reads && service.writes == writes,
            "restoring a paused account does not contact the sync endpoint");
}

void competingEdits(const QString& firstPath, const QString& secondPath) {
    Service service;
    Device first(firstPath, service);
    Device second(secondPath, service);
    first.signIn(u"A"_s);
    second.signIn(u"A"_s);
    waitUntil(
        [&] {
            return first.sync.lastSuccessfulSync().isValid() &&
                   second.sync.lastSuccessfulSync().isValid();
        },
        "two devices share an initialized snapshot");
    service.holdWrite = true;
    require(first.store.setValue(theme, u"dark"_s), "first device changes theme");
    waitUntil([&] { return service.held.size() == 1; }, "first upload is in flight");
    require(first.store.setValue(theme, u"light"_s), "same-key edit while upload in flight");
    service.holdWrite = false;
    service.release();
    waitUntil(
        [&] { return service.operation(u"A"_s, theme).value(u"value"_s).toString() == u"light"; },
        "acknowledging first edit preserves the newer edit");
    second.check();
    require(second.store.value(theme) == QJsonValue(u"light"_s),
            "second device receives final value");

    const auto scopedTasks =
        snow_shot::storage::exportTaskScopedKey(snow_shot::storage::ExportTrigger::ScreenshotCopy);
    const QJsonArray localUploads{u"upload_to_cloud"_s};
    const QJsonArray localScopedUploads{u"upload_to_cloud.focused_window"_s};
    require(second.store.setValues({{tasks, localUploads}, {scopedTasks, localScopedUploads}}),
            "device-only uploads enabled");
    require(first.store.setValues(
                {{tasks, QJsonArray{}},
                 {scopedTasks, QJsonArray{u"auto_save.scrolling"_s, u"upload_to_cloud.normal"_s}}}),
            "set a synced task and a device-only task on the first device");
    waitUntil([&] { return !service.operation(u"A"_s, scopedTasks).isEmpty(); },
              "eligible export task uploaded");
    require(service.operation(u"A"_s, tasks).isEmpty() &&
                service.operation(u"A"_s, scopedTasks).value(u"value"_s).toArray() ==
                    QJsonArray{u"auto_save.scrolling"_s},
            "only the eligible scoped option reaches the cloud");
    second.check();
    const QJsonArray remotelyScoped{u"auto_save.scrolling"_s, u"upload_to_cloud.focused_window"_s};
    require(second.store.value(tasks) == localUploads &&
                second.store.value(scopedTasks) == remotelyScoped,
            "sync combines scoped actions with this device's upload settings");
    service.set(u"A"_s, tasks, QJsonArray{u"pin_to_screen"_s});
    second.check();
    require(second.store.value(tasks) == QJsonArray{u"pin_to_screen"_s, u"upload_to_cloud"_s} &&
                second.store.value(scopedTasks) == remotelyScoped,
            "a legacy device can replace all-types choices without losing scoped tasks");
    first.check();
    require(first.store.setValues({{tasks, QJsonArray{}}, {scopedTasks, QJsonArray{}}},
                                  ConfigurationWriteOrigin::Reset),
            "export trigger reset clears both records");
    waitUntil(
        [&] {
            return service.operation(u"A"_s, tasks).value(u"op"_s).toString() == u"reset" &&
                   service.operation(u"A"_s, scopedTasks).value(u"op"_s).toString() == u"reset";
        },
        "both reset markers uploaded");
    second.check();
    require(second.store.value(tasks) == localUploads &&
                second.store.value(scopedTasks) == localScopedUploads,
            "receiving a reset preserves local auto-upload");

    require(first.store.setValue(theme, u"dark"_s), "first device edits the shared key");
    waitUntil(
        [&] { return service.operation(u"A"_s, theme).value(u"value"_s) == QJsonValue(u"dark"_s); },
        "first device commits its edit");
    require(second.store.setValues({{theme, u"system"_s}, {font, 23}}),
            "second device edits the same key and an independent key");
    second.check();
    require(second.store.value(theme) == QJsonValue(u"dark"_s) &&
                service.operation(u"A"_s, font).value(u"value"_s).toInt() == 23,
            "the committed result wins a same-key conflict and the independent edit uploads");
}

void pauseSurvivesWriteFailure(const QString& directory, bool suspend) {
    require(QDir().mkpath(directory), "create the pause test's configuration directory");
    const QString path = QDir(directory).filePath(u"config.json"_s);
    Service service;
    {
        Device device(path, service, true, 25);
        device.signIn(u"A"_s);
        device.check();
        const int reads = service.reads;
        if (suspend)
            device.store.suspendWrites(true);
        else
            device.store.relocate(path + u"/unavailable"_s);
        device.sync.setEnabled(false);
        require(!device.sync.enabled(), "pausing takes effect immediately");
        if (suspend) {
            device.store.suspendWrites(false);
            require(device.store.flushNow().success, "shutdown can checkpoint the resumed store");
        } else {
            waitUntil([&] { return !device.store.lastError().isEmpty(); },
                      "the asynchronous pause write fails");
            device.store.relocate(directory);
            waitUntil([&] { return !device.store.isDirty(); }, "the pause write is retried");
        }
        require(service.reads == reads, "retrying the preference does not start a cloud check");
    }
    Device restored(path, service);
    restored.signIn(u"A"_s);
    require(!restored.sync.enabled() && !restored.sync.checking(),
            "restarted account stays paused");
}

void conflictingShortcutSwap(const QString& firstPath, const QString& secondPath) {
    const auto first = u"global_shortcuts/screenshot_delay"_s;
    const auto second = u"global_shortcuts/screenshot"_s;
    const auto chord = [](const QString& key) {
        return QJsonArray{QJsonObject{{u"portable"_s, key}}};
    };
    Service service;
    service.set(u"A"_s, first, chord(u"Ctrl+F8"_s));
    Device editing(firstPath, service);
    Device competing(secondPath, service);
    editing.signIn(u"A"_s);
    editing.check();
    competing.signIn(u"A"_s);
    competing.check();
    const auto originalSecond = editing.store.value(second);
    service.holdWrite = true;
    require(editing.store.setValues(
                {{first, originalSecond}, {second, chord(u"Ctrl+F8"_s)}, {font, 22}}),
            "edit a shortcut swap and an independent preference");
    waitUntil([&] { return service.held.size() == 1; }, "the swap is in flight");
    service.holdWrite = false;
    require(competing.store.setValue(first, chord(u"Ctrl+F10"_s)),
            "competing device changes one action");
    competing.check();
    service.release();
    editing.check();
    require(editing.store.value(first) == chord(u"Ctrl+F10"_s) &&
                editing.store.value(second) == originalSecond &&
                service.operation(u"A"_s, second).isEmpty() &&
                service.operation(u"A"_s, font).value(u"value"_s).toInt() == 22,
            "a revision conflict rolls back the entire swap and retains independent edits");
}

void blockedShortcutRollback(const QString& path, bool hasCloudRecord) {
    const auto first = u"global_shortcuts/screenshot"_s;
    const auto newer = u"global_shortcuts/screenshot_delay"_s;
    const auto occupied = u"global_shortcuts/screenshot_fixed"_s;
    const QJsonArray previousChord{QJsonObject{{u"portable"_s, u"Ctrl+Shift+F17"_s}}};
    const QJsonArray rejectedChord{QJsonObject{{u"portable"_s, u"Ctrl+Shift+F18"_s}}};
    Service service;
    if (hasCloudRecord)
        service.set(u"A"_s, first, previousChord);
    service.set(u"A"_s, occupied, rejectedChord);
    QJsonValue previous;
    {
        Device device(path, service);
        device.signIn(u"A"_s);
        device.check();
        previous = device.store.value(first);
        require(!previous.toArray().isEmpty(), "the action starts with a binding to restore");
        require(device.store.setValue(occupied, QJsonArray{}, ConfigurationWriteOrigin::Internal),
                "simulate a local-only override of a cloud binding");

        const int previousWrites = service.writes;
        service.holdWrite = true;
        require(device.store.setValue(first, rejectedChord), "choose the cloud-occupied chord");
        waitUntil([&] { return service.held.size() == 1; }, "hold the rejected binding upload");
        require(device.store.setValue(newer, previous), "another action takes the previous chord");
        service.holdWrite = false;
        service.release();
        waitUntil([&] { return service.writes == previousWrites + 2 && !device.sync.checking(); },
                  "both edits are processed");

        if (hasCloudRecord) {
            require(device.store.value(first) == previous &&
                        device.store.value(newer) == QJsonArray{},
                    "once both edits are rejected, the original cloud binding is restored");
            return;
        }
        require(device.store.value(first) == rejectedChord &&
                    service.operation(u"A"_s, newer).value(u"value"_s) == previous &&
                    service.operation(u"A"_s, first).isEmpty(),
                "the accepted newer edit blocks rollback of the action without a cloud record");
        require(device.store.flushNow().success, "checkpoint the blocked rollback before restart");
    }

    Device restored(path, service);
    restored.signIn(u"A"_s);
    waitUntil([&] { return !restored.sync.checking(); }, "restore the account after restart");
    require(restored.store.setValue(newer, QJsonArray{}), "free the chord blocking the rollback");
    restored.check();
    require(restored.store.value(first) == previous && service.operation(u"A"_s, first).isEmpty(),
            "the stored rollback restores the original local binding without uploading it");
}

void unsupportedShortcutList(const QString& path) {
    const auto key = u"global_shortcuts/screenshot"_s;
    Service service;
    service.set(u"A"_s, key,
                QJsonArray{QJsonObject{{u"portable"_s, u"Ctrl+F9"_s}},
                           QJsonObject{{u"portable"_s, u"UnknownFutureKey"_s}}});
    service.set(u"A"_s, font, 23);
    Device device(path, service);
    const auto original = device.store.value(key);
    device.signIn(u"A"_s);
    device.check();
    require(device.store.value(key) == original && device.store.value(font).toInt() == 23,
            "an unsupported member preserves the entire local action while other changes sync");
}

void atomicShortcutSwap(const QString& path, const QString& first, const QString& second) {
    const QJsonArray firstChord{QJsonObject{{u"portable"_s, u"Ctrl+Shift+F17"_s}}};
    const QJsonArray secondChord{QJsonObject{{u"portable"_s, u"Ctrl+Shift+F18"_s}}};
    Service service;
    service.set(u"A"_s, first, firstChord);
    service.set(u"A"_s, second, secondChord);
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    require(device.store.setValues({{first, secondChord}, {second, firstChord}, {font, 22}}),
            "submit two dependent bindings and an independent font size");
    device.check();
    QList<QSet<QString>> groups;
    for (const auto group : service.lastUpdate.value(u"groups"_s).toArray()) {
        QSet<QString> keys;
        for (const auto change : group.toObject().value(u"changes"_s).toArray())
            keys.insert(change.toObject().value(u"key"_s).toString());
        groups.append(keys);
    }
    require(groups.size() == 2 && groups.contains({first, second}) && groups.contains({font}) &&
                service.operation(u"A"_s, first).value(u"value"_s) == secondChord &&
                service.operation(u"A"_s, second).value(u"value"_s) == firstChord &&
                service.operation(u"A"_s, font).value(u"value"_s).toInt() == 22,
            "a swap commits in one wire group and the independent edit in another");

    service.permanentlyRejectedKey = first;
    require(device.store.setValues({{first, firstChord}, {second, secondChord}, {font, 23}}),
            "submit a swap with one rejected binding");
    waitUntil(
        [&] {
            return service.operation(u"A"_s, font).value(u"value"_s).toInt() == 23 &&
                   !device.sync.checking();
        },
        "the independent group commits despite the rejected swap");
    require(service.operation(u"A"_s, first).value(u"value"_s) == secondChord &&
                service.operation(u"A"_s, second).value(u"value"_s) == firstChord,
            "the rejected swap preserves both previous server bindings");
}

#ifdef Q_OS_WIN
void explorerSettingsOnUpgrade(const QString& path) {
    Service service;
    const auto options = u"system/context_menu_options"_s;
    const auto scope = u"system/context_menu_mode"_s;
    const auto shortcut = u"context_menu_shortcuts/compress_images"_s;
    const auto legacyToggle = u"system/annotation_mode_when_opening_for_editing"_s;
    {
        ConfigurationStore legacy(path, true, true, 100);
        require(legacy.setCloudSyncState(
                    {{u"version"_s, 1}, {u"signed_out"_s, QJsonObject{{legacyToggle, false}}}}) &&
                    legacy.flushNow().success,
                "persist a signed-out baseline that has no modern Explorer settings");
    }
    service.set(u"A"_s, legacyToggle, true);
    Device device(path, service);
    device.signIn(u"A"_s);
    const QJsonArray annotate{u"pin_to_screen_and_annotate"_s};
    waitUntil([&] { return device.store.value(options) == annotate; },
              "migrate the account's legacy annotation setting");
    device.check();
    const QJsonArray tools{u"stitch_images"_s, u"edit_images"_s};
    service.set(u"A"_s, options, tools);
    service.set(u"A"_s, scope, u"images_only"_s);
    const QJsonArray accessKey{QJsonObject{{u"portable"_s, u"C"_s}}};
    service.set(u"A"_s, shortcut, accessKey);
    device.check();
    require(device.store.value(options) == tools && device.store.value(scope) == u"images_only"_s &&
                device.store.value(shortcut) == accessKey,
            "modern Explorer settings override legacy annotation and retain a hidden shortcut");

    device.account.signOut();
    service.holdRead = true;
    device.signIn(u"B"_s);
    waitUntil([&] { return service.held.size() == 1; }, "hold the second account's first read");
    const QJsonArray pin{u"pin_to_screen"_s};
    require(device.store.value(options) == pin &&
                device.store.value(scope) == device.store.defaultValue(scope) &&
                device.store.value(shortcut) == QJsonArray{},
            "switching accounts restores the legacy baseline before reading the cloud");
    service.holdRead = false;
    service.release();
    device.check();
    require(service.operation(u"B"_s, options).isEmpty() &&
                service.operation(u"B"_s, scope).isEmpty() &&
                service.operation(u"B"_s, shortcut).isEmpty(),
            "the second account cannot upload the previous account's Explorer settings");
}

void writeLegacyExplorerProfile(const QString& path, const Service& service,
                                const QJsonObject& profile, const QString& scope) {
    const auto toggle = u"system/annotation_mode_when_opening_for_editing"_s;
    const auto owner = QString::fromLatin1(
        QCryptographicHash::hash(service.origin().toUtf8() + '\0' + 'A', QCryptographicHash::Sha256)
            .toHex());
    const QJsonObject journal{{u"version"_s, 1},
                              {u"signed_out"_s, QJsonObject{{toggle, false}}},
                              {u"profiles"_s, QJsonObject{{owner, profile}}}};
    const QJsonObject document{
        {u"storage"_s, QJsonObject{{u"schema_version"_s, 11}}},
        {u"system"_s, QJsonObject{{u"context_menu_mode"_s, scope},
                                  {u"annotation_mode_when_opening_for_editing"_s, false}}},
        {u"context_menu_shortcuts"_s, QJsonObject{{u"edit_with_snow_shot"_s, QJsonArray{u"K"_s}}}},
        {u"_cloud_settings_sync"_s, journal}};
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                file.write(QJsonDocument(document).toJson()) > 0,
            "persist a v11 installation with local Explorer settings and a signed-out journal");
}

void explorerDeviceSettingsOnUpgrade(const QString& path) {
    const auto toggle = u"system/annotation_mode_when_opening_for_editing"_s;
    const auto scope = u"system/context_menu_mode"_s;
    const auto options = u"system/context_menu_options"_s;
    const auto pin = u"context_menu_shortcuts/pin_to_screen"_s;
    const auto annotate = u"context_menu_shortcuts/pin_to_screen_and_annotate"_s;
    const QJsonArray key{QJsonObject{{u"portable"_s, u"K"_s}}};
    Service service;
    const QJsonObject existing{{u"joined"_s, true}, {u"values"_s, QJsonObject{{toggle, true}}}};
    writeLegacyExplorerProfile(path, service, existing, u"disabled"_s);
    Device device(path, service);
    require(device.store.value(scope) == u"disabled"_s && device.store.value(pin) == key,
            "the signed-out v11 installation retains its disabled menu and custom access key");
    device.signInAndSync(u"A"_s);
    require(device.store.value(scope) == u"disabled"_s &&
                device.store.value(options) == QJsonArray{} &&
                device.store.value(annotate) == key && device.store.value(pin) == QJsonArray{},
            "an existing account inherits the device scope and the access key for its own action");
    device.account.signOut();
    device.signInAndSync(u"B"_s);
    require(device.store.value(scope) == u"disabled"_s &&
                device.store.value(options) == QJsonArray{} && device.store.value(pin) == key &&
                device.store.value(annotate) == QJsonArray{} &&
                service.operation(u"B"_s, pin).value(u"value"_s) == key,
            "a new account inherits the signed-out scope and access key after switching accounts");
}

void pendingExplorerEditOnUpgrade(const QString& path, bool sent, bool modern) {
    const auto toggle = u"system/annotation_mode_when_opening_for_editing"_s;
    const auto options = u"system/context_menu_options"_s;
    const auto set = [](bool value) {
        return QJsonObject{{u"op"_s, u"set"_s}, {u"value"_s, value}};
    };
    const auto edit = [&](bool desired, int sequence) {
        return QJsonObject{{u"operation"_s, set(desired)},
                           {u"base"_s, set(!sent)},
                           {u"fallback"_s, !sent},
                           {u"sequence"_s, QString::number(sequence)}};
    };
    const QJsonArray editImages{u"edit_images"_s};
    Service service;
    service.set(u"A"_s, toggle, true);
    if (modern)
        service.set(u"A"_s, options, editImages);
    QJsonObject profile{{u"joined"_s, true},
                        {u"read"_s, true},
                        {u"revision"_s, QString::number(service.revisions.value(u"A"_s))},
                        {u"client_version"_s, QCoreApplication::applicationVersion()},
                        {u"sequence"_s, u"2"_s},
                        {u"values"_s, QJsonObject{{toggle, false}}},
                        {u"confirmed"_s, QJsonObject{{toggle, set(!sent)}}},
                        {u"pending"_s, QJsonObject{{toggle, edit(false, 2)}}}};
    // The first upload reached the server before the second edit went offline.
    if (sent)
        profile.insert(u"sent"_s, QJsonObject{{toggle, edit(true, 1)}});
    writeLegacyExplorerProfile(path, service, profile, u"image_files"_s);
    Device device(path, service);
    device.signInAndSync(u"A"_s);
    const auto expected = modern ? editImages : QJsonArray{u"pin_to_screen"_s};
    require(device.store.value(options) == expected &&
                service.operation(u"A"_s, options).value(u"value"_s) == expected &&
                service.operation(u"A"_s, toggle) == set(true),
            "reconcile the legacy edit under its modern key while preserving newer cloud options");
}
#endif

void mousePreferencesOnUpgrade(const QString& path) {
    using namespace snow_shot::mouse;
    Service service;
    service.set(u"A"_s, u"screenshot/double_click_action"_s, u"quick_save"_s);
    service.set(u"A"_s, u"screenshot/middle_mouse_button_action"_s, u"quick_save"_s);
    const auto menu = definition(Action::Menu).key();
    const auto close = definition(Action::Close).key();
    const Binding right{Kind::Click, Qt::RightButton};
    const Binding side{Kind::DoubleClick, Qt::BackButton, Qt::ShiftModifier};
    service.set(u"A"_s, menu, encode(side));
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    require(
        device.store.mouseAction(Category::Screenshot, {Kind::DoubleClick, Qt::LeftButton}) ==
                Action::ShotQuickSave &&
            !device.store.mouseAction(Category::Screenshot, {Kind::Click, Qt::MiddleButton}) &&
            device.store.mouseAction(Category::Pinned, side) == Action::Menu &&
            service.operation(u"A"_s, definition(Action::ShotQuickSave).key()).value(u"value"_s) ==
                encode({Kind::DoubleClick, Qt::LeftButton}),
        "legacy cloud actions seed the current platform independently of new pinned preferences");
    require(
        device.store.setValues({{menu, encode(right)},
                                {close, encode({Kind::Click, Qt::BackButton, Qt::ShiftModifier})}}),
        "reassign the single-click family released by Menu");
    device.check();
    bool atomic = false;
    for (const auto group : service.lastUpdate.value(u"groups"_s).toArray()) {
        QSet<QString> changed;
        for (const auto edit : group.toObject().value(u"changes"_s).toArray())
            changed.insert(edit.toObject().value(u"key"_s).toString());
        if (changed == QSet<QString>{menu, close})
            atomic = true;
    }
    require(atomic, "a click and double-click transfer is synchronized as one group");
}

void signedOutMouseEditMergesWithLegacyCloud(const QString& path) {
    using namespace snow_shot::mouse;
    const auto fineOpacity = definition(Action::FineOpacity).key();
    const Binding wheel{Kind::Wheel, Qt::NoButton, Qt::ShiftModifier};
    const Binding doubleClick{Kind::DoubleClick, Qt::LeftButton};
    Service service;
    service.set(u"A"_s, u"pin_to_screen/double_click_action"_s, u"close"_s);
    Device device(path, service);
    require(device.store.setValue(fineOpacity, encode(wheel)), "customize the wheel before login");
    device.signIn(u"A"_s);
    device.check();
    require(device.store.mouseAction(Category::Pinned, doubleClick) == Action::Close &&
                device.store.mouseAction(Category::Pinned, wheel) == Action::FineOpacity &&
                service.operation(u"A"_s, definition(Action::Close).key()).value(u"value"_s) ==
                    encode(doubleClick) &&
                service.operation(u"A"_s, fineOpacity).value(u"value"_s) == encode(wheel),
            "first login merges the legacy cloud click with the customized local wheel");
}

void pendingLegacyMouseEditsOnUpgrade(const QString& path) {
    using namespace snow_shot::mouse;
    const auto pin = u"pin_to_screen/double_click_action"_s;
    const auto shot = u"screenshot/double_click_action"_s;
    const Binding doubleClick{Kind::DoubleClick, Qt::LeftButton};
    const auto set = [](QString value) {
        return QJsonObject{{u"op"_s, u"set"_s}, {u"value"_s, value}};
    };
    const auto edit = [&](QString base, QString desired, QString sequence) {
        return QJsonObject{{u"operation"_s, set(desired)},
                           {u"base"_s, set(base)},
                           {u"fallback"_s, base},
                           {u"sequence"_s, sequence}};
    };
    Service service;
    {
        Device device(path, service);
        device.signIn(u"A"_s);
        device.check();
        const auto owner = device.store.cloudSyncState().value(u"owner"_s).toString();
        device.account.signOut();
        service.set(u"A"_s, pin, u"close"_s);
        service.set(u"A"_s, shot, u"quick_save"_s);
        auto state = device.store.cloudSyncState();
        const QJsonObject profile{
            {u"joined"_s, true},
            {u"read"_s, true},
            {u"revision"_s, QString::number(service.revisions.value(u"A"_s))},
            {u"client_version"_s, QCoreApplication::applicationVersion()},
            {u"sequence"_s, u"2"_s},
            {u"values"_s, QJsonObject{{pin, u"reset_zoom"_s}, {shot, u"ocr"_s}}},
            {u"confirmed"_s, QJsonObject{{pin, set(u"close"_s)}, {shot, set(u"save"_s)}}},
            {u"pending"_s, QJsonObject{{pin, edit(u"close"_s, u"reset_zoom"_s, u"1"_s)},
                                       {shot, edit(u"save"_s, u"ocr"_s, u"2"_s)}}}};
        state[u"profiles"_s] = QJsonObject{{owner, profile}};
        require(device.store.setCloudSyncState(state) && device.store.flushNow().success,
                "persist a pre-upgrade account with two pending mouse edits");
    }
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    require(device.store.mouseAction(Category::Pinned, doubleClick) == Action::ResetZoom &&
                service.operation(u"A"_s, definition(Action::ResetZoom).key()).value(u"value"_s) ==
                    encode(doubleClick) &&
                service.operation(u"A"_s, pin) == set(u"close"_s),
            "an unconflicted legacy edit is uploaded under its modern key only");
    require(
        device.store.mouseAction(Category::Screenshot, doubleClick) == Action::ShotQuickSave &&
            service.operation(u"A"_s, definition(Action::ShotQuickSave).key()).value(u"value"_s) ==
                encode(doubleClick) &&
            service.operation(u"A"_s, shot) == set(u"quick_save"_s),
        "a conflicting legacy edit yields to the newer cloud value");
}

void mousePreferencesStayWithTheirAccount(const QString& path) {
    using namespace snow_shot::mouse;
    const auto legacy = u"screenshot/double_click_action"_s;
    const Binding doubleClick{Kind::DoubleClick, Qt::LeftButton};
    Service service;
    {
        Device oldDevice(path, service);
        oldDevice.signIn(u"B"_s);
        oldDevice.check();
        const auto profileKey = oldDevice.store.cloudSyncState().value(u"owner"_s).toString();
        require(!profileKey.isEmpty(), "find the saved account profile");
        oldDevice.account.signOut();
        auto state = oldDevice.store.cloudSyncState();
        state[u"signed_out"_s] = QJsonObject{};
        state[u"profiles"_s] =
            QJsonObject{{profileKey, QJsonObject{{u"joined"_s, true},
                                                 {u"values"_s, QJsonObject{{legacy, u"save"_s}}}}}};
        require(oldDevice.store.setCloudSyncState(state), "write a pre-upgrade account profile");
        require(oldDevice.store.flushNow().success, "persist the pre-upgrade account profile");
    }
    service.set(u"A"_s, legacy, u"quick_save"_s);
    service.set(u"B"_s, legacy, u"ocr"_s);
    service.set(u"B"_s, u"screenshot/middle_mouse_button_action"_s, u"quick_save"_s);
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    require(device.store.mouseAction(Category::Screenshot, doubleClick) == Action::ShotQuickSave,
            "account A uses its legacy quick save");
    device.account.signOut();
    service.holdRead = true;
    device.signIn(u"B"_s);
    waitUntil([&] { return service.held.size() == 1; }, "hold the account's first snapshot");
    require(device.store.mouseAction(Category::Screenshot, doubleClick) == Action::ShotSave,
            "offline restore uses the cached legacy action");
    const Binding middle{Kind::Click, Qt::MiddleButton};
    const auto cancel = definition(Action::ShotCancel).key();
    require(device.store.setValues(
                {{definition(Action::ShotPin).key(), encode({})}, {cancel, encode(middle)}}),
            "transfer the middle click while offline");
    service.holdRead = false;
    service.release();
    device.check();
    require(device.store.mouseAction(Category::Screenshot, middle) == Action::ShotCancel &&
                service.operation(u"B"_s, cancel).value(u"value"_s) == encode(middle),
            "the explicit middle click takes precedence over a conflicting legacy cloud gesture");
    require(device.store.mouseAction(Category::Screenshot, doubleClick) == Action::ShotOcr &&
                service.operation(u"B"_s, definition(Action::ShotOcr).key()).value(u"value"_s) ==
                    encode(doubleClick),
            "the latest cloud legacy action supersedes the restored offline cache for account B");
    device.account.signOut();
    device.signIn(u"A"_s);
    device.check();
    require(device.store.mouseAction(Category::Screenshot, doubleClick) == Action::ShotQuickSave,
            "switching back restores account A's migrated action");
}

void modernMouseSnapshotSupersedesLegacyClaims(const QString& path) {
    using namespace snow_shot::mouse;
    const auto legacy = u"screenshot/middle_mouse_button_action"_s;
    const auto pin = definition(Action::ShotPin).key();
    const auto save = definition(Action::ShotSave).key();
    const auto quickSave = definition(Action::ShotQuickSave).key();
    const auto cancel = definition(Action::ShotCancel).key();
    const Binding middle{Kind::Click, Qt::MiddleButton};
    const Binding side{Kind::Click, Qt::ForwardButton, Qt::ShiftModifier};
    Service service;
    {
        Device olderDevice(path, service);
        olderDevice.signIn(u"A"_s);
        olderDevice.check();
        const auto state = olderDevice.store.cloudSyncState();
        const auto accountKey = state.value(u"owner"_s).toString();
        require(!accountKey.isEmpty(), "find the joined account profile");
        olderDevice.account.signOut();
        auto legacyState = olderDevice.store.cloudSyncState();
        legacyState[u"profiles"_s] = QJsonObject{
            {accountKey, QJsonObject{{u"joined"_s, true},
                                     {u"values"_s, QJsonObject{{legacy, u"quick_save"_s}}}}}};
        require(olderDevice.store.setCloudSyncState(legacyState) &&
                    olderDevice.store.flushNow().success,
                "save the joined profile in its legacy format");
    }
    service.set(u"A"_s, legacy, u"quick_save"_s);
    service.set(u"A"_s, pin, encode({}));
    service.set(u"A"_s, save, encode(middle));
    service.holdRead = true;
    Device device(path, service);
    device.signIn(u"A"_s);
    waitUntil([&] { return service.held.size() == 1; }, "hold the first modern snapshot");
    require(device.store.mouseAction(Category::Screenshot, middle) == Action::ShotQuickSave,
            "the old profile initially restores the legacy gesture");
    require(device.store.setValue(cancel, encode(side)),
            "record a modern local edit while the first snapshot is in flight");
    service.holdRead = false;
    service.release();
    device.check();
    require(device.store.mouseAction(Category::Screenshot, middle) == Action::ShotSave &&
                device.store.value(quickSave) == encode({}) &&
                service.operation(u"A"_s, quickSave).isEmpty(),
            "the partial modern snapshot replaces the migrated claim without uploading it");
    require(device.store.mouseAction(Category::Screenshot, side) == Action::ShotCancel &&
                service.operation(u"A"_s, cancel).value(u"value"_s) == encode(side),
            "the pending modern local edit survives reconciliation");
}

void expertTranslationModel(const QString& path) {
    const auto model = u"screenshot_translation/model"_s;
    Service service;
    service.set(u"A"_s, model, u"free-translation"_s);
    Device device(path, service);
    device.signIn(u"A"_s);
    device.check();
    device.sync.setEnabled(false);
    require(device.store.setValue(model, u"expert-model"_s), "select Expert Model while paused");
    device.sync.setEnabled(true);
    device.check();
    require(device.store.value(model) == u"expert-model"_s &&
                service.operation(u"A"_s, model).value(u"value"_s) == u"free-translation"_s,
            "Expert Model stays selected locally when a supported cloud model arrives");
    require(device.store.setValue(model, u"fast-translation"_s), "select a syncable model");
    device.check();
    require(service.operation(u"A"_s, model).value(u"value"_s) == u"fast-translation"_s,
            "explicitly selecting a supported model resumes model sync");
}

void translationPreferenceEdits(const QString& path) {
    const auto targetKey = u"screenshot_translation/target_language"_s;
    Service service;
    const QJsonObject reset{{u"op"_s, u"reset"_s}};
    service.snapshots[u"A"_s].insert(targetKey, reset);
    ++service.revisions[u"A"_s];
    Device device(path, service);
    snow_shot::presentation::TranslationPageController translation(device.client, device.store,
                                                                   QLocale::English);
    device.signIn(u"A"_s);
    device.check();
    const QString target = translation.preferences().targetLanguage;
    require(translation.setPreferences({.sourceLanguage = u"ja"_s}), "change the source language");
    device.check();
    require(service.snapshots.value(u"A"_s).size() == 2 &&
                service.operation(u"A"_s, u"screenshot_translation/source_language"_s)
                        .value(u"value"_s) == u"ja"_s &&
                service.operation(u"A"_s, targetKey) == reset,
            "changing one language preserves reset and missing records for untouched fields");
    require(translation.setPreferences({.targetLanguage = target}),
            "explicitly choose the displayed target");
    device.check();
    require(service.operation(u"A"_s, targetKey).value(u"value"_s) == target,
            "an explicit choice replaces the target reset marker");
}

void unsupportedService(const QString& path) {
    Service service;
    service.unavailable = 404;
    Device device(path, service);
    device.signIn(u"A"_s);
    waitUntil([&] { return service.reads == 1; }, "custom service checked");
    device.sync.syncNow();
    require(service.writes == 0 && !device.sync.lastSuccessfulSync().isValid(),
            "unsupported endpoint does not initialize a snapshot or update success time");
}

void recoverLostAcknowledgement(const QString& path) {
    Service service;
    service.set(u"A"_s, theme, u"light"_s);
    {
        Device device(path, service);
        device.signIn(u"A"_s);
        waitUntil([&] { return device.sync.lastSuccessfulSync().isValid(); },
                  "initial sync persists");
        service.holdWrite = true;
        require(device.store.setValue(theme, u"dark"_s), "first edit on old process");
        waitUntil([&] { return service.held.size() == 1; }, "old process sends the first edit");
        require(device.store.setValue(theme, u"system"_s), "newer edit before shutdown");
        require(device.store.flushNow().success, "outbox and newer edit checkpoint together");
    }
    service.holdWrite = false;
    service.release();
    require(service.operation(u"A"_s, theme).value(u"value"_s) == QJsonValue(u"dark"_s),
            "server committed the original request without a client acknowledgement");
    Device restored(path, service);
    restored.now = restored.now.addSecs(7);
    restored.signIn(u"A"_s);
    waitUntil([&] { return restored.sync.lastSuccessfulSync() == restored.now; },
              "restored outbox settles");
    require(service.operation(u"A"_s, theme).value(u"value"_s) == QJsonValue(u"system"_s),
            "recovering the submitted operation keeps the newer edit pending");
}

void separateServiceAccounts(const QString& path) {
    Service first;
    Service second;
    first.set(u"same-name"_s, theme, u"dark"_s);
    second.set(u"same-name"_s, theme, u"light"_s);
    Device device(path, first);
    device.signIn(u"same-name"_s);
    waitUntil([&] { return device.sync.lastSuccessfulSync().isValid(); }, "first service syncs");
    first.holdRead = true;
    device.sync.syncNow();
    waitUntil([&] { return first.held.size() == 1; }, "first-service request is pending");
    require(device.store.setValue(font, 25), "pending edit belongs to the first service");
    require(device.client.setBaseUrl(second.origin()), "switch to a second account service");
    require(!device.sync.signedIn() && !device.sync.lastSuccessfulSync().isValid(),
            "new service starts with independent sync status");
    first.set(u"same-name"_s, theme, u"system"_s);
    first.release();
    device.now = device.now.addSecs(7);
    device.signIn(u"same-name"_s);
    waitUntil([&] { return device.sync.lastSuccessfulSync() == device.now; },
              "second service syncs");
    require(device.store.value(theme) == QJsonValue(u"light"_s) &&
                first.operation(u"same-name"_s, font).isEmpty() &&
                second.operation(u"same-name"_s, font).isEmpty(),
            "the previous service reply and pending edit cannot affect the new account");
}

void partialRejectionAndLocalFailure(const QString& path) {
    Service service;
    Device device(path, service);
    device.signIn(u"A"_s);
    waitUntil([&] { return device.sync.lastSuccessfulSync().isValid(); }, "initial success saved");
    const auto previousTime = device.sync.lastSuccessfulSync();
    service.permanentlyRejectedKey = font;
    device.now = device.now.addSecs(7);
    require(device.store.setValues({{font, 20}, {theme, u"dark"_s}}), "stage independent edits");
    waitUntil(
        [&] { return service.operation(u"A"_s, theme).value(u"value"_s) == QJsonValue(u"dark"_s); },
        "valid change makes progress alongside rejection");
    const int writes = service.writes;
    const int reads = service.reads;
    waitUntil(
        [&] {
            const auto state = device.store.cloudSyncState();
            const auto owner = state.value(u"owner"_s).toString();
            const auto profile = state.value(u"profiles"_s).toObject().value(owner).toObject();
            const auto edit = profile.value(u"pending"_s).toObject().value(font).toObject();
            return edit.value(u"rejection"_s) == QJsonValue(u"invalid_value"_s) &&
                   !device.store.isDirty() && !device.sync.checking();
        },
        "partial acknowledgement persisted");
    device.sync.syncNow();
    waitUntil([&] { return service.reads > reads && !device.sync.checking(); },
              "manual check after rejection");
    require(service.writes == writes && device.sync.lastSuccessfulSync() == previousTime &&
                device.store.value(font).toInt() == 20,
            "rejection preserves the local value and timestamp without resending it");

    device.store.suspendWrites(true);
    service.set(u"A"_s, theme, u"light"_s);
    const int newerRead = service.reads;
    device.sync.syncNow();
    waitUntil([&] { return service.reads > newerRead && !device.sync.checking(); },
              "download attempted with suspended writes");
    require(device.store.value(theme) == QJsonValue(u"dark"_s) &&
                device.sync.lastSuccessfulSync() == previousTime,
            "failed local save retains the last successful settings and timestamp");
    device.store.suspendWrites(false);
    service.permanentlyRejectedKey.clear();
    require(device.store.setValue(font, 22), "rejecting value corrected locally");
    device.check();
    require(device.store.value(theme) == QJsonValue(u"light"_s) &&
                service.operation(u"A"_s, font).value(u"value"_s).toInt() == 22,
            "writable retry completes remote changes and corrected upload");
}
} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary settings directory");
#ifdef Q_OS_WIN
    if (application.arguments().contains(u"--explorer-upgrade"_s)) {
        explorerSettingsOnUpgrade(temp.filePath(u"explorer.json"_s));
        explorerDeviceSettingsOnUpgrade(temp.filePath(u"explorer-device.json"_s));
        pendingExplorerEditOnUpgrade(temp.filePath(u"explorer-pending.json"_s), false, false);
        pendingExplorerEditOnUpgrade(temp.filePath(u"explorer-sent.json"_s), true, false);
        pendingExplorerEditOnUpgrade(temp.filePath(u"explorer-modern.json"_s), true, true);
        return 0;
    }
#endif
    runCloudSettingsLayoutTests();
    accountLifecycle(temp.filePath(u"device.json"_s));
    unchangedChecksShareNextFlush(temp.filePath(u"unchanged.json"_s));
    signOutDuringStorageMove(temp.filePath(u"suspend-sign-out.json"_s));
    workerEditsAcrossSignOut(temp.filePath(u"worker.json"_s));
    workerEditDuringSnapshot(temp.filePath(u"worker-snapshot.json"_s));
    workerCheckpointSurvivesRestart(temp.filePath(u"worker-checkpoint.json"_s));
    ocrModelDownloadOrigin(temp.filePath(u"ocr-origin.json"_s));
    restorePausedAccount(temp.filePath(u"paused-account.json"_s));
    competingEdits(temp.filePath(u"first.json"_s), temp.filePath(u"second.json"_s));
    pauseSurvivesWriteFailure(temp.filePath(u"suspended-pause"_s), true);
    pauseSurvivesWriteFailure(temp.filePath(u"failed-pause"_s), false);
    conflictingShortcutSwap(temp.filePath(u"swap.json"_s), temp.filePath(u"competing.json"_s));
    blockedShortcutRollback(temp.filePath(u"rollback-confirmed.json"_s), true);
    blockedShortcutRollback(temp.filePath(u"rollback-local.json"_s), false);
    atomicShortcutSwap(temp.filePath(u"atomic-swap.json"_s), u"global_shortcuts/screenshot_delay"_s,
                       u"global_shortcuts/screenshot_copy"_s);
    atomicShortcutSwap(temp.filePath(u"shared-ocr-swap.json"_s),
                       u"screenshot_shortcuts/text_recognition"_s,
                       u"pin_to_screen_shortcuts/save_as_file"_s);
    mousePreferencesOnUpgrade(temp.filePath(u"mouse-operations.json"_s));
    signedOutMouseEditMergesWithLegacyCloud(temp.filePath(u"mouse-signed-out.json"_s));
    pendingLegacyMouseEditsOnUpgrade(temp.filePath(u"mouse-pending-upgrade.json"_s));
    mousePreferencesStayWithTheirAccount(temp.filePath(u"mouse-operations-accounts.json"_s));
    modernMouseSnapshotSupersedesLegacyClaims(temp.filePath(u"mouse-operations-upgrade.json"_s));
    unsupportedShortcutList(temp.filePath(u"unsupported-binding.json"_s));
    translationPreferenceEdits(temp.filePath(u"translation.json"_s));
    expertTranslationModel(temp.filePath(u"expert-model.json"_s));
    unsupportedService(temp.filePath(u"unsupported.json"_s));
    recoverLostAcknowledgement(temp.filePath(u"restart.json"_s));
    separateServiceAccounts(temp.filePath(u"services.json"_s));
    partialRejectionAndLocalFailure(temp.filePath(u"partial.json"_s));
    return 0;
}
