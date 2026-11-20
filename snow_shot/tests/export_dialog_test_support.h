// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <utility>
namespace adqt::widgets {
class AdModal;
}
void captureExportTestWindow(adqt::widgets::AdModal* modal, const QString& name);
std::pair<QByteArray, QString> readExportTestClipboard();
