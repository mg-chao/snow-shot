// SPDX-License-Identifier: GPL-3.0-only
#include "snow_shot/presentation/screenshotimagefileservice.h"
#include "snow_shot/presentation/screenshotmanualsave.h"
#include "snow_shot/presentation/screenrecordingfolder.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/timevariables.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTimeZone>

#include <cstdlib>
#include <iostream>

namespace storage = snow_shot::storage;
namespace recording = snow_shot::presentation::recording;

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void exportVariables(const QTemporaryDir& temporary) {
    const QDateTime timestamp(QDate(2026, 11, 22), QTime(12, 34, 56), QTimeZone::UTC);
    const storage::ScreenshotExportContext context{
        {}, QStringLiteral("{HOME}"), QStringLiteral("Display-2")};
    using Path = QStandardPaths;
    const std::pair<QString, Path::StandardLocation> roots[] = {
        {QStringLiteral("{HOME}"), Path::HomeLocation},
        {QStringLiteral("{PICTURES}"), Path::PicturesLocation},
        {QStringLiteral("{VIDEOS}"), Path::MoviesLocation},
        {QStringLiteral("{DESKTOP}"), Path::DesktopLocation},
        {QStringLiteral("{DOCUMENTS}"), Path::DocumentsLocation},
        {QStringLiteral("{DOWNLOADS}"), Path::DownloadLocation},
    };
    for (const auto& [token, location] : roots) {
        const QString root = Path::writableLocation(location);
        require(storage::expandScreenshotExportDirectory(token, timestamp) == root,
                "each root must use its own system location");
        if (!root.isEmpty())
            require(storage::expandScreenshotExportDirectory(
                        token + QStringLiteral("/{YYYY}/{SCREEN}/{WN}"), timestamp, context) ==
                        QDir(root).filePath(QStringLiteral("2026/Display-2/{HOME}")),
                    "system paths and capture metadata must expand in one pass");
        require(storage::expandScreenshotExportVariables(token, timestamp) == token,
                "a filename cannot inject an absolute path");
    }
    const auto plain = temporary.filePath(QStringLiteral("截图/plain-name"));
    require(storage::expandScreenshotExportDirectory(plain, timestamp).constData() ==
                    plain.constData() &&
                storage::expandScreenshotExportVariables(plain, timestamp).constData() ==
                    plain.constData(),
            "plain templates must reuse their QString storage");
    const QString retired = QStringLiteral("{WIDTH}x{HEIGHT}");
    const QString timeOnly = storage::expandTimeVariables(retired, timestamp);
    require(storage::expandScreenshotExportVariables(retired, timestamp, context) == timeOnly &&
                storage::expandScreenshotExportDirectory(retired, timestamp, context) == timeOnly,
            "retired size placeholders must follow ordinary time-format handling");
    require(storage::expandScreenshotExportVariables(QStringLiteral("{SCREEN}"), timestamp) ==
                QStringLiteral("Unknown"),
            "unavailable metadata has a stable printable fallback");
    QImage image(32, 19, QImage::Format_RGB32);
    image.fill(Qt::red);
    const auto saved = ScreenshotImageFileService::saveAutomatically(
        image, {temporary.path()}, ScreenshotImageFileFormat::Png, QStringLiteral("{SCREEN}"),
        timestamp, {}, {}, context);
    require(saved.succeeded() &&
                QFileInfo(saved.path).fileName() == QStringLiteral("Display-2.png"),
            "automatic saves must use the source screen");
}
void windowVariables(const QTemporaryDir& temporary) {
    const QDateTime timestamp(QDate(2026, 10, 16), QTime(12, 34, 56), QTimeZone::UTC);
    const storage::ScreenshotExportContext names{QStringLiteral("Google Chrome"),
                                                 QStringLiteral("文档: A/B")};
    require(storage::containsScreenshotWindowVariables(QStringLiteral("/{AN}/image")) &&
                storage::containsScreenshotWindowVariables(QStringLiteral("{WN}")) &&
                !storage::containsScreenshotWindowVariables(QStringLiteral("{an}-{wn}-{YYYY}")),
            "only exact AN and WN tokens must trigger native lookup");
    require(storage::expandScreenshotExportVariables(QStringLiteral("{AN}/{WN}_{YYYY}_{AN}"),
                                                     timestamp, names) ==
                QStringLiteral("Google Chrome/文档_ A_B_2026_Google Chrome"),
            "window values must be sanitized individually and coexist with time variables");
    require(storage::expandScreenshotExportVariables(QStringLiteral("{AN}-{WN}"), timestamp) ==
                    QStringLiteral("Unknown-Unknown") &&
                storage::expandScreenshotExportVariables(QStringLiteral("{AN}-{WN}"), timestamp,
                                                         {QStringLiteral("Known"), {}}) ==
                    QStringLiteral("Known-Unknown"),
            "missing names must fall back independently");
    require(storage::expandScreenshotExportVariables(
                QStringLiteral("{AN}-{WN}"), timestamp,
                {QStringLiteral("{WN}"), QStringLiteral("{YYYY}")}) ==
                QStringLiteral("{WN}-{YYYY}"),
            "retrieved names must never be parsed as templates");
    for (const QString& value : {QStringLiteral("CON"), QStringLiteral("nul.txt"),
                                 QStringLiteral("LPT1"), QStringLiteral("COM²")})
        require(storage::expandScreenshotExportVariables(
                    QStringLiteral("{WN}"), timestamp, {{}, value}) == QStringLiteral("_") + value,
                "reserved device names must be safe path components");
    require(storage::expandScreenshotExportVariables(
                QStringLiteral("{AN}/{WN}"), timestamp,
                {QStringLiteral(" .. "), QStringLiteral("x\n\t\x7f<>:\"/\\|?*... ")}) ==
                QStringLiteral("Unknown/x____________"),
            "dot-only names, control characters and path punctuation must be safe");
    require(storage::expandTimeVariables(QStringLiteral("{AN}-{WN}"), timestamp) ==
                timestamp.toString(QStringLiteral("AN")) + QLatin1Char('-') +
                    timestamp.toString(QStringLiteral("WN")),
            "the time-only expander must remain unchanged for other callers");
    const QString directory = temporary.filePath(QStringLiteral("names/{AN}/{YYYY}"));
    const QString filename = QStringLiteral("{WN}_{HH-mm-ss}");
    require(storage::ScreenshotSettings().setImageSaveDirectory(directory) &&
                storage::ScreenshotSettings().setManualSaveFilenameFormat(filename) &&
                storage::ScreenshotSettings().setLastManualSaveDirectory({}),
            "window naming settings must persist");
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    const auto directories =
        ScreenshotImageFileService::automaticDirectories(directory, timestamp, names);
    const auto saved = ScreenshotImageFileService::saveAutomatically(
        image, directories, ScreenshotImageFileFormat::Png, filename, timestamp, {}, {}, names);
    require(saved.succeeded() &&
                saved.path == screenshotManualSaveDefaults(timestamp, names).initialPath &&
                saved.path.endsWith(QStringLiteral("/Google Chrome/2026/文档_ A_B_12-34-56.png")),
            "automatic saves and native dialog defaults must use identical metadata");
    const auto duplicate = ScreenshotImageFileService::saveAutomatically(
        image, directories, ScreenshotImageFileFormat::Png, filename, timestamp, {}, {}, names);
    require(duplicate.succeeded() && duplicate.path.endsWith(QStringLiteral("_1.png")),
            "window variables must retain collision suffix behavior");

    const QString videoTemplate = temporary.filePath(QStringLiteral("videos/{AN}/{WN}/{YYYY}"));
    const QString expandedVideo =
        temporary.filePath(QStringLiteral("videos/Google Chrome/文档_ A_B/2026"));
    require(storage::RecordingSettings().setVideoSaveDirectory(videoTemplate),
            "recording window directory template must persist");
    const auto videoDirectories = recording::screenRecordingDirectories(timestamp, names);
    require(videoDirectories.constFirst() == expandedVideo && !QFileInfo::exists(expandedVideo),
            "recording names and time must resolve together without creating directories");
    const QString videoBase = storage::expandScreenshotExportVariables(filename, timestamp, names);
    const QString video =
        recording::screenRecordingOutputPath(videoDirectories, videoBase, QStringLiteral("mp4"));
    require(video == QDir(expandedVideo).filePath(QStringLiteral("文档_ A_B_12-34-56.mp4")) &&
                recording::screenRecordingDirectory(timestamp, names) == expandedVideo,
            "recording export and folder resolution must create the same expanded directory");
    QFile collision(video);
    require(collision.open(QIODevice::WriteOnly), "create a named recording collision");
    collision.close();
    require(
        recording::screenRecordingOutputPath(videoDirectories, videoBase, QStringLiteral("mp4")) ==
            QDir(expandedVideo).filePath(QStringLiteral("文档_ A_B_12-34-56_1.mp4")),
        "named recordings preserve collision suffixes");
    require(recording::screenRecordingDirectories(timestamp).constFirst() ==
                    temporary.filePath(QStringLiteral("videos/Unknown/Unknown/2026")) &&
                storage::RecordingSettings().videoSaveDirectory() == videoTemplate,
            "missing recording context uses Unknown and preserves the stored template");
    require(storage::RecordingSettings().setVideoSaveDirectory(
                temporary.filePath(QStringLiteral("literal-names/{AN}/{WN}"))) &&
                recording::screenRecordingDirectories(
                    timestamp, {QStringLiteral("{WN}"), QStringLiteral("{YYYY}")})
                        .constFirst() ==
                    temporary.filePath(QStringLiteral("literal-names/{WN}/{YYYY}")),
            "recording directory names containing braces must never expand recursively");
    const QString blocked = temporary.filePath(QStringLiteral("window-blocker"));
    QFile blocker(blocked);
    require(blocker.open(QIODevice::WriteOnly),
            "create a blocker for the named recording directory");
    blocker.close();
    require(storage::RecordingSettings().setVideoSaveDirectory(blocked + QStringLiteral("/{AN}")),
            "configure an unusable named recording directory");
    auto blockedDirectories = recording::screenRecordingDirectories(timestamp, names);
    const QString fallback = temporary.filePath(QStringLiteral("window-fallback"));
    blockedDirectories = {blockedDirectories.constFirst(), fallback};
    require(recording::screenRecordingOutputPath(blockedDirectories, videoBase,
                                                 QStringLiteral("mp4")) ==
                QDir(fallback).filePath(videoBase + QStringLiteral(".mp4")),
            "unusable named recording directories retain the output fallback");
}

