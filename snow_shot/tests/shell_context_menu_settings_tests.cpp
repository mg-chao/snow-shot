#include "shell_context_menu_test_registry.h"
#include "snow_shot/app/distribution.h"
#include "snow_shot/presentation/globalshortcutmanager.h"
#include "snow_shot/presentation/languagemanager.h"
#include "snow_shot/presentation/settings/settingsbackend.h"
#include "snow_shot/presentation/settings/settingsformfield.h"
#include "snow_shot/presentation/settings/settingsregistry.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/presentation/settings/settingssearchindex.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/platform/windows/packagedcontextmenupreference.h"
#include "widgets/multi_select.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
using namespace shell_context_menu_tests;
namespace settings = snow_shot::presentation::settings;
namespace storage = snow_shot::storage;

const QString kKey = QStringLiteral("system/context_menu_mode");
const QString kId = QStringLiteral("system.context-menu-mode");
const QString kOptions = QStringLiteral("system/context_menu_options");
const QString kShortcutKey = QStringLiteral("context_menu_shortcuts/pin_to_screen");
constexpr auto kScope = settings::SettingsLocalShortcutScope::WindowsContextMenu;
constexpr auto kBinding = settings::SettingsSelectBinding::ContextMenuMode;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void migration(const QString& directory) {
    using Profile = storage::ConfigurationDefaults;
    for (const auto profile : {Profile::Standard, Profile::Portable}) {
        for (const auto& scope : {QString(), QStringLiteral("disabled"),
                                  QStringLiteral("image_files"), QStringLiteral("images_only")}) {
            for (int toggle = -1; toggle <= 1; ++toggle) {
                QJsonObject system;
                if (!scope.isEmpty())
                    system.insert(QStringLiteral("context_menu_mode"), scope);
                if (toggle >= 0)
                    system.insert(QStringLiteral("annotation_mode_when_opening_for_editing"),
                                  bool(toggle));
                QFile file(directory + QStringLiteral("/legacy.json"));
                require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                        "create legacy profile");
                file.write(
                    QJsonDocument(QJsonObject{{QStringLiteral("storage"),
                                               QJsonObject{{QStringLiteral("schema_version"), 11}}},
                                              {QStringLiteral("system"), system},
                                              {QStringLiteral("context_menu_shortcuts"),
                                               QJsonObject{{QStringLiteral("edit_with_snow_shot"),
                                                            QJsonArray{QStringLiteral("K")}}}}})
                        .toJson());
                file.close();
                storage::ConfigurationStore store(file.fileName(), true, true, 60000, nullptr,
                                                  profile);
                const auto effective =
                    scope.isEmpty() ? store.defaultValue(kKey).toString() : scope;
                const auto action = toggle == 0 ? QStringLiteral("pin_to_screen")
                                                : QStringLiteral("pin_to_screen_and_annotate");
                const auto key = QStringLiteral("context_menu_shortcuts/") + action;
                require(store.value(kKey) == effective &&
                            store.value(kOptions) ==
                                (effective == u"disabled" ? QJsonArray{} : QJsonArray{action}) &&
                            !store.value(key).toArray().isEmpty(),
                        "legacy defaults select the correct action and retain its shortcut");
                require(store.setValue(kOptions, QJsonArray{}) && store.flushNow().success,
                        "persist empty selection");
                storage::ConfigurationStore reload(file.fileName(), true, true, 60000, nullptr,
                                                   profile);
                require(reload.value(kOptions) == QJsonArray{}, "upgrade runs only once");
            }
        }
    }
}

