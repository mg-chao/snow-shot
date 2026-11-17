#include "snow_shot/presentation/globalshortcutmanager.h"
#include "snow_shot/presentation/settings/settingsbackend.h"
#include "snow_shot/presentation/settings/settingsregistry.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/storage/applicationstorage.h"

#include <QApplication>
#include <QTemporaryDir>
#include <windows.h>

#include <cstdlib>
#include <iostream>

namespace {
namespace settings = snow_shot::presentation::settings;
namespace storage = snow_shot::storage;

const QString kPowerQoSKey = QStringLiteral("system/power_qos");
const QString kPowerQoSId = QStringLiteral("system.power-qos");
constexpr auto kBinding = settings::SettingsSelectBinding::PowerQoS;
constexpr ULONG kExecutionSpeed = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void requireExecutionSpeedPolicy(ULONG control, ULONG state) {
    PROCESS_POWER_THROTTLING_STATE policy{};
    policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    require(GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &policy,
                                  sizeof(policy)) != FALSE,
            "query the process power throttling policy");
    require((policy.ControlMask & kExecutionSpeed) == control &&
                (policy.StateMask & kExecutionSpeed) == state,
            "Windows must report the requested execution speed throttling policy");
}

void catalogOffersPowerQoSBelowPriority() {
    const auto& registry = settings::builtInSettingsRegistry();
    const auto* section =
        registry.catalog().section(QStringLiteral("general"), QStringLiteral("core"));
    const auto* field = registry.fieldForSelect(kBinding);
    require(registry.isValid() && section && section->items.size() >= 2 && field &&
                section->items.at(0).id == QStringLiteral("system.application-priority") &&
                section->items.at(1).id == kPowerQoSId &&
                section->items.at(1).title.translated() == QStringLiteral("Power QoS") &&
                field->configurationKey == kPowerQoSKey &&
                field->definition == &section->items.at(1),
            "Power QoS appears directly below Application priority in General");
    const auto& options =
        std::get<settings::SettingsSelectDefinition>(field->definition->payload).options;
    require(options.size() == 3 && options.at(0).value == QStringLiteral("performance") &&
                options.at(0).label.translated() == QStringLiteral("Performance") &&
                options.at(1).value == QStringLiteral("normal") &&
                options.at(1).label.translated() == QStringLiteral("Normal") &&
                options.at(2).value == QStringLiteral("power_saving") &&
                options.at(2).label.translated() == QStringLiteral("Power saving"),
            "Power QoS presents the three requested choices in order");
}

void appliesAndPersistsProcessPolicy(const QString& path) {
    auto& configuration = storage::ApplicationStorage::instance().configuration();
    snow_shot::presentation::GlobalShortcutManager shortcuts;
    settings::BuiltInSettingsBackend backend(shortcuts);
    settings::SettingsRuntimeSession session(settings::builtInSettingsRegistry(), backend);
    require(session.state(kPowerQoSId).acceptedValue == QStringLiteral("performance"),
            "missing Power QoS preference defaults to Performance");

    require(session.applySelectValue(kBinding, QStringLiteral("power_saving")),
            "select Power saving");
    requireExecutionSpeedPolicy(kExecutionSpeed, kExecutionSpeed);
    require(session.applySelectValue(kBinding, QStringLiteral("performance")),
            "select Performance after Power saving");
    requireExecutionSpeedPolicy(kExecutionSpeed, 0);
    require(session.applySelectValue(kBinding, QStringLiteral("normal")),
            "select Normal after explicitly controlling the policy");
    requireExecutionSpeedPolicy(0, 0);

    require(session.applySelectValue(kBinding, QStringLiteral("power_saving")) &&
                session.state(kPowerQoSId).acceptedValue == QStringLiteral("power_saving") &&
                configuration.flushNow().success,
            "commit Power saving through the settings session");
    storage::ConfigurationStore persisted(path, true, false);
    require(persisted.value(kPowerQoSKey) == QStringLiteral("power_saving"),
            "the selected Power QoS survives a configuration reload");

    const auto previous = configuration.snapshot();
    require(!backend.applySelectValue(kBinding, QStringLiteral("invalid")) &&
                configuration.snapshot() == previous,
            "reject an invalid selection without changing configuration");
    requireExecutionSpeedPolicy(kExecutionSpeed, kExecutionSpeed);
    configuration.suspendWrites(true);
    require(!backend.applySelectValue(kBinding, QStringLiteral("performance")) &&
                configuration.snapshot() == previous,
            "reject a change when the preference cannot be saved");
    requireExecutionSpeedPolicy(kExecutionSpeed, kExecutionSpeed);
    require(!backend.resetSection(settings::SettingsSectionReset::SystemSettings) &&
                configuration.snapshot() == previous,
            "reject a section reset when preferences cannot be saved");
    requireExecutionSpeedPolicy(kExecutionSpeed, kExecutionSpeed);
    configuration.suspendWrites(false);

    require(session.reset(settings::SettingsSectionReset::SystemSettings) &&
                session.state(kPowerQoSId).acceptedValue == QStringLiteral("performance"),
            "the section reset restores Performance");
    requireExecutionSpeedPolicy(kExecutionSpeed, 0);

    auto imported = configuration.snapshot();
    imported.insert(kPowerQoSKey, QStringLiteral("normal"));
    require(backend.importConfigurationSnapshot(imported,
                                                storage::ConfigurationSchema::currentVersion()) &&
                configuration.value(kPowerQoSKey) == QStringLiteral("normal"),
            "import applies and saves the selected Normal policy");
    requireExecutionSpeedPolicy(0, 0);
    imported.remove(kPowerQoSKey);
    require(backend.importConfigurationSnapshot(imported,
                                                storage::ConfigurationSchema::currentVersion()) &&
                configuration.value(kPowerQoSKey) == QStringLiteral("performance"),
            "importing an older configuration restores Performance");
    requireExecutionSpeedPolicy(kExecutionSpeed, 0);
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    catalogOffersPowerQoSBelowPriority();
    QTemporaryDir temporary;
    require(temporary.isValid(), "create isolated Power QoS settings storage");
    auto& applicationStorage = storage::ApplicationStorage::instance();
    require(applicationStorage
                .initialize({temporary.filePath(QStringLiteral("bin")),
                             temporary.filePath(QStringLiteral("data")), 60000})
                .success,
            "initialize isolated application storage");
    appliesAndPersistsProcessPolicy(temporary.filePath(QStringLiteral("data/config.json")));
    applicationStorage.shutdown();
    return 0;
}
