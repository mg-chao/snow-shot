// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/storage/screenshotexportvariables.h"
#include "snow_shot/storage/exportfilename.h"
#include "snow_shot/storage/timevariables.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <optional>

namespace snow_shot::storage {
namespace {
QString pathComponent(const QString& value) {
    QString result = sanitizeExportFileName(
        value, u"Unknown", {ExportFileNameControls::C0AndC1, maximumExportPathComponentUtf8Bytes});
    // Windows device names remain reserved even when followed by an extension.
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(result).hasMatch())
        result.prepend(u'_');
    return result;
}
std::optional<QStandardPaths::StandardLocation> directoryLocation(QStringView token) {
    using Path = QStandardPaths;
    if (token == u"HOME")
        return Path::HomeLocation;
    if (token == u"PICTURES")
        return Path::PicturesLocation;
    if (token == u"VIDEOS")
        return Path::MoviesLocation;
    if (token == u"DESKTOP")
        return Path::DesktopLocation;
    if (token == u"DOCUMENTS")
        return Path::DocumentsLocation;
    if (token == u"DOWNLOADS")
        return Path::DownloadLocation;
    return std::nullopt;
}

QString boundPathComponents(QString path) {
    constexpr auto limit = maximumExportPathComponentUtf8Bytes;
    // One UTF-16 code unit occupies at most three UTF-8 bytes.
    if (path.size() <= limit / 3)
        return path;
    qsizetype end = path.size();
    while (end > limit / 3) {
        qsizetype start = end;
        while (start && path.at(start - 1) != u'/' && path.at(start - 1) != u'\\')
            --start;
        if (end - start > limit / 3) {
            auto component = path.mid(start, end - start);
            if (component.toUtf8().size() > limit)
                path.replace(start, end - start,
                             composeExportFileName(std::move(component), {}, limit));
        }
        end = start - 1;
    }
    return path;
}

QString expand(const QString& input, const QDateTime& timestamp,
               const ScreenshotExportContext& context, bool directory) {
    const QString text = input.trimmed();
    if (!text.contains(u'{'))
        return boundPathComponents(text);
    static const QRegularExpression placeholders(QStringLiteral("\\{([^{}]+)\\}"));
    auto matches = placeholders.globalMatch(text);
    if (!matches.hasNext())
        return boundPathComponents(text);
    QString result;
    result.reserve(text.size());
    qsizetype position = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += QStringView(text).mid(position, match.capturedStart() - position);
        position = match.capturedEnd();
        const auto token = match.capturedView(1);
        if (token == u"AN")
            result += pathComponent(context.applicationName);
        else if (token == u"WN")
            result += pathComponent(context.windowName);
        else if (token == u"SCREEN")
            result += pathComponent(context.screen);
        else if (const auto location = directoryLocation(token)) {
            // Absolute system folders can only introduce a directory path, never a filename.
            if (directory && match.capturedStart() == 0 &&
                (position == text.size() || text.at(position) == u'/' ||
                 text.at(position) == u'\\')) {
                const QString root = QStandardPaths::writableLocation(*location);
                if (root.isEmpty())
                    return {};
                result += root;
                if (root.endsWith(u'/') && position < text.size())
                    ++position;
            } else {
                result += match.capturedView();
            }
        } else
            result += formatTimeVariable(token.toString(), timestamp);
    }
    result += QStringView(text).mid(position);
    return boundPathComponents(std::move(result));
}
} // namespace

bool containsScreenshotWindowVariables(const QString& text) {
    return text.contains(QStringLiteral("{AN}")) || text.contains(QStringLiteral("{WN}"));
}

ScreenshotWindowNameFields screenshotWindowNameFields(const QString& directory,
                                                      const QString& filename) {
    return {directory.contains(QStringLiteral("{AN}")) || filename.contains(QStringLiteral("{AN}")),
            directory.contains(QStringLiteral("{WN}")) ||
                filename.contains(QStringLiteral("{WN}"))};
}

QString expandScreenshotExportVariables(const QString& text, const QDateTime& timestamp,
                                        const ScreenshotExportContext& context) {
    return expand(text, timestamp, context, false);
}

QString expandScreenshotExportDirectory(const QString& text, const QDateTime& timestamp,
                                        const ScreenshotExportContext& context) {
    return expand(text, timestamp, context, true);
}
} // namespace snow_shot::storage