void accessKeySchemaValidation() {
    using snow_shot::shortcuts::ShortcutBinding;
    using snow_shot::shortcuts::shortcutBindingsToJson;
    ShortcutBinding physical{QStringLiteral("E")};
    physical.physicalKeys.insert(snow_shot::shortcuts::ShortcutPlatform::MacOS, 14);
    for (const auto& value : {QJsonArray{QStringLiteral("e")}, shortcutBindingsToJson({physical}),
                              QJsonArray{QStringLiteral("7")}, QJsonArray{}}) {
        const auto normalized = storage::ConfigurationSchema::normalize(kShortcutKey, value);
        require(normalized.valid, "valid logical access keys normalize successfully");
        const auto keys = snow_shot::shortcuts::shortcutBindingsFromJson(normalized.value, false);
        require(keys.isEmpty() || (keys.size() == 1 && keys.first().physicalKeys.isEmpty() &&
                                   (keys.first().portableText == QStringLiteral("E") ||
                                    keys.first().portableText == QStringLiteral("7"))),
                "normalization uppercases letters and strips physical keyboard metadata");
    }
    for (const auto& value :
         {QJsonValue(QStringLiteral("E")), QJsonValue(QJsonArray{42}),
          QJsonValue(QJsonArray{QStringLiteral("Ctrl+E")}),
          QJsonValue(QJsonArray{QStringLiteral("Shift+E")}),
          QJsonValue(QJsonArray{QStringLiteral("Alt+E")}),
          QJsonValue(QJsonArray{QStringLiteral("Meta+E")}),
          QJsonValue(QJsonArray{QStringLiteral("F1")}), QJsonValue(QJsonArray{QStringLiteral(";")}),
          QJsonValue(QJsonArray{QStringLiteral("雪")}),
          QJsonValue(QJsonArray{QStringLiteral("E"), QStringLiteral("S")})})
        require(!storage::ConfigurationSchema::normalize(kShortcutKey, value).valid,
                "schema rejects malformed, modified, non-alphanumeric and multiple access keys");
}

