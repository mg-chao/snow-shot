#include "snow_shot/storage/cloudsyncconfiguration.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/exporttasks.h"
#include "snow_shot/app/edition.h"
#include "snow_shot/shortcuts/shortcutbinding.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
using snow_shot::storage::ConfigurationSchema;
using snow_shot::storage::ConfigurationSchemaEntry;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QString outputDirectory(QStandardPaths::StandardLocation location) {
    const QString directory = QStandardPaths::writableLocation(location);
    return directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                               : directory;
}

QJsonArray metadata() {
    constexpr const char* kinds[] = {"Boolean",    "Integer",      "String",
                                     "StringList", "ShortcutList", "Structured"};
    QJsonArray result;
    for (const auto& entry : ConfigurationSchema::entries()) {
        QJsonArray allowed;
        for (const auto& item : entry.allowedStringValues)
            allowed.append(item);
        QJsonValue range;
        if (entry.integerRange)
            range = QJsonArray{entry.integerRange->minimum, entry.integerRange->maximum,
                               entry.integerRange->step};
        QJsonValue value = entry.defaultValue;
        // The reference captures all actual factory results. Only these two
        // environment-dependent values use markers; they are checked separately.
        if (entry.key == QStringLiteral("screen_recording/video_save_directory"))
            value = QStringLiteral("$movies-directory");
        else if (entry.key == QStringLiteral("screenshot/image_save_directory"))
            value = QStringLiteral("$pictures-directory");
        result.append(QJsonObject{
            {QStringLiteral("key"), entry.key},
            {QStringLiteral("defaultValue"), value},
            {QStringLiteral("defaultJsonType"), static_cast<int>(entry.defaultValue.type())},
            {QStringLiteral("defaultStringIsNull"),
             entry.defaultValue.isString() && entry.defaultValue.toString().isNull()},
            {QStringLiteral("valueKind"),
             QString::fromLatin1(kinds[static_cast<int>(entry.valueKind)])},
            {QStringLiteral("integerRange"), range},
            {QStringLiteral("allowedStringValues"), allowed},
            {QStringLiteral("maximumListItems"), entry.maximumListItems},
        });
    }
    return result;
}

void publicMetadataMatchesOriginal(const QJsonArray& actual) {
#ifdef Q_OS_MACOS
    require(
        !ConfigurationSchema::defaultValue(QStringLiteral("system/auto_start_at_boot")).toBool(),
        "macOS login registration defaults off in both editions");
#else
    require(ConfigurationSchema::defaultValue(QStringLiteral("system/auto_start_at_boot")).toBool(),
            "other platforms retain the enabled startup default");
#endif

#ifdef Q_OS_WIN
    QFile file(QString::fromUtf8(SNOW_SCHEMA_BASELINE_JSON));
    require(file.open(QIODevice::ReadOnly), "the original schema metadata fixture must open");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(),
            "the original schema metadata fixture must be valid JSON");
    const auto fixture = document.object();
    require(fixture.value(QStringLiteral("originalSourceSha256")).toString() ==
                QStringLiteral("8ceb23d8125a6c950b33f3a486629fdcb5844939b2f1f1bdff5376ac77c1ca8f"),
            "the reference must come from the frozen original initializer");
    require(fixture.value(QStringLiteral("edition")).toString() ==
                (snow_shot::app::edition::isMini ? QStringLiteral("mini") : QStringLiteral("full")),
            "the reference must match the linked storage edition");
    const auto expected = fixture.value(QStringLiteral("entries")).toArray();
    if (actual.size() != expected.size()) {
        std::cerr << "schema entries: actual=" << actual.size() << " expected=" << expected.size()
                  << '\n';
        const auto keys = [](const QJsonArray& entries) {
            QSet<QString> result;
            for (const auto& entry : entries)
                result.insert(entry.toObject().value(QStringLiteral("key")).toString());
            return result;
        };
        const auto actualKeys = keys(actual);
        const auto expectedKeys = keys(expected);
        for (const auto& key : actualKeys - expectedKeys)
            std::cerr << "missing fixture key: " << key.toStdString() << '\n';
        for (const auto& key : expectedKeys - actualKeys)
            std::cerr << "unexpected fixture key: " << key.toStdString() << '\n';
    }
    require(actual.size() == expected.size(), "schema entry counts must match the original");
    for (qsizetype i = 0; i < expected.size(); ++i) {
        if (actual.at(i) != expected.at(i)) {
            std::cerr
                << "schema metadata changed at entry " << i << ": "
                << actual.at(i).toObject().value(QStringLiteral("key")).toString().toStdString()
                << "\nactual: " << QJsonDocument(actual.at(i).toObject()).toJson().constData()
                << "expected: " << QJsonDocument(expected.at(i).toObject()).toJson().constData();
            std::exit(1);
        }
    }
