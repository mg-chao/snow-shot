#include "shell_context_menu_test_registry.h"
#include "snow_shot/app/distribution.h"
#include "snow_shot/platform/windows/administratorlaunch.h"
#include "snow_shot/platform/windows/autostartregistration.h"
#include "snow_shot/platform/windows/packagedcontextmenupreference.h"
#include "snow_shot/storage/storagedirectorychange.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    using namespace snow_shot::platform::windows;
    require(snow_shot::app::distribution::isStoreManaged(), "test must compile the Store policy");
    for (const auto state : {AutoStartState::Unavailable, AutoStartState::DisabledByUser,
                             AutoStartState::DisabledByPolicy, AutoStartState::EnabledByPolicy})
        require(!AutoStartRegistration::canChangeState(state),
                "Windows user and policy decisions remain authoritative");
    require(AutoStartRegistration::canChangeState(AutoStartState::Disabled) &&
                AutoStartRegistration::canChangeState(AutoStartState::Enabled),
            "ordinary startup states can be changed explicitly");
#ifdef Q_OS_WIN
    require(snow_shot::app::distribution::runtimePackageFamilyName().isEmpty(),
            "the native Store-policy fixture runs without registered package identity");
    for (int query = 0; query < 3; ++query) {
        const auto startup = AutoStartRegistration::snapshot();
        require(!startup.valid && !startup.exists && startup.state == AutoStartState::Unavailable &&
                    !startup.error.isEmpty() && AutoStartRegistration::hint() == startup.error,
                "repeated unpackaged startup reads report unavailable without invoking WinRT");
    }
    for (const bool enabled : {false, true}) {
        QString startupError;
        require(!AutoStartRegistration::setEnabled(enabled, &startupError) &&
                    !startupError.isEmpty(),
                "unpackaged Store policy cannot change native startup registration");
    }
#endif
    for (const bool elevated : {false, true}) {
        const auto presentation = administratorPresentation({true, true, elevated}, true, false);
        require(!presentation.launchEnabled && !presentation.restartEnabled,
                "Store controls never offer elevation");
    }
    bool persisted = false;
    require(!changeStartupMode(
                 StartupMode::ElevatedTask,
                 [&] {
                     persisted = true;
                     return true;
                 }).success &&
                !persisted,
            "Store startup refuses elevation before changing state");
    require(!restartAsAdministrator([] { return true; }).success,
            "Store restart never launches an elevation helper");
    require(dispatchAdministratorHelper(
                {QStringLiteral("snow_shot.exe"), QStringLiteral("--administrator-helper")}) == 20,
            "Store rejects helper command lines");
    require(reconcileStartupMode(StartupMode::Registry).success,
            "launch reconciliation does not require or modify a startup registration");
    require(removeInstallationStartup(QStringLiteral("C:/unused-store-installation")).success &&
                migrateInstallationStartup(QStringLiteral("C:/unused-store-installation"),
                                           QStringLiteral("D:/unused-store-installation"))
                    .success,
            "Store installation maintenance never repairs native startup registrations");

    QTemporaryDir fixture;
    require(fixture.isValid(), "storage checks use an isolated writable fixture");
    shell_context_menu_tests::Registry registry;
    auto options = registry.options();
    options.packagedPreferencePath = fixture.filePath(QStringLiteral("context-menu-enabled"));
    const auto preferencePath = options.packagedPreferencePath.toStdWString();
    ShellContextMenuRegistration shell(options);
    bool preferenceChanged = false;
    require(!ShellContextMenuRegistration::isSupported(),
            "Store shell registration still belongs to the manifest");
    require(packaged_context_menu::isEnabled(preferencePath),
            "a new installation enables the command before the app first runs");
    ShellMenuPreferences prefs;
    prefs.mode = ShellContextMenuMode::Disabled;
    require(!shell.change(prefs, [] { return false; }).success &&
                !QFile::exists(options.packagedPreferencePath),
            "failed first write restores absent packaged preference");
    require(shell.change(prefs).success && !packaged_context_menu::isEnabled(preferencePath),
            "disabling the scope hides all packaged commands");
    prefs.mode = ShellContextMenuMode::ImageFiles;
    prefs.actions = {'P', '-', 'S', '.', '-', '7'};
    require(shell.change(prefs).success &&
                packaged_context_menu::readPreference(preferencePath) == prefs,
            "every action persists its own visibility and key");
    const auto before = prefs;
    prefs.actions.fill('-');
    require(!shell.change(prefs, [] { return false; }).success &&
                packaged_context_menu::readPreference(preferencePath) == before &&
                registry.writes == 0,
            "failed commits restore packaged labels and visibility without registry changes");
    QFile malformed(options.packagedPreferencePath);
    for (const QByteArray bytes : {QByteArray("0"), QByteArray("1"), QByteArray("2"),
                                   QByteArray("0E"), QByteArray("1E"), QByteArray("2E")}) {
        require(malformed.open(QIODevice::WriteOnly) && malformed.write(bytes) == bytes.size(),
                "create legacy or scoped packaged preferences");
        malformed.close();
        const auto preference = packaged_context_menu::readPreference(preferencePath);
        const auto expected = bytes[0] == '0'   ? ShellContextMenuMode::Disabled
                              : bytes[0] == '2' ? ShellContextMenuMode::ImageFiles
                                                : ShellContextMenuMode::ImageAndDocumentFiles;
        require(preference.mode == expected &&
                    preference.actions[1] == (bytes.size() == 2 ? 'E' : '.'),
                "legacy and image-only payloads preserve scope and optional access keys");
    }
    for (const QByteArray bytes : {QByteArray(), QByteArray("invalid"), QByteArray("1Eextra"),
                                   QByteArray("1&"), QByteArray("1e"), QByteArray("3E")}) {
        require(malformed.open(QIODevice::WriteOnly) && malformed.write(bytes) == bytes.size(),
                "create an invalid preference");
        malformed.close();
        require(packaged_context_menu::readPreference(preferencePath) ==
                    packaged_context_menu::Preference{ShellContextMenuMode::Disabled},
                "invalid data cannot enable the command or supply a menu mnemonic");
    }
    options.packagedPreferencePath += QStringLiteral("/blocked");
    preferenceChanged = false;
    require(!ShellContextMenuRegistration(options)
                    .change({ShellContextMenuMode::Disabled},
                            [&] {
                                preferenceChanged = true;
                                return true;
                            })
                    .success &&
                !preferenceChanged,
            "a native preference write failure never commits application settings");
    const auto first = snow_shot::storage::storageBootstrapPath(
        fixture.path(),
        QStringLiteral("C:/Program Files/WindowsApps/SnowApps.SnowShot_1.0.0.0_x64/bin"));
    const auto updated = snow_shot::storage::storageBootstrapPath(
        fixture.path(), QStringLiteral("D:/WindowsApps/SnowApps.SnowShot_2.0.0.0_arm64/bin"));
    require(first == updated,
            "package version, volume and architecture do not change a saved storage selection");
    const auto custom = fixture.filePath(QStringLiteral("custom-data"));
    QFile selection(first);
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("directory"), custom}}).toJson();
    require(selection.open(QIODevice::WriteOnly) && selection.write(bytes) == bytes.size(),
            "persist custom directory fixture");
    selection.close();
    require(snow_shot::storage::savedStorageDirectory(
                fixture.path(),
                QStringLiteral("D:/WindowsApps/SnowApps.SnowShot_2.0.0.0_arm64/bin")) == custom,
            "an update restores the same custom data directory");
    return 0;
}
