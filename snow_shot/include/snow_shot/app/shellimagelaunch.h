#ifndef SNOW_SHOT_APP_SHELLIMAGELAUNCH_H
#define SNOW_SHOT_APP_SHELLIMAGELAUNCH_H

#include <QDir>
#include <QUuid>
#include <algorithm>
#include <QStringList>

namespace snow_shot::app {

// Consume argv[0] in the launching process. IPC and updater replays carry typed operands,
// so a receiving instance can run from a different installation directory.
[[nodiscard]] inline QStringList processLaunchOperands(const QStringList& processArguments) {
    auto operands = processArguments.mid(1);
#ifdef Q_OS_WIN
    if (std::none_of(operands.cbegin(), operands.cend(),
                     [](const QString& arg) { return arg.startsWith(u"--"); })) {
        QStringList typed;
        for (const auto& operand : operands) {
            if (QDir::isAbsolutePath(operand))
                typed.append(QStringLiteral("--edit-with-snow-shot"));
            typed.append(operand);
        }
        return typed;
    }
#endif
    return operands;
}

struct ShellImageLaunchRequest {
    bool requested = false;
    bool valid = true;
    QStringList paths;
    QStringList replayArguments;
};

// Explorer supplies a separate typed operand for each shell invocation. Repeated pairs also
// preserve multiple requests across an updater relaunch without parsing a shell command string.
[[nodiscard]] inline ShellImageLaunchRequest
parseShellImageLaunchRequest(const QStringList& arguments) {
    ShellImageLaunchRequest request;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments.at(index);
        if (argument == u"--explorer-selection") {
            request.requested = true;
            if (++index >= arguments.size()) {
                request.valid = false;
                break;
            }
            const auto token = arguments.at(index);
            const QUuid uuid(token);
            if (uuid.isNull() || uuid.toString().compare(token, Qt::CaseInsensitive) != 0) {
                request.valid = false;
                break;
            }
            request.replayArguments << argument << token;
            continue;
        }
        if (argument != u"--edit-with-snow-shot")
            continue;
        request.requested = true;
        if (++index >= arguments.size()) {
            request.valid = false;
            break;
        }
        const QString& path = arguments.at(index);
        bool valid = !path.isEmpty() && !path.contains(u'"') && QDir::isAbsolutePath(path);
#ifdef Q_OS_WIN
        const QString normalized = QDir::fromNativeSeparators(path);
        const bool driveAbsolute = normalized.size() >= 3 && normalized.at(0).toUpper() >= u'A' &&
                                   normalized.at(0).toUpper() <= u'Z' && normalized.at(1) == u':' &&
                                   normalized.at(2) == u'/';
        const bool uncAbsolute = normalized.startsWith(QStringLiteral("//")) &&
                                 normalized.split(u'/', Qt::SkipEmptyParts).size() >= 2;
        valid = valid && (driveAbsolute || uncAbsolute);
#endif
        for (const QChar character : path)
            valid = valid && character.category() != QChar::Other_Control;
        if (!valid) {
            request.valid = false;
            break;
        }
        request.paths.append(path);
        request.replayArguments << QStringLiteral("--edit-with-snow-shot") << path;
    }
    if (!request.valid) {
        request.paths.clear();
        request.replayArguments.clear();
    }
    return request;
}

[[nodiscard]] inline QStringList shellImageLaunchArguments(const QStringList& paths) {
    QStringList arguments;
    for (const QString& path : paths) {
        arguments.append(QStringLiteral("--edit-with-snow-shot"));
        arguments.append(path);
    }
    return arguments;
}

} // namespace snow_shot::app

#endif // SNOW_SHOT_APP_SHELLIMAGELAUNCH_H
