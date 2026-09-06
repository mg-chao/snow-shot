#pragma once

#include <QString>
#include <QVector>

namespace adqt::theme {

QVector<QString> generatePalette(const QString& color, bool darkTheme = false,
                                 const QString& backgroundColor = QString());

}  // namespace adqt::theme