#else
    // The frozen field-by-field references are native Windows Full/Mini runs.
    // Other hosts still exercise the common contracts and native defaults below.
    require(actual.size() == ConfigurationSchema::entries().size(),
            "every native entry must expose complete metadata");
#endif
}

void entryReferencesAndCopiesRemainIndependent() {
    const auto& entries = ConfigurationSchema::entries();
    require(!entries.isEmpty(), "the schema must contain entries");
    const auto* storage = entries.constData();
    QSet<QString> keys;
    bool checkedAllowedValues = false;
    bool checkedObject = false;
    bool checkedArray = false;
    for (const auto& entry : entries) {
        require(!entry.key.isEmpty() && !keys.contains(entry.key),
                "schema keys must remain nonempty and unique");
        keys.insert(entry.key);
        require(ConfigurationSchema::entry(entry.key) == &entry &&
                    ConfigurationSchema::contains(entry.key) &&
                    ConfigurationSchema::defaultValue(entry.key) == entry.defaultValue,
                "indexed lookups must return the canonical entry and default");
        ConfigurationSchemaEntry copy = entry;
        copy.key[0] = QChar(u'#');
        require(copy.key != entry.key && ConfigurationSchema::entry(entry.key) == &entry,
                "copied keys must detach from immutable schema text");
        if (!entry.allowedStringValues.isEmpty() && !entry.allowedStringValues.front().isEmpty()) {
            copy.allowedStringValues[0][0] =
                entry.allowedStringValues.front().front() == QChar(u'#') ? QChar(u'?')
                                                                         : QChar(u'#');
            require(copy.allowedStringValues != entry.allowedStringValues,
                    "copied allowed values must detach from immutable schema text");
            checkedAllowedValues = true;
        }
        if (entry.defaultValue.isObject()) {
            auto object = copy.defaultValue.toObject();
            object.insert(QStringLiteral("schema-test-copy"), true);
            copy.defaultValue = object;
            require(copy.defaultValue != entry.defaultValue,
                    "copied JSON object defaults must remain independent");
            checkedObject = true;
        } else if (entry.defaultValue.isArray()) {
            auto array = copy.defaultValue.toArray();
            array.append(QStringLiteral("schema-test-copy"));
            copy.defaultValue = array;
            require(copy.defaultValue != entry.defaultValue,
                    "copied JSON array defaults must remain independent");
            checkedArray = true;
        }
    }
    auto detachedEntries = entries;
    detachedEntries[0].key = QStringLiteral("schema-test-replacement");
    require(entries.constData() == storage && &ConfigurationSchema::entries() == &entries &&
                ConfigurationSchema::entry(entries.front().key) == &entries.front(),
            "lookups and detached vector copies must preserve canonical entry addresses");
    require(checkedAllowedValues && checkedObject && checkedArray,
            "copy independence must cover option strings and structured defaults");
}

