// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/storage/timevariables.h"

#include <QRegularExpression>

namespace snow_shot::storage {
namespace {
const QRegularExpression& placeholderPattern() {
    static const QRegularExpression pattern(QStringLiteral("\\{([^{}]+)\\}"));
    return pattern;
}
} // namespace

bool containsTimeVariables(const QString& text) {
    return text.contains(u'{') && placeholderPattern().match(text).hasMatch();
}

QString formatTimeVariable(QString format, const QDateTime& timestamp) {
    format.replace(QStringLiteral("YYYY"), QStringLiteral("yyyy"));
    format.replace(QStringLiteral("YY"), QStringLiteral("yy"));
    format.replace(QStringLiteral("DD"), QStringLiteral("dd"));
    return timestamp.toString(format);
}

QString expandTimeVariables(const QString& text, const QDateTime& timestamp) {
    QString result = text.trimmed();
    if (!result.contains(u'{'))
        return result;
    QList<QRegularExpressionMatch> matches;
    auto iterator = placeholderPattern().globalMatch(result);
    while (iterator.hasNext()) {
        matches.push_back(iterator.next());
    }
    for (auto match = matches.crbegin(); match != matches.crend(); ++match) {
        result.replace(match->capturedStart(), match->capturedLength(),
                       formatTimeVariable(match->captured(1), timestamp));
    }
    return result;
}
} // namespace snow_shot::storage