void settingsTransactions(const QString& directory) {
    Registry registry;
    auto options = registry.options();
    options.packagedPreferencePath = directory + QStringLiteral("/menu-state");
    windows::ShellContextMenuRegistration shell(options);
    snow_shot::presentation::GlobalShortcutManager shortcuts;
    settings::BuiltInSettingsBackend backend(shortcuts, nullptr, nullptr, nullptr, nullptr, &shell);
    auto& config = storage::ApplicationStorage::instance().configuration();
    const auto& catalog = settings::builtInSettingsRegistry();
    require(catalog.isValid() && catalog.field(kId) &&
                catalog.field(QStringLiteral("system.context-menu-options")) &&
                !catalog.field(QStringLiteral("system.annotation-mode-when-opening-for-editing")),
            "catalog exposes options and scope");
    const auto before = config.value(kKey);
    require(config.value(kOptions) == QJsonArray{QStringLiteral("pin_to_screen")},
            "a new profile offers only plain pin");
    using snow_shot::shortcuts::ShortcutBinding;
    const auto edit = QStringLiteral("edit_images");
    const auto split = QStringLiteral("split_images");
    require(backend.applySelectValue(kBinding, QStringLiteral("disabled")) &&
                backend.applyLocalShortcuts(kScope, edit, {ShortcutBinding{QStringLiteral("e")}}) &&
                !backend.applyLocalShortcuts(kScope, split, {ShortcutBinding{QStringLiteral("E")}}),
            "disabled integration saves and reserves keys for unchecked actions");
    for (auto id : windows::shellActionIds)
        require(catalog.fieldForLocalShortcut(kScope, QString::fromLatin1(id)),
                "every action has a shortcut row");
    require(backend.applyMultiSelectValue(settings::SettingsMultiSelectBinding::ContextMenuOptions,
                                          {edit, split}) &&
                backend.applySelectValue(kBinding, QStringLiteral("images_only")),
            "enable image tools");
    const auto prefs = shell.preferences(config.snapshot());
    require(prefs.actions[0] == '-' && prefs.actions[4] == 'E' && prefs.actions[3] == '.',
            "published menu contains only visible access keys");
    if (snow_shot::app::distribution::isStoreManaged()) {
        require(windows::packaged_context_menu::readPreference(
                    options.packagedPreferencePath.toStdWString()) == prefs,
                "Store surrogate reads the same actions, shortcuts and scope");
    } else {
        const auto state = registry.keys;
        registry.writesBeforeFailure = 0;
        require(!backend.applySelectValue(kBinding, QStringLiteral("disabled")) &&
                    state == registry.keys && config.value(kKey) == QStringLiteral("images_only"),
                "failed registration rolls back settings and Explorer");
    }
    const auto editKey = QStringLiteral("context_menu_shortcuts/edit_images");
    const auto splitKey = QStringLiteral("context_menu_shortcuts/split_images");
    settings::SettingsRuntimeSession session(catalog, backend);
    require(session.applyContextMenuShortcutBatch(
                {{editKey, QJsonArray{QStringLiteral("S")}},
                 {splitKey, QJsonArray{QStringLiteral("E")}}}),
            "a shortcut batch can reassign both access keys atomically");
    const auto updated = shell.preferences(config.snapshot());
    require(updated.actions[3] == 'E' && updated.actions[4] == 'S',
            "the batch commits both shortcuts");
    if (snow_shot::app::distribution::isStoreManaged()) {
        require(windows::packaged_context_menu::readPreference(
                    options.packagedPreferencePath.toStdWString()) == updated,
                "the Store projection publishes the batch's access keys");
    } else {
        const auto state = registry.keys;
        registry.writesBeforeFailure = 0;
        require(!session.applyContextMenuShortcutBatch(
                    {{editKey, QJsonArray{QStringLiteral("E")}},
                     {splitKey, QJsonArray{QStringLiteral("S")}}}) &&
                    registry.keys == state && shell.preferences(config.snapshot()) == updated,
                "a failed batch keeps both settings and the native menu unchanged");
    }
    QMap<QString, storage::ConfigurationWriteOrigin> origins;
    const auto connection = QObject::connect(
        &config, &storage::ConfigurationStore::valuesCommitted, &backend,
        [&](storage::ConfigurationWriteOrigin origin, const auto&, const auto& values) {
            for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                origins.insert(it.key(), origin);
        });
    auto imported = config.snapshot();
    imported.insert(kOptions, QJsonArray{QStringLiteral("compress_images")});
    imported.remove(editKey);
    const QSet<QString> explicitlySupplied{kOptions};
    require(backend.importConfigurationSnapshot(imported,
                                                storage::ConfigurationSchema::currentVersion(),
                                                nullptr, &explicitlySupplied) &&
                origins.value(kOptions) == storage::ConfigurationWriteOrigin::Import &&
                origins.value(editKey) == storage::ConfigurationWriteOrigin::ImportDefaults &&
                shell.preferences(config.snapshot()).actions[5] == '.',
            "imports publish the selected actions and preserve the origin of omitted shortcuts");
    require(backend.resetSection(settings::SettingsSectionReset::WindowsContextMenuShortcuts) &&
                origins.value(editKey) == storage::ConfigurationWriteOrigin::Reset,
            "resetting the shortcut section records an explicit reset for the access keys");
    QObject::disconnect(connection);
    require(backend.applySelectValue(kBinding, before.toVariant()), "restore fixture scope");

    auto& language = snow_shot::presentation::LanguageManager::instance();
    language.initialize();
    require(language.setLanguage(QStringLiteral("zh_CN")), "load the Chinese settings labels");
    QWidget owner;
    auto* field = settings::SettingsFormField::create(
        *catalog.field(QStringLiteral("system.context-menu-options")), session, {.parent = &owner});
    auto* select = qobject_cast<adqt::widgets::AdMultiSelect*>(field->editor());
    require(select != nullptr, "context menu options use a multi-select");
    auto* placeholder = select->findChild<QLabel*>(QStringLiteral("adselect-placeholder"));
    select->setSelectedValues({});
    require(placeholder && !placeholder->isHidden() &&
                placeholder->text() == QStringLiteral("选择操作"),
            "clearing the selection shows the action placeholder");
    select->setSelectedValues({QStringLiteral("pin_to_screen")});
    require(placeholder->isHidden(), "selecting an action hides the placeholder");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "create isolated test storage");
    migration(temporary.path());
    accessKeySchemaValidation();
    auto& appStorage = storage::ApplicationStorage::instance();
    require(appStorage.initialize({temporary.path(), temporary.path(), 60000}).success,
            "initialize configuration");
    settingsTransactions(temporary.path());
    appStorage.shutdown();
    return 0;
}
