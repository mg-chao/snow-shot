#include "snow_shot/update/updateservice.h"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QWidget>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QFile configuration(QCoreApplication::applicationDirPath() +
                        QStringLiteral("/../Resources/fixture.json"));
    const auto fixture = configuration.open(QIODevice::ReadOnly)
                             ? QJsonDocument::fromJson(configuration.readAll()).object()
                             : QJsonObject();
    const QString root = qEnvironmentVariable("SNOW_SPARKLE_FIXTURE_DIR",
                                              fixture.value(QStringLiteral("root")).toString());
    if (root.isEmpty())
        return 2;
    snow_shot::update::UpdateService service({});
    QWidget window;
    window.show();
    QString lastOperation;
    QString lastOutcome;
    int completedOperations = 0;
    const auto snapshot = [&] {
        const auto& status = service.status();
        QSaveFile file(root + QStringLiteral("/status.json"));
        if (!file.open(QIODevice::WriteOnly))
            std::abort();
        file.write(
            QJsonDocument(QJsonObject{{QStringLiteral("state"), static_cast<int>(status.state)},
                                      {QStringLiteral("error"), status.error},
                                      {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                                      {QStringLiteral("operation"), lastOperation},
                                      {QStringLiteral("outcome"), lastOutcome},
                                      {QStringLiteral("completedOperations"), completedOperations},
                                      {QStringLiteral("ready"), service.installsOnQuit()},
                                      {QStringLiteral("windowVisible"), window.isVisible()}})
                .toJson());
        if (!file.commit())
            std::abort();
    };
    QObject::connect(&service, &snow_shot::update::UpdateService::statusChanged, snapshot);
    QObject::connect(&service, &snow_shot::update::UpdateService::operationFinished,
                     [&](const QString& operation, const QString& outcome) {
                         lastOperation = operation;
                         lastOutcome = outcome;
                         ++completedOperations;
                         snapshot();
                     });
    QObject::connect(&service, &snow_shot::update::UpdateService::restartRequested, &service,
                     &snow_shot::update::UpdateService::beginApply);
    QObject::connect(&service, &snow_shot::update::UpdateService::handoffCommitted, &app,
                     &QApplication::quit);
    service.setHandoffApproval(&app, [] { return true; });
    service.setHandoffValidation(&app, [] { return true; });
    QTimer commands;
    QObject::connect(&commands, &QTimer::timeout, [&] {
        QFile file(root + QStringLiteral("/command"));
        if (!file.open(QIODevice::ReadOnly))
            return;
        const auto command = file.readAll().trimmed();
        file.close();
        file.remove();
        if (command == "quit")
            app.quit();
        else if (command == "close") {
            window.close();
            snapshot();
        } else if (command == "manual")
            service.setMode(QStringLiteral("manual"));
        else if (command == "check")
            service.check();
        else if (command == "download")
            service.download();
        else if (command == "restart")
            service.requestRestart();
    });
    commands.start(50);
    QTimer::singleShot(90000, &app, &QApplication::quit);
    service.setMode(qEnvironmentVariable("SNOW_SPARKLE_FIXTURE_MODE", QStringLiteral("manual")));
    service.start();
    snapshot();
    return app.exec();
}
