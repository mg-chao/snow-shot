// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QMap>
#include <QJsonValue>

#include <functional>
#include <memory>

namespace snow_shot::storage {
class ConfigurationStore;
}

namespace snow_shot::app {
class AccountSessionService;

class CloudSettingsSyncService final : public QObject {
    Q_OBJECT

  public:
    struct Options {
        int editDelayMs = 3000;
        int settingsCheckIntervalMs = 60'000;
        int trayIntervalMs = 30 * 60'000;
        int retryInitialMs = 5000;
        bool observeConnectivity = true;
        std::function<bool(const QMap<QString, QJsonValue>&, const std::function<bool()>&)>
            commitMenuSettings;
        std::function<QDateTime()> utcNow = QDateTime::currentDateTimeUtc;
    };

    CloudSettingsSyncService(AccountSessionService& account, storage::ConfigurationStore& store,
                             QObject* parent = nullptr);
    CloudSettingsSyncService(AccountSessionService& account, storage::ConfigurationStore& store,
                             Options options, QObject* parent = nullptr);
    ~CloudSettingsSyncService() override;

    [[nodiscard]] bool signedIn() const;
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool checking() const;
    [[nodiscard]] QDateTime lastSuccessfulSync() const;
    [[nodiscard]] static bool shouldDeferOcrModelDownload(const storage::ConfigurationStore& store);
    void setEnabled(bool enabled);
    void syncNow();
    void settingsOpened();

  signals:
    void changed();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace snow_shot::app