void timeDirectories(const QTemporaryDir& temporary) {
    const QDateTime before(QDate(2026, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);
    const QDateTime after = before.addSecs(1);
    const QString pattern = temporary.filePath(QStringLiteral("output/{YYYY}/{MM}/{DD}"));
    require(storage::expandTimeVariables(QStringLiteral(" {YYYY-MM-DD_HH-mm-ss} "), before) ==
                    QStringLiteral("2026-12-31_23-59-59") &&
                storage::expandTimeVariables(QStringLiteral("{yyyy}/{YY}/{dd}"), after) ==
                    QStringLiteral("2027/27/01"),
            "shared expansion must preserve existing date formats and aliases");
    require(
        storage::ScreenshotSettings().setImageSaveDirectory(pattern) &&
            storage::ScreenshotSettings().setManualSaveFilenameFormat(QStringLiteral("{YYYY}")) &&
            storage::RecordingSettings().setVideoSaveDirectory(pattern),
        "template settings must persist");
    QImage image(QSize(2, 2), QImage::Format_RGB32);
    image.fill(Qt::red);
    for (const auto& timestamp : {before, after}) {
        const QString expanded = storage::expandTimeVariables(pattern, timestamp);
        require(!QFileInfo::exists(expanded), "dated directory must initially be absent");
        const auto imageDirectories =
            ScreenshotImageFileService::automaticDirectories(pattern, timestamp);
        const auto videoDirectories = recording::screenRecordingDirectories(timestamp);
        require(imageDirectories.constFirst() == expanded &&
                    videoDirectories.constFirst() == expanded && !QFileInfo::exists(expanded),
                "directory resolution must expand templates without filesystem mutation");
        const auto saved = ScreenshotImageFileService::saveAutomatically(
            image, imageDirectories, ScreenshotImageFileFormat::Png, QStringLiteral("{YYYY}"),
            timestamp);
        const auto defaults = screenshotManualSaveDefaults(timestamp);
        require(
            saved.succeeded() && saved.path == defaults.initialPath &&
                saved.path == QDir(expanded).filePath(timestamp.toString(QStringLiteral("yyyy")) +
                                                      QStringLiteral(".png")) &&
                !saved.path.contains(QLatin1Char('{')),
            "automatic and manual output must use one timestamp and create the dated directory");
        const QString baseName =
            ScreenshotImageFileService::suggestedBaseName(QStringLiteral("{YYYY}"), timestamp);
        const QString video =
            recording::screenRecordingOutputPath(videoDirectories, baseName, QStringLiteral("mp4"));
        require(video == QDir(expanded).filePath(baseName + QStringLiteral(".mp4")) &&
                    recording::screenRecordingDirectory(timestamp) == expanded,
                "recording output and folder resolution must use the expanded directory");
        QFile collision(video);
        require(collision.open(QIODevice::WriteOnly), "video collision fixture must open");
        collision.close();
        require(recording::screenRecordingOutputPath(videoDirectories, baseName,
                                                     QStringLiteral("mp4")) ==
                    QDir(expanded).filePath(baseName + QStringLiteral("_1.mp4")),
                "recording collision names must remain intact");
    }
    require(storage::ScreenshotSettings().imageSaveDirectory() == pattern &&
                storage::RecordingSettings().videoSaveDirectory() == pattern,
            "resolution must preserve stored templates");

    const QString videoPattern = temporary.filePath(QStringLiteral("videos/{YYYY}"));
    const QString videoDirectory = temporary.filePath(QStringLiteral("videos/2027"));
    require(storage::RecordingSettings().setVideoSaveDirectory(videoPattern) &&
                !QFileInfo::exists(videoDirectory),
            "new recording directory fixture must be absent");
    require(recording::screenRecordingOutputPath(recording::screenRecordingDirectories(after),
                                                 QStringLiteral("video"), QStringLiteral("mp4")) ==
                    QDir(videoDirectory).filePath(QStringLiteral("video.mp4")) &&
                QFileInfo(videoDirectory).isDir(),
            "first recording must create its dated output directory");

    const QString remembered = temporary.filePath(QStringLiteral("remembered/{YYYY}"));
    require(QDir().mkpath(remembered) && ScreenshotImageFileService::saveDialogDirectory(
                                             remembered, pattern, before) == remembered,
            "remembered concrete paths must remain literal and retain precedence");
    const QString missing = temporary.filePath(QStringLiteral("missing"));
    require(!ScreenshotImageFileService::automaticDirectories(missing, before).contains(missing) &&
                storage::RecordingSettings().setVideoSaveDirectory(missing) &&
                !recording::screenRecordingDirectories(before).contains(missing),
            "missing literal configured directories must retain their fallback behavior");
    const QString blocker = temporary.filePath(QStringLiteral("blocked"));
    QFile file(blocker);
    require(file.open(QIODevice::WriteOnly), "blocked directory fixture must open");
    file.close();
    const QString expandedBlocked =
        storage::expandTimeVariables(blocker + QStringLiteral("/{YYYY}"), before);
    const QString fallback = temporary.filePath(QStringLiteral("fallback"));
    const auto saved = ScreenshotImageFileService::saveAutomatically(
        image, {expandedBlocked, fallback}, ScreenshotImageFileFormat::Png,
        QStringLiteral("fallback"), before);
    require(saved.succeeded() && QFileInfo(saved.path).absolutePath() == fallback &&
                recording::screenRecordingOutputPath({expandedBlocked, fallback},
                                                     QStringLiteral("fallback"),
                                                     QStringLiteral("mp4")) ==
                    QDir(fallback).filePath(QStringLiteral("fallback.mp4")),
            "unusable expanded targets must retain writer fallback behavior");
}

void longWindowNames(const QTemporaryDir& temporary) {
    const QDateTime timestamp(QDate(2026, 10, 16), QTime(12, 34, 56), QTimeZone::UTC);
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString longStem = QString::fromUtf8("文😀").repeated(100);
    for (const QString& ending : {QStringLiteral("_1234567890.webp"),
                                  QStringLiteral("-12345678-1234-1234-1234-123456789012.png")}) {
        const QString name = storage::composeExportFileName(longStem, ending);
        require(name.toUtf8().size() <= 255 && name.endsWith(ending) &&
                    QString::fromUtf8(name.toUtf8()) == name,
                "large collision and unique suffixes must fit without splitting Unicode");
    }
    const QString windowsDirectory = storage::expandScreenshotExportVariables(
        QStringLiteral("C:\\images\\prefix-{AN}-{WN}\\{WN}"), timestamp, {longStem, longStem});
    const QString uncDirectory = storage::expandScreenshotExportVariables(
        QStringLiteral("\\\\server\\share\\{WN}"), timestamp, {{}, longStem});
    require(windowsDirectory.startsWith(QStringLiteral("C:\\images\\prefix-")) &&
                uncDirectory.startsWith(QStringLiteral("\\\\server\\share\\")),
            "bounding directory components must preserve drive and UNC separators");
    int index = 0;
    for (const QString& title : {QString(300, u'x'), QString::fromUtf8("文😀").repeated(100)}) {
        const storage::ScreenshotExportContext names{title, title};
        const QString directory =
            temporary.filePath(QStringLiteral("long-%1/prefix-{AN}-{WN}-suffix/{WN}").arg(index++));
        const QString filename = QStringLiteral("{AN}_{WN}_{YYYY}");
        const auto directories =
            ScreenshotImageFileService::automaticDirectories(directory, timestamp, names);
        const auto first = ScreenshotImageFileService::saveAutomatically(
            image, directories, ScreenshotImageFileFormat::Png, filename, timestamp, {}, {}, names);
        require(first.succeeded() &&
                    QFileInfo(first.path).absolutePath() == directories.constFirst(),
                "long window names must save in the configured directory without fallback");
        const auto duplicate = ScreenshotImageFileService::saveAutomatically(
            image, directories, ScreenshotImageFileFormat::Png, filename, timestamp, {}, {}, names);
        require(duplicate.succeeded() && duplicate.path.endsWith(QStringLiteral("_1.png")),
                "long window names must leave room for collision suffixes");
        QFile encoded(first.path);
        require(encoded.open(QIODevice::ReadOnly), "read the long-name PNG fixture");
        const auto copied = ScreenshotImageFileService::saveEncodedBytesAutomatically(
            encoded.readAll(), directories, ScreenshotImageFileFormat::Png, filename, timestamp, {},
            names);
        require(copied.succeeded() && copied.path.endsWith(QStringLiteral("_2.png")),
                "encoded exports must use the same bounded collision names");
        require(storage::ScreenshotSettings().setImageSaveDirectory(directory) &&
                    storage::ScreenshotSettings().setLastManualSaveDirectory({}) &&
                    storage::ScreenshotSettings().setLastManualSaveFormat(QStringLiteral("png")) &&
                    storage::ScreenshotSettings().setManualSaveFilenameFormat(filename) &&
                    screenshotManualSaveDefaults(timestamp, names).initialPath == first.path,
                "native manual defaults must reserve the same extension space as automatic saves");

        const QString base = storage::expandScreenshotExportVariables(filename, timestamp, names);
        const QString video =
            recording::screenRecordingOutputPath(directories, base, QStringLiteral("mp4"));
        QFile existing(video);
        require(existing.open(QIODevice::WriteOnly), "long recording names must be writable");
        existing.close();
        const QString duplicateVideo =
            recording::screenRecordingOutputPath(directories, base, QStringLiteral("mp4"));
        require(duplicateVideo.endsWith(QStringLiteral("_1.mp4")),
                "long recording names must preserve collision suffixes");
        for (const QString& path :
             {first.path, duplicate.path, copied.path, video, duplicateVideo}) {
            for (const QString& component : path.split(QLatin1Char('/'))) {
                require(
                    component.toUtf8().size() <= 255 &&
                        QString::fromUtf8(component.toUtf8()) == component,
                    "every generated component must fit the byte limit without splitting Unicode");
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    auto& appStorage = storage::ApplicationStorage::instance();
    require(temporary.isValid() &&
                appStorage.initialize({temporary.path(), temporary.path(), 60000}).success,
            "isolated settings must initialize");
    exportVariables(temporary);
    timeDirectories(temporary);
    windowVariables(temporary);
    longWindowNames(temporary);
    appStorage.shutdown();
    return EXIT_SUCCESS;
}
