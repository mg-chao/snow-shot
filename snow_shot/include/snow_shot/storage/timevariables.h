// SPDX-License-Identifier: GPL-3.0-only
#ifndef SNOW_SHOT_STORAGE_TIMEVARIABLES_H
#define SNOW_SHOT_STORAGE_TIMEVARIABLES_H

#include <QDateTime>
#include <QString>

namespace snow_shot::storage {
[[nodiscard]] bool containsTimeVariables(const QString& text);
[[nodiscard]] QString formatTimeVariable(QString format, const QDateTime& timestamp);
[[nodiscard]] QString expandTimeVariables(const QString& text, const QDateTime& timestamp);
} // namespace snow_shot::storage

#endif