void nativeDefaultsRemainIntact(const QString& movies, const QString& pictures) {
    require(ConfigurationSchema::defaultValue(
                QStringLiteral("screen_recording/video_save_directory")) == movies &&
                ConfigurationSchema::defaultValue(
                    QStringLiteral("screenshot/image_save_directory")) == pictures,
            "directory factories must preserve eager native defaults before application/test-mode "
            "changes");
    require(
        ConfigurationSchema::defaultValue(QStringLiteral("text_recognition/resident_process")) ==
                QJsonValue(!snow_shot::app::edition::isMini) &&
            ConfigurationSchema::defaultValue(
                QStringLiteral("pin_to_screen/automatic_text_recognition")) ==
                QJsonValue(!snow_shot::app::edition::isMini),
        "edition-specific default booleans must remain intact");
#ifdef Q_OS_MACOS
    require(ConfigurationSchema::defaultValue(QStringLiteral("updates/mode")) ==
                    QStringLiteral("next_launch") &&
                ConfigurationSchema::defaultValue(QStringLiteral(
                    "screenshot_ui/selection_display_unit")) == QStringLiteral("logical_pixels"),
            "native macOS update and coordinate defaults must remain intact");
    for (const auto& binding : {std::pair{"global_shortcuts/screenshot", 18u},
                                std::pair{"global_shortcuts/screenshot_copy", 19u},
                                std::pair{"global_shortcuts/pin_clipboard_content", 20u},
                                std::pair{"global_shortcuts/restore_last_closed_windows", 20u}}) {
        const auto values = snow_shot::shortcuts::shortcutBindingsFromJson(
            ConfigurationSchema::defaultValue(QString::fromLatin1(binding.first)), true);
        require(values.size() == 1 &&
                    values.front().physicalKeys.value(
                        snow_shot::shortcuts::ShortcutPlatform::MacOS) == binding.second,
                "macOS physical shortcut bindings must remain attached to their defaults");
    }
#endif
}

