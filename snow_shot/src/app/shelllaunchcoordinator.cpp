// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/app/shelllaunchcoordinator.h"

#include "snow_shot/app/shellimagelaunch.h"
#include "snow_shot/app/supportedpinfiles.h"

#include <QFileInfo>

namespace snow_shot::app {

QStringList ShellLaunchCoordinator::Intent::replayArguments() const {
    auto arguments = fileArguments;
    if (foreground)
        arguments.append(QStringLiteral("--show-main-window"));
    return arguments;
}

ShellLaunchCoordinator::Request ShellLaunchCoordinator::parse(const QStringList& arguments) {
    const auto files = parseShellImageLaunchRequest(arguments);
    Request request;
    request.filesRequested = files.requested;
    request.valid = files.valid;
    request.autostart = arguments.contains(QStringLiteral("--autostart"));
    request.foreground = arguments.contains(QStringLiteral("--show-main-window")) ||
                         (!files.requested && !request.autostart);
    if (!request.valid)
        return request;
    const auto supported = supportedPinFileExtensions();
    for (qsizetype i = 0; i < files.replayArguments.size(); i += 2) {
        const auto flag = files.replayArguments.at(i);
        const auto path = files.replayArguments.at(i + 1);
        if (flag == u"--explorer-selection") {
            request.fileArguments << flag << path;
            continue;
        }
        if (supported.contains(QFileInfo(path).suffix().toLower())) {
            request.paths.append(path);
            request.fileArguments << flag << path;
        } else
            request.unsupportedPaths.append(path);
    }
    return request;
}

ShellLaunchCoordinator::Intent
ShellLaunchCoordinator::updateCandidate(const Request& request) const {
    auto candidate = m_update;
    candidate.paths.append(request.paths);
    candidate.fileArguments.append(request.fileArguments);
    candidate.foreground = candidate.foreground || request.foreground;
    return candidate;
}

bool ShellLaunchCoordinator::acceptsUpdate(const Request& request, bool accepting,
                                           const UpdateAdmission& admission) const {
    // Terminal commitment closes even malformed launches. Before commitment the handler owns
    // validation feedback, so malformed operands must not consume cumulative transfer capacity.
    return accepting && (!request.valid || admission(updateCandidate(request)));
}

ShellLaunchCoordinator::Intent ShellLaunchCoordinator::retainUpdate(const Request& request) {
    m_update = updateCandidate(request);
    return m_update;
}

void ShellLaunchCoordinator::retainStorage(const Request& request) {
    m_storage.paths.append(request.paths);
    m_storage.fileArguments.append(request.fileArguments);
    m_storage.foreground = m_storage.foreground || request.foreground;
}

ShellLaunchCoordinator::Intent ShellLaunchCoordinator::takeUpdate() {
    return std::exchange(m_update, {});
}

ShellLaunchCoordinator::Intent ShellLaunchCoordinator::takeStorage() {
    return std::exchange(m_storage, {});
}

} // namespace snow_shot::app
