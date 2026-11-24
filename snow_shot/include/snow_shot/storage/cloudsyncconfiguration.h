// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonValue>
#include <QMap>
#include <QStringList>

namespace snow_shot::storage {

[[nodiscard]] const QStringList& cloudSettingKeys();
[[nodiscard]] bool legacyMouseCloudKey(const QString& key);
[[nodiscard]] bool cloudSettingSupported(const QString& key);
[[nodiscard]] bool cloudSettingPlatformSpecific(const QString& key);
[[nodiscard]] bool cloudSettingLayout(const QString& key);
[[nodiscard]] bool cloudSettingLayoutComplete(const QString& key, const QJsonValue& raw);

// Undefined means that the value cannot be synced or applied on this device.
[[nodiscard]] QJsonValue cloudSettingValue(const QString& key, const QJsonValue& local);
[[nodiscard]] QJsonValue cloudSettingLocalValue(const QString& key, const QJsonValue& cloud,
                                                const QJsonValue& local,
                                                const QString& localCustomTrayIconPath = {});
[[nodiscard]] bool cloudSettingShortcutChangesValid(const QMap<QString, QJsonValue>& current,
                                                    const QMap<QString, QJsonValue>& changes);

} // namespace snow_shot::storage