void cloudSyncConfiguration() {
    using namespace snow_shot::storage;
    const auto& keys = cloudSettingKeys();
    const QSet<QString> unique(keys.cbegin(), keys.cend());
    require(keys.size() == 347 && unique.size() == keys.size() &&
                std::count_if(keys.cbegin(), keys.cend(), cloudSettingPlatformSpecific) == 195,
            "cloud allowlist requires all 347 unique schema keys and 195 platform records");
    const QString themeKey = QStringLiteral("interface/theme_mode");
    const QString fontKey = QStringLiteral("interface/app_font_size");
    const QString tasksKey = QStringLiteral("export_tasks/screenshot_copy");
    const auto scopedTasksKey = exportTaskScopedKey(ExportTrigger::ScreenshotCopy);
    const auto autoUpload = QJsonArray{QStringLiteral("upload_to_cloud")};
    const auto eligibleTask = QJsonArray{QStringLiteral("auto_save")};
    const QString layoutKey = QStringLiteral("tray/menu_options");
    const auto futureLayout =
        QJsonArray{QStringLiteral("quick.screenshot"), QStringLiteral("future.item")};
    require(!cloudSettingSupported(QStringLiteral("interface/skin_path")) &&
                !cloudSettingSupported(QStringLiteral("api_configuration/server_url")) &&
                !cloudSettingSupported(QStringLiteral("screenshot/unknown_value")),
            "cloud rejects unlisted settings even with a listed prefix");
    if constexpr (snow_shot::app::edition::isMini) {
        require(!cloudSettingSupported(themeKey), "Mini has no settings sync");
    } else {
        const QJsonArray scopedUpload{QStringLiteral("upload_to_cloud.focused_window")};
        const QJsonArray scopedSave{QStringLiteral("auto_save.scrolling")};
        require(cloudSettingSupported(scopedTasksKey) &&
                    cloudSettingValue(tasksKey, scopedSave) == QJsonArray{} &&
                    cloudSettingValue(scopedTasksKey, eligibleTask) == QJsonArray{} &&
                    cloudSettingValue(scopedTasksKey, scopedSave) == scopedSave &&
                    cloudSettingValue(scopedTasksKey, scopedUpload) == QJsonArray{} &&
                    cloudSettingLocalValue(scopedTasksKey, QJsonArray{}, scopedUpload) ==
                        scopedUpload,
                "scoped settings sync independently and keep automatic upload local");
        require(cloudSettingSupported(themeKey) &&
                    cloudSettingValue(tasksKey, autoUpload) == QJsonArray{} &&
                    cloudSettingLocalValue(tasksKey, QJsonArray{}, autoUpload) == autoUpload &&
                    cloudSettingLocalValue(tasksKey, eligibleTask, autoUpload) ==
                        QJsonArray{QStringLiteral("auto_save"), QStringLiteral("upload_to_cloud")},
                "export task projection always preserves local automatic upload");
        require(!cloudSettingLayoutComplete(layoutKey, futureLayout) &&
                    cloudSettingLocalValue(layoutKey, futureLayout, {}) ==
                        QJsonArray{QStringLiteral("quick.screenshot")},
                "layout display drops unknown commands without losing the incomplete status");
        const QString languageKey = QStringLiteral("interface/language");
        require(cloudSettingValue(languageKey, QStringLiteral("system")) ==
                        QStringLiteral("system") &&
                    cloudSettingLocalValue(languageKey, QStringLiteral("es_ES"),
                                           QStringLiteral("en_US"))
                        .isUndefined(),
                "locale preferences require a translation catalog on the receiving device");
        const QString regularTrayIcon = QStringLiteral("tray/icon");
        const QString darkTrayIcon = QStringLiteral("tray/dark_icon");
        const QString localIconPath = QStringLiteral("icons/device-specific.png");
        const QString dedicatedIcon = QStringLiteral("snow-dark");
        const QString inheritedIcon = QStringLiteral("inherit");
        require(cloudSettingLocalValue(darkTrayIcon, dedicatedIcon, inheritedIcon, localIconPath)
                        .isUndefined() &&
                    cloudSettingLocalValue(darkTrayIcon, dedicatedIcon, inheritedIcon) ==
                        dedicatedIcon &&
                    cloudSettingLocalValue(regularTrayIcon, dedicatedIcon,
                                           QStringLiteral("default"),
                                           localIconPath) == dedicatedIcon,
                "dark mode keeps inherited custom artwork while built-in choices still sync");
        const QString modelKey = QStringLiteral("screenshot_translation/model");
        if (cloudSettingSupported(modelKey)) {
            const QString custom = QStringLiteral("translation:local-account-only");
            const QString builtIn = QStringLiteral("free-translation");
            require(cloudSettingValue(modelKey, custom).isUndefined() &&
                        cloudSettingLocalValue(modelKey, builtIn, custom).isUndefined(),
                    "custom translation services remain local in both directions");
        }
    }

    QTemporaryDir directory;
    require(directory.isValid(), "configuration test directory should be available");
    const auto path = directory.filePath(QStringLiteral("config.json"));
    ConfigurationStore store(path, true, true);
    QList<ConfigurationWriteOrigin> origins;
    QList<QMap<QString, QJsonValue>> operations;
    QObject::connect(&store, &ConfigurationStore::valuesCommitted, &store,
                     [&origins, &operations](auto origin, const auto&, const auto& values) {
                         origins.append(origin);
                         operations.append(values);
                     });
    const auto unchanged = store.value(themeKey);
    const auto revision = store.revision();
    require(store.setValue(themeKey, unchanged, ConfigurationWriteOrigin::Reset) &&
                origins == QList{ConfigurationWriteOrigin::Reset} &&
                operations.front().value(themeKey) == unchanged && store.revision() == revision,
            "same-value resets still record the explicit reset operation");

    const QJsonObject syncState{{QStringLiteral("pending"), true}};
    require(store.setCloudSyncState(syncState, {{themeKey, QStringLiteral("dark")}}) &&
                origins.back() == ConfigurationWriteOrigin::Remote && store.flushNow().success,
            "remote settings and their sync state share a durable transaction");
    const ConfigurationStore reloaded(path, true, false);
    require(reloaded.cloudSyncState() == syncState &&
                reloaded.value(themeKey) == QStringLiteral("dark") &&
                !reloaded.snapshot().contains(QStringLiteral("_cloud_settings_sync")),
            "only the live configuration document carries private sync state");

    origins.clear();
    operations.clear();
    const QSet explicitlyImported{fontKey};
    require(store.applySnapshot({{fontKey, store.value(fontKey)}}, 9, &explicitlyImported) &&
                origins == QList{ConfigurationWriteOrigin::Import,
                                 ConfigurationWriteOrigin::ImportDefaults} &&
                operations.front().size() == 1 && operations.front().contains(fontKey) &&
                operations.back().contains(themeKey) && store.cloudSyncState() == syncState,
            "import reports a supplied preference separately from omitted replacement defaults");

    const QString syncColorKey = QStringLiteral("drawing/sync_tool_colors");
    const QString shapeKey = QStringLiteral("drawing/shape_style");
    const QString sharedColorKey = QStringLiteral("drawing/shared_tool_color");
    const QString stroke = QStringLiteral("stroke");
    const QString localColor = QStringLiteral("#112233FF");
    require(store.setValue(shapeKey, QJsonObject{{stroke, localColor}}),
            "the test device chooses its own color");
    QMap<QString, QJsonValue> enable{{syncColorKey, true}};
    require(applyDrawingColorPolicy(store, enable) && enable.value(sharedColorKey) == localColor &&
                store.setCloudSyncState(syncState, enable) &&
                !store.value(shapeKey).toObject().contains(stroke),
            "a downloaded color toggle keeps the device's effective local color");
    QMap<QString, QJsonValue> disable{{syncColorKey, false}};
    require(applyDrawingColorPolicy(store, disable) &&
                disable.value(shapeKey).toObject().value(stroke) == localColor,
            "turning off shared color on another device preserves local tool appearance");
}
void journalSnapshotsCoalesce() {
    using namespace snow_shot::storage;
    QTemporaryDir directory;
    require(directory.isValid(), "journal checkpoint directory");
    const auto path = directory.filePath(QStringLiteral("config.json"));
    const auto font = QStringLiteral("interface/app_font_size");
    ConfigurationStore store(path, true, true);
    QJsonObject journal;
    int snapshots = 0;
    store.setCloudSyncJournal(
        [&](auto origin, const auto&, const auto& values) {
            journal.insert(font, values.value(font));
            journal.insert(QStringLiteral("reset"), origin == ConfigurationWriteOrigin::Reset);
            return true;
        },
        [&] {
            ++snapshots;
            return journal;
        });
    for (int size : {18, 20, 22})
        require(store.setValue(font, size), "edit a preference before the checkpoint");
    require(snapshots == 0 && store.isDirty(), "edits journal intent without building snapshots");
    require(store.flushNow().success && snapshots == 1, "one flush materializes one journal");
    const ConfigurationStore saved(path, true, false);
    require(saved.value(font).toInt() == 22 && saved.cloudSyncState() == journal,
            "the latest journal and preference share the checkpoint");

    require(store.setValue(font, 22, ConfigurationWriteOrigin::Reset) && store.isDirty(),
            "same-value intent can require a journal-only checkpoint");
    store.setCloudSyncJournal({}, {});
    require(snapshots == 2 && store.flushNow().success &&
                ConfigurationStore(path, true, false).cloudSyncState() == journal,
            "detaching the journal preserves pending intent for shutdown");
}
} // namespace

int main(int argc, char** argv) {
    const QString movies = outputDirectory(QStandardPaths::MoviesLocation);
    const QString pictures = outputDirectory(QStandardPaths::PicturesLocation);
    QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const auto before = metadata();
    publicMetadataMatchesOriginal(before);
    nativeDefaultsRemainIntact(movies, pictures);
    entryReferencesAndCopiesRemainIndependent();
    cloudSyncConfiguration();
    journalSnapshotsCoalesce();
    require(metadata() == before, "copy mutation must not change any public schema field");
    return 0;
}
