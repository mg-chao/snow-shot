#include "snow_shot/shortcuts/localshortcutcontext.h"
#include "snow_shot/storage/configurationstore.h"
#include "snow_shot/clouduploadconfiguration.h"
#include "snow_shot/app/edition.h"
#if SNOW_SHOT_ENABLE_API_CONFIGURATION
#include "snow_shot/customaimodelconfiguration.h"
#include "snow_shot/texttranslationconfiguration.h"
#endif

#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/storagelogging.h"
#include "snow_shot/shortcuts/shortcutbinding.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QPointer>
#include <QThreadPool>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <utility>

namespace snow_shot::storage {
namespace {
const QString kSchemaVersionKey = QStringLiteral("storage/schema_version");
const QString kCloudSyncStateKey = QStringLiteral("_cloud_settings_sync");
#if SNOW_SHOT_ENABLE_API_CONFIGURATION
const QString kCustomModelsKey = QStringLiteral("api_configuration/custom_models");
#endif

enum class ConfigurationOverlayPolicy {
    MergeFromDisk,
    ReplaceAll,
};

struct MaterializedConfiguration {
    QMap<QString, QJsonValue> values;
    QJsonObject document;
    bool dirty = false;
    bool customConfigurationsRepaired = false;
};

QJsonValue valueAtPath(const QJsonObject& root, const QString& path, bool* present = nullptr) {
    if (present != nullptr) {
        *present = false;
    }
    const QStringList parts = path.split(u'/');
    if (parts.size() != 2 || !root.value(parts[0]).isObject()) {
        return {};
    }
    const QJsonObject group = root.value(parts[0]).toObject();
    if (!group.contains(parts[1])) {
        return {};
    }
    if (present != nullptr) {
        *present = true;
    }
    return group.value(parts[1]);
}

void insertPath(QJsonObject* root, const QString& path, const QJsonValue& value) {
    const QStringList parts = path.split(u'/');
    if (root == nullptr || parts.size() != 2) {
        return;
    }
    QJsonObject group = root->value(parts[0]).toObject();
    group.insert(parts[1], value);
    root->insert(parts[0], group);
}

QMap<QString, QJsonValue> overlayFromDocument(const QJsonObject& document) {
    QMap<QString, QJsonValue> overlay;
    for (const ConfigurationSchemaEntry& entry : ConfigurationSchema::entries()) {
        if (entry.key == kSchemaVersionKey) {
            continue;
        }
        bool present = false;
        const QJsonValue raw = valueAtPath(document, entry.key, &present);
        if (present) {
            overlay.insert(entry.key, raw);
        }
    }
    for (const QString& legacyKey :
         {QStringLiteral("system/annotation_mode_when_opening_for_editing"),
          QStringLiteral("context_menu_shortcuts/edit_with_snow_shot"),
          QStringLiteral("capture_history/enabled"),
          QStringLiteral("screenshot/auto_save_after_copy"),
          QStringLiteral("screenshot/auto_save_after_execution_actions"),
          QStringLiteral("screenshot/copy_after_pinning_to_screen"),
          QStringLiteral("screenshot/screenshot_sound_notification"),
          QStringLiteral("interface/app_font_size_percentage"),
          QStringLiteral("pin_to_screen_shortcuts/show_text_recognition_results"),
          QStringLiteral("screenshot_selection/selection_target")}) {
        bool present = false;
        const QJsonValue raw = valueAtPath(document, legacyKey, &present);
        if (present) {
            overlay.insert(legacyKey, raw);
        }
    }
    return overlay;
}

void reconcileLocalShortcutDefaults(MaterializedConfiguration& result,
                                    const QSet<QString>& defaultedKeys, bool mutateDocument,
                                    bool replaceAll) {
    if (defaultedKeys.isEmpty())
        return;

    QList<std::pair<QString, shortcuts::ShortcutBinding>> assigned;
    QStringList defaultedLocalKeys;
    const auto bindingsForKey = [&result](const QString& key) {
        return shortcuts::shortcutBindingsFromJson(
            result.values.value(key), ConfigurationSchema::allowsModifierOnlyShift(key), -1,
            nullptr, nullptr, key == QStringLiteral("screenshot_shortcuts/toggle_guides"));
    };
    // Reserve every explicit binding first, including entries later in the schema.
    // Only missing or invalid local settings may yield their default alternatives.
    for (const auto& entry : ConfigurationSchema::entries()) {
        if (entry.valueKind != ConfigurationValueKind::ShortcutList ||
            entry.key.startsWith(QStringLiteral("global_shortcuts/")))
            continue;
        if (defaultedKeys.contains(entry.key)) {
            defaultedLocalKeys.push_back(entry.key);
        } else {
            for (const auto& binding : bindingsForKey(entry.key))
                assigned.append({entry.key, binding});
        }
    }
    for (const QString& key : defaultedLocalKeys) {
        const auto defaults = bindingsForKey(key);
        shortcuts::ShortcutBindingList retained;
        for (const auto& binding : defaults) {
            if (std::any_of(
                    assigned.cbegin(), assigned.cend(), [&binding, &key](const auto& existing) {
                        return shortcuts::localShortcutKeysMayConflict(key, existing.first) &&
                               shortcuts::bindingsConflict(existing.second, binding,
                                                           shortcuts::ShortcutPolicy::Application);
                    }))
                continue;
            retained.push_back(binding);
            assigned.push_back({key, binding});
        }
        if (retained.size() == defaults.size())
            continue;
        const QJsonArray value = shortcuts::shortcutBindingsToJson(retained);
        result.values.insert(key, value);
        if (mutateDocument) {
            insertPath(&result.document, key, value);
            result.dirty = result.dirty || !replaceAll;
        }
    }
}

#ifdef Q_OS_WIN
void migrateLegacyMenuProfiles(QJsonObject& document, const QMap<QString, QJsonValue>& overlay,
                               const QJsonValue& scope) {
    auto journal = document.value(kCloudSyncStateKey).toObject();
    if (journal.value(QStringLiteral("version")).toInt() != 1)
        return;

    // Scope and the single access key belonged to this installation before v12. Capture
    // them while reading that file, before restoring another account can change them.
    const auto scopeKey = QStringLiteral("system/context_menu_mode");
    const auto annotationKey = QStringLiteral("system/annotation_mode_when_opening_for_editing");
    const auto pinKey = QStringLiteral("context_menu_shortcuts/pin_to_screen");
    const auto annotateKey = QStringLiteral("context_menu_shortcuts/pin_to_screen_and_annotate");
    const auto access = ConfigurationSchema::normalize(
        pinKey, overlay.value(QStringLiteral("context_menu_shortcuts/edit_with_snow_shot")));
    const auto migrate = [&](QJsonObject values) {
        if (!values.contains(scopeKey))
            values.insert(scopeKey, scope);
        if (!values.contains(annotationKey))
            values.insert(annotationKey, true);
        const auto& shortcut = values.value(annotationKey).toBool(true) ? annotateKey : pinKey;
        if (access.valid && !values.contains(shortcut))
            values.insert(shortcut, access.value);
        return values;
    };

    const auto baseline = QStringLiteral("signed_out");
    journal.insert(baseline, migrate(journal.value(baseline).toObject()));
    const auto profilesKey = QStringLiteral("profiles");
    auto profiles = journal.value(profilesKey).toObject();
    for (auto it = profiles.begin(); it != profiles.end(); ++it) {
        auto profile = it.value().toObject();
        const auto valuesKey = QStringLiteral("values");
        profile.insert(valuesKey, migrate(profile.value(valuesKey).toObject()));
        if (!profile.value(QStringLiteral("joined")).toBool()) {
            const auto seedKey = QStringLiteral("seed");
            profile.insert(seedKey, migrate(profile.value(seedKey).toObject()));
        }
        it.value() = profile;
    }
    journal.insert(profilesKey, profiles);
    document.insert(kCloudSyncStateKey, journal);
}
#endif

MaterializedConfiguration materializeConfiguration(const QMap<QString, QJsonValue>& overlay,
                                                   const QMap<QString, QJsonValue>& defaults,
                                                   QJsonObject document, int schemaVersion,
                                                   ConfigurationCompatibility compatibility,
                                                   ConfigurationOverlayPolicy policy) {
    MaterializedConfiguration result;
    result.document = std::move(document);
    const bool mutateDocument = compatibility != ConfigurationCompatibility::FutureVersion;
    const bool replaceAll = policy == ConfigurationOverlayPolicy::ReplaceAll;
    QMap<QString, QJsonValue> currentOverlay =
        mutateDocument ? ConfigurationSchema::migrateLegacyValues(
                             overlay, schemaVersion,
                             defaults.value(QStringLiteral("system/context_menu_mode")).toString())
                       : overlay;
    mouse::reconcile(currentOverlay);
    if (mutateDocument) {
        for (const auto& [group, key] :
             {std::pair{QStringLiteral("system"),
                        QStringLiteral("annotation_mode_when_opening_for_editing")},
              std::pair{QStringLiteral("context_menu_shortcuts"),
                        QStringLiteral("edit_with_snow_shot")}}) {
            auto fields = result.document.value(group).toObject();
            if (!fields.take(key).isUndefined()) {
                result.document.insert(group, fields);
                result.dirty = !replaceAll;
            }
        }
        auto history = result.document.value(QStringLiteral("capture_history")).toObject();
        if (history.contains(QStringLiteral("enabled"))) {
            history.remove(QStringLiteral("enabled"));
            result.document.insert(QStringLiteral("capture_history"), history);
            result.dirty = !replaceAll;
        }
        QJsonObject interfaceValues = result.document.value(QStringLiteral("interface")).toObject();
        if (interfaceValues.contains(QStringLiteral("app_font_size_percentage"))) {
            interfaceValues.remove(QStringLiteral("app_font_size_percentage"));
            result.document.insert(QStringLiteral("interface"), interfaceValues);
            result.dirty = !replaceAll;
        }
        QJsonObject screenshot = result.document.value(QStringLiteral("screenshot")).toObject();
        for (const auto& key : {QStringLiteral("auto_save_after_copy"),
                                QStringLiteral("auto_save_after_execution_actions"),
                                QStringLiteral("copy_after_pinning_to_screen"),
                                QStringLiteral("screenshot_sound_notification")}) {
            if (screenshot.contains(key)) {
                screenshot.remove(key);
                result.document.insert(QStringLiteral("screenshot"), screenshot);
                result.dirty = !replaceAll;
            }
        }
        auto pinnedShortcuts =
            result.document.value(QStringLiteral("pin_to_screen_shortcuts")).toObject();
        if (!pinnedShortcuts.take(QStringLiteral("show_text_recognition_results")).isUndefined()) {
            result.document.insert(QStringLiteral("pin_to_screen_shortcuts"), pinnedShortcuts);
            result.dirty = !replaceAll;
        }
        QJsonObject screenshotSelection =
            result.document.value(QStringLiteral("screenshot_selection")).toObject();
        if (screenshotSelection.contains(QStringLiteral("selection_target"))) {
            screenshotSelection.remove(QStringLiteral("selection_target"));
            result.document.insert(QStringLiteral("screenshot_selection"), screenshotSelection);
            result.dirty = !replaceAll;
        }
        if (screenshotSelection.contains(QStringLiteral("previous_selection"))) {
            screenshotSelection.remove(QStringLiteral("previous_selection"));
            result.document.insert(QStringLiteral("screenshot_selection"), screenshotSelection);
            result.dirty = !replaceAll;
        }
    }
    QSet<QString> defaultedShortcutKeys;

    for (const ConfigurationSchemaEntry& entry : ConfigurationSchema::entries()) {
        if (entry.key == kSchemaVersionKey) {
            continue;
        }
        const QJsonValue defaultValue = defaults.value(entry.key);

        const bool present = currentOverlay.contains(entry.key);
        if (!present) {
            result.values.insert(entry.key, defaultValue);
            if (entry.valueKind == ConfigurationValueKind::ShortcutList)
                defaultedShortcutKeys.insert(entry.key);
            if (mutateDocument) {
                insertPath(&result.document, entry.key, defaultValue);
                if (!replaceAll) {
                    result.dirty = true;
                }
            }
            continue;
        }

        QJsonValue raw = currentOverlay.value(entry.key);
#ifdef Q_OS_MACOS
        if (mutateDocument && schemaVersion < 7 && entry.key == QStringLiteral("updates/mode")) {
            // Enable Sparkle for all existing macOS installations once. Version 7 records
            // the migration so later user choices survive reloads and current snapshots.
            raw = QStringLiteral("next_launch");
        }
#endif
        if (mutateDocument && schemaVersion < 6 && raw == QStringLiteral("dedicated") &&
            (entry.key == QStringLiteral("screenshot_table/model") ||
             entry.key == QStringLiteral("screenshot_latex/model"))) {
            raw = defaultValue;
        }
#if SNOW_SHOT_ENABLE_TEXT_TRANSLATION
        if (entry.key == QStringLiteral("screenshot_translation/model") && mutateDocument &&
            schemaVersion < 5 && !raw.toString().startsWith(QStringLiteral("custom:")) &&
            !raw.toString().startsWith(QStringLiteral("translation:"))) {
            raw = entry.defaultValue;
        }
#endif
        // Upgrade only the untouched previous tray default. A customized list keeps its order.
        if (entry.key == QStringLiteral("tray/menu_options") && mutateDocument &&
            !currentOverlay.contains(
                QStringLiteral("global_shortcuts/restore_last_closed_windows"))) {
            QJsonArray previousDefault = entry.defaultValue.toArray();
            for (qsizetype i = previousDefault.size(); i > 0; --i) {
                if (previousDefault.at(i - 1).toString() ==
                    QStringLiteral("quick.restore-last-closed-windows"))
                    previousDefault.removeAt(i - 1);
            }
            if (raw.toArray() == previousDefault) {
                raw = entry.defaultValue;
                insertPath(&result.document, entry.key, raw);
                result.dirty = true;
            }
        }
        if (entry.key == QStringLiteral("cloud_upload/configuration")) {
            bool valid = false;
            const auto canonical =
                cloudUploadSettingsToJson(cloudUploadSettingsFromJson(raw, &valid));
            result.values.insert(entry.key, canonical);
            result.customConfigurationsRepaired = result.customConfigurationsRepaired || !valid;
            if (replaceAll)
                insertPath(&result.document, entry.key, canonical);
            continue;
        }
        bool migratedDestroyShortcut = false;
        if (entry.key == QStringLiteral("pin_to_screen_shortcuts/destroy_window") &&
            mutateDocument && schemaVersion < 3) {
            const auto previousDefault =
                ConfigurationSchema::normalize(entry.key, QJsonArray{QStringLiteral("Ctrl+Esc")});
            const auto stored = ConfigurationSchema::normalize(entry.key, raw);
            if (stored.valid && stored.value == previousDefault.value) {
                raw = entry.defaultValue;
                migratedDestroyShortcut = true;
            }
        }
#if SNOW_SHOT_ENABLE_API_CONFIGURATION
        if (entry.key == kCustomModelsKey ||
            entry.key == QStringLiteral("api_configuration/text_translation")) {
            bool valid = false;
            const QJsonValue canonical =
                entry.key == kCustomModelsKey
                    ? customAiModelsToJson(customAiModelsFromJson(raw, &valid))
                    : textTranslationConfigurationsToJson(
                          textTranslationConfigurationsFromJson(raw, &valid));
            result.values.insert(entry.key, canonical);
            result.customConfigurationsRepaired = result.customConfigurationsRepaired || !valid;
            if (replaceAll) {
                insertPath(&result.document, entry.key, canonical);
            }
            continue;
        }

#endif
        const ConfigurationNormalization normalized =
            ConfigurationSchema::normalize(entry.key, raw);
        if (!normalized.valid) {
            result.values.insert(entry.key, defaultValue);
            if (entry.valueKind == ConfigurationValueKind::ShortcutList)
                defaultedShortcutKeys.insert(entry.key);
            if (mutateDocument) {
                insertPath(&result.document, entry.key, defaultValue);
                if (!replaceAll) {
                    result.dirty = true;
                }
            }
            continue;
        }

        result.values.insert(entry.key, normalized.value);
        const bool migratedLegacyValue =
            !overlay.contains(entry.key) || overlay.value(entry.key) != raw;
        if (mutateDocument &&
            (replaceAll || normalized.changed || migratedDestroyShortcut || migratedLegacyValue)) {
            insertPath(&result.document, entry.key, normalized.value);
            if ((normalized.changed || migratedDestroyShortcut || migratedLegacyValue) &&
                !replaceAll) {
                result.dirty = true;
            }
        }
    }

    reconcileLocalShortcutDefaults(result, defaultedShortcutKeys, mutateDocument, replaceAll);
    QSet<QString> assignedMenuKeys;
    for (auto it = result.values.lowerBound(QStringLiteral("context_menu_shortcuts/"));
         it != result.values.end() && it.key().startsWith(u"context_menu_shortcuts/"); ++it) {
        const auto bindings = shortcuts::shortcutBindingsFromJson(it.value(), false);
        if (bindings.isEmpty())
            continue;
        const auto& key = bindings.first().portableText;
        if (assignedMenuKeys.contains(key)) {
            it.value() = QJsonArray{};
            if (mutateDocument) {
                insertPath(&result.document, it.key(), it.value());
                result.dirty = true;
            }
        }
        assignedMenuKeys.insert(key);
    }

#ifdef Q_OS_WIN
    if (!replaceAll && schemaVersion < 12)
        migrateLegacyMenuProfiles(result.document, overlay,
                                  result.values.value(QStringLiteral("system/context_menu_mode")));
#endif
    int resolvedVersion = schemaVersion;
    const int currentVersion = ConfigurationSchema::currentVersion();
    if (mutateDocument && resolvedVersion < currentVersion) {
        resolvedVersion = currentVersion;
        if (!replaceAll) {
            result.dirty = true;
        }
    }
    result.values.insert(kSchemaVersionKey, resolvedVersion);
    if (mutateDocument && (replaceAll || resolvedVersion != schemaVersion)) {
        insertPath(&result.document, kSchemaVersionKey, resolvedVersion);
    }
    return result;
}

QString corruptBackupPath(const QString& configurationFile) {
    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
    const QString base = configurationFile + QStringLiteral(".corrupt.") + timestamp;
    QString candidate = base + QStringLiteral(".json");
    for (int suffix = 1; QFileInfo::exists(candidate); ++suffix) {
        candidate = base + QStringLiteral(".%1.json").arg(suffix);
    }
    return candidate;
}

void preserveCorruptFile(const QString& configurationFile) {
    if (!QFileInfo::exists(configurationFile)) {
        return;
    }
    const QString backup = corruptBackupPath(configurationFile);
    if (QFile::copy(configurationFile, backup)) {
        qCWarning(storageLog) << "Preserved invalid configuration at" << backup;
    } else {
        qCWarning(storageLog) << "Unable to preserve invalid configuration" << configurationFile;
    }
}

void cleanupCorruptBackups(const QString& configurationFile) {
    const QFileInfo configurationInfo(configurationFile);
    if (configurationFile.isEmpty() || configurationInfo.absolutePath().isEmpty()) {
        return;
    }
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-30);
    const QString pattern = configurationInfo.fileName() + QStringLiteral(".corrupt.*.json");
    const QFileInfoList backups =
        QDir(configurationInfo.absolutePath())
            .entryInfoList({pattern}, QDir::Files | QDir::Hidden | QDir::System, QDir::Time);
    for (const QFileInfo& backup : backups) {
        if (backup.lastModified().toUTC() >= cutoff || QFile::remove(backup.absoluteFilePath())) {
            continue;
        }
        qCWarning(storageLog) << "Unable to remove expired configuration backup"
                              << backup.absoluteFilePath();
    }
}

QByteArray serializedDocument(const QJsonObject& document) {
    QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (!bytes.endsWith('\n')) {
        bytes.append('\n');
    }
    return bytes;
}

QMap<QString, QJsonValue> configurationDefaults(ConfigurationDefaults profile) {
    QMap<QString, QJsonValue> defaults;
    for (const auto& entry : ConfigurationSchema::entries())
        defaults.insert(entry.key, entry.defaultValue);
    if (profile == ConfigurationDefaults::Portable) {
        defaults.insert(QStringLiteral("system/auto_start_at_boot"), false);
        defaults.insert(QStringLiteral("system/context_menu_mode"), QStringLiteral("disabled"));
    }
    return defaults;
}
} // namespace

ConfigurationStore::ConfigurationStore(QString configurationFile, bool readAvailable,
                                       bool writeAvailable, int debounceMilliseconds,
                                       QObject* parent, ConfigurationDefaults defaults)
    : QObject(parent), m_configurationFile(std::move(configurationFile)),
      m_readAvailable(readAvailable), m_writeAvailable(writeAvailable),
      m_defaults(configurationDefaults(defaults)) {
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(std::max(0, debounceMilliseconds));
    connect(&m_flushTimer, &QTimer::timeout, this, [this] {
        if (!isDirty())
            return;
        if (m_flushWorker)
            flushAsync(this, {});
        else
            static_cast<void>(flushNow());
    });
    load();
}

ConfigurationStore::~ConfigurationStore() {
    if (m_flushWorker)
        m_flushWorker->waitForDone();
}

int ConfigurationStore::currentSchemaVersion() {
    return ConfigurationSchema::currentVersion();
}

QJsonValue ConfigurationStore::value(const QString& key) const {
    QMutexLocker locker(&m_mutex);
    return m_values.value(key, defaultValue(key));
}

QJsonValue ConfigurationStore::defaultValue(const QString& key) const {
    return m_defaults.value(key);
}

QMap<QString, QJsonValue> ConfigurationStore::snapshot() const {
    QMutexLocker locker(&m_mutex);
    return m_values;
}

mouse::Bindings ConfigurationStore::mouseBindings() const {
    QMutexLocker locker(&m_mutex);
    return m_mouseBindings;
}

std::optional<mouse::Action> ConfigurationStore::mouseAction(mouse::Category category,
                                                             mouse::Binding binding) const {
    QMutexLocker locker(&m_mutex);
    return m_mouseBindings.match(category, binding);
}

quint64 ConfigurationStore::revision() const {
    QMutexLocker locker(&m_mutex);
    return m_revision;
}

bool ConfigurationStore::mutateIfRevision(quint64 expectedRevision,
                                          const std::function<bool()>& mutation, bool* conflict) {
    QMutexLocker mutationLock(&m_mutationMutex);
    const bool stale = revision() != expectedRevision;
    if (conflict)
        *conflict = stale;
    return !stale && mutation && mutation();
}

bool ConfigurationStore::isDirty() const {
    QMutexLocker locker(&m_mutex);
    return m_dirty || m_journalSnapshot == JournalSnapshot::Scheduled;
}

void ConfigurationStore::afterPendingChanges(const std::function<void()>& transition) {
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker mutationLock(&m_mutationMutex);
    {
        QScopedValueRollback delivering(m_deliveringChanges, true);
        while (!m_pendingChanges.isEmpty())
            m_pendingChanges.takeFirst()();
    }
    if (transition)
        transition();
}

bool ConfigurationStore::isWritable() const {
    QMutexLocker locker(&m_mutex);
    return m_writeAvailable && m_compatibility != ConfigurationCompatibility::FutureVersion;
}

void ConfigurationStore::setCloudSyncJournal(CloudSyncJournal journal,
                                             std::function<QJsonObject()> snapshot) {
    QMutexLocker lock(&m_mutationMutex);
    Q_ASSERT(bool(journal) == bool(snapshot));
    materializeCloudSyncJournal();
    m_cloudSyncJournal = std::move(journal);
    m_snapshotCloudSyncJournal = std::move(snapshot);
}

bool ConfigurationStore::deferCloudSyncStatus() {
    QMutexLocker mutationLock(&m_mutationMutex);
    QMutexLocker lock(&m_mutex);
    if (!m_snapshotCloudSyncJournal || m_suspended || !m_writeAvailable ||
        m_compatibility == ConfigurationCompatibility::FutureVersion)
        return false;
    if (m_journalSnapshot == JournalSnapshot::Current)
        m_journalSnapshot = JournalSnapshot::OnNextFlush;
    return true;
}

// The mutation lock keeps the snapshot and the matching preference values in one transaction.
void ConfigurationStore::materializeCloudSyncJournal() {
    if (m_journalSnapshot == JournalSnapshot::Current)
        return;
    const auto state = m_snapshotCloudSyncJournal();
    QMutexLocker lock(&m_mutex);
    m_journalSnapshot = JournalSnapshot::Current;
    if (m_document.value(kCloudSyncStateKey).toObject() != state) {
        if (state.isEmpty())
            m_document.remove(kCloudSyncStateKey);
        else
            m_document.insert(kCloudSyncStateKey, state);
        ++m_documentRevision;
        m_dirty = true;
    }
}

QString ConfigurationStore::lastError() const {
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

ConfigurationCompatibility ConfigurationStore::compatibility() const {
    QMutexLocker locker(&m_mutex);
    return m_compatibility;
}

bool ConfigurationStore::setValue(const QString& key, const QJsonValue& value,
                                  ConfigurationWriteOrigin origin) {
    return setValues({{key, value}}, origin);
}

bool ConfigurationStore::setValues(const QMap<QString, QJsonValue>& values,
                                   ConfigurationWriteOrigin origin) {
    return setValuesImpl(values, origin, nullptr);
}

QJsonObject ConfigurationStore::cloudSyncState() const {
    QMutexLocker mutationLock(&m_mutationMutex);
    if (m_journalSnapshot != JournalSnapshot::Current)
        return m_snapshotCloudSyncJournal();
    QMutexLocker locker(&m_mutex);
    return m_document.value(kCloudSyncStateKey).toObject();
}

bool ConfigurationStore::setCloudSyncState(const QJsonObject& state,
                                           const QMap<QString, QJsonValue>& remoteValues) {
    return setValuesImpl(remoteValues, ConfigurationWriteOrigin::Remote, &state);
}

bool ConfigurationStore::setValuesImpl(const QMap<QString, QJsonValue>& values,
                                       ConfigurationWriteOrigin origin,
                                       const QJsonObject* cloudSyncState) {
    QMutexLocker mutationLock(&m_mutationMutex);
    QMap<QString, QJsonValue> normalizedValues;
    const bool mouseChanged = std::any_of(values.keyBegin(), values.keyEnd(),
                                          [](const QString& key) { return mouse::isKey(key); });
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!ConfigurationSchema::contains(it.key())) {
            rejectMutation(it.key(), QStringLiteral("The configuration key is not supported"));
            return false;
        }
        const ConfigurationNormalization normalized =
            ConfigurationSchema::normalize(it.key(), it.value());
        if (!normalized.valid) {
            rejectMutation(it.key(), QStringLiteral("The configuration value is invalid"));
            return false;
        }
        normalizedValues.insert(it.key(), normalized.value);
    }

    QMap<QString, QJsonValue> menuShortcuts;
    for (auto it = normalizedValues.lowerBound(QStringLiteral("context_menu_shortcuts/"));
         it != normalizedValues.end() && it.key().startsWith(u"context_menu_shortcuts/"); ++it)
        menuShortcuts.insert(it.key(), it.value());
    if (!menuShortcuts.isEmpty() && !localShortcutChangesValid(snapshot(), menuShortcuts)) {
        rejectMutation(menuShortcuts.firstKey(),
                       tr("This context menu shortcut is already assigned"));
        return false;
    }
    if (mouseChanged && !mouse::changesValid(snapshot(), normalizedValues)) {
        rejectMutation(normalizedValues.firstKey(), tr("The mouse operation is already assigned."));
        return false;
    }
    QVector<QPair<QString, QJsonValue>> changed;
    QMap<QString, QJsonValue> previous;
    bool documentChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_suspended || !m_writeAvailable ||
            m_compatibility == ConfigurationCompatibility::FutureVersion) {
            locker.unlock();
            rejectMutation(values.isEmpty() ? QString() : values.cbegin().key(),
                           QStringLiteral("Configuration storage is read-only"));
            return false;
        }
        for (auto it = normalizedValues.cbegin(); it != normalizedValues.cend(); ++it) {
            const auto before = m_values.value(it.key());
            previous.insert(it.key(), before);
            if (before == it.value()) {
                continue;
            }
            m_values.insert(it.key(), it.value());
            insertPath(&m_document, it.key(), it.value());
            changed.push_back({it.key(), it.value()});
        }
        const bool stateChanged =
            cloudSyncState && m_document.value(kCloudSyncStateKey).toObject() != *cloudSyncState;
        if (cloudSyncState)
            m_journalSnapshot = JournalSnapshot::Current;
        if (stateChanged) {
            if (cloudSyncState->isEmpty())
                m_document.remove(kCloudSyncStateKey);
            else
                m_document.insert(kCloudSyncStateKey, *cloudSyncState);
        }
        if (mouseChanged)
            m_mouseBindings = mouse::Bindings(m_values);
        documentChanged = stateChanged || !changed.isEmpty();
        if (documentChanged) {
            m_dirty = true;
            ++m_documentRevision;
        }
        if (!changed.isEmpty())
            ++m_revision;
    }

    announceChanges(origin, std::move(previous), std::move(normalizedValues), std::move(changed),
                    documentChanged);
    return true;
}

void ConfigurationStore::announceChanges(ConfigurationWriteOrigin origin,
                                         QMap<QString, QJsonValue> previous,
                                         QMap<QString, QJsonValue> values,
                                         QVector<QPair<QString, QJsonValue>> changed,
                                         bool documentChanged) {
    if (!documentChanged && values.isEmpty())
        return;
    if (m_cloudSyncJournal && !values.isEmpty() && origin != ConfigurationWriteOrigin::Remote &&
        origin != ConfigurationWriteOrigin::Internal &&
        m_cloudSyncJournal(origin, previous, values)) {
        QMutexLocker lock(&m_mutex);
        m_journalSnapshot = JournalSnapshot::Scheduled;
        documentChanged = true;
    }
    auto notify = [this, origin, previous = std::move(previous), values = std::move(values),
                   changed = std::move(changed), documentChanged]() {
        if (!values.isEmpty())
            emit valuesCommitted(origin, previous, values);
        for (const auto& item : changed) {
            emit valueChanged(item.first, item.second);
        }
        if (documentChanged)
            scheduleFlush();
    };
    const bool onStoreThread = QThread::currentThread() == thread();
    if (onStoreThread && m_pendingChanges.isEmpty() && !m_deliveringChanges) {
        notify();
        return;
    }
    m_pendingChanges.append(std::move(notify));
    if (onStoreThread && !m_deliveringChanges)
        afterPendingChanges({});
    else if (!onStoreThread && m_pendingChanges.size() == 1)
        QMetaObject::invokeMethod(this, [this] { afterPendingChanges({}); }, Qt::QueuedConnection);
}

bool ConfigurationStore::applySnapshot(const QMap<QString, QJsonValue>& values, int schemaVersion,
                                       const QSet<QString>* explicitlyImported) {
    QMutexLocker mutationLock(&m_mutationMutex);
    const int currentVersion = ConfigurationSchema::currentVersion();
    if (schemaVersion <= 0) {
        schemaVersion = currentVersion;
    } else if (schemaVersion > currentVersion) {
        rejectMutation({}, QStringLiteral("Configuration schema is newer than this application"));
        return false;
    }

    MaterializedConfiguration materialized = materializeConfiguration(
        values, m_defaults, {}, schemaVersion, ConfigurationCompatibility::Current,
        ConfigurationOverlayPolicy::ReplaceAll);
    const QString customModelsError =
        materialized.customConfigurationsRepaired
            ? tr("Some custom API configurations are invalid and were ignored")
            : QString();

    QVector<QPair<QString, QJsonValue>> changed;
    QMap<QString, QJsonValue> importedValues;
    if (explicitlyImported) {
        for (const auto& key : *explicitlyImported) {
            const auto supplied = values.constFind(key);
            if (supplied == values.cend())
                continue;
            const auto normalized = ConfigurationSchema::normalize(key, supplied.value());
            if (normalized.valid && normalized.value == materialized.values.value(key))
                importedValues.insert(key, normalized.value);
        }
    }
    QMap<QString, QJsonValue> previousImported;
    QMap<QString, QJsonValue> internalValues;
    QMap<QString, QJsonValue> previousInternal;
    {
        QMutexLocker locker(&m_mutex);
        if (m_suspended || !m_writeAvailable ||
            m_compatibility == ConfigurationCompatibility::FutureVersion) {
            locker.unlock();
            rejectMutation({}, QStringLiteral("Configuration storage is read-only"));
            return false;
        }
        for (auto it = materialized.values.cbegin(); it != materialized.values.cend(); ++it) {
            const auto before = m_values.value(it.key());
            if (importedValues.contains(it.key()))
                previousImported.insert(it.key(), before);
            if (before == it.value()) {
                continue;
            }
            if (!importedValues.contains(it.key())) {
                previousInternal.insert(it.key(), before);
                internalValues.insert(it.key(), it.value());
            }
            changed.push_back({it.key(), it.value()});
        }
        if (!changed.isEmpty()) {
            if (m_document.contains(kCloudSyncStateKey))
                materialized.document.insert(kCloudSyncStateKey,
                                             m_document.value(kCloudSyncStateKey));
            m_values = std::move(materialized.values);
            m_mouseBindings = mouse::Bindings(m_values);
            m_document = std::move(materialized.document);
            m_dirty = true;
            ++m_revision;
            ++m_documentRevision;
        }
    }

    if (!customModelsError.isEmpty()) {
        qCWarning(storageLog) << customModelsError;
        setLastError(customModelsError);
    } else if (!changed.isEmpty()) {
        setLastError({});
    }
    announceChanges(ConfigurationWriteOrigin::Import, std::move(previousImported),
                    std::move(importedValues), {}, false);
    const bool documentChanged = !changed.isEmpty();
    announceChanges(explicitlyImported ? ConfigurationWriteOrigin::ImportDefaults
                                       : ConfigurationWriteOrigin::Internal,
                    std::move(previousInternal), std::move(internalValues), std::move(changed),
                    documentChanged);
    return true;
}

StorageResult ConfigurationStore::flushNow() {
    if (QThread::currentThread() == thread()) {
        m_flushTimer.stop();
    }
    for (;;) {
        QMutexLocker mutationLock(&m_mutationMutex);
        QMutexLocker ioLocker(&m_ioMutex);
        materializeCloudSyncJournal();
        QJsonObject document;
        quint64 revision = 0;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_dirty) {
                return StorageResult::ok();
            }
            if (!m_writeAvailable || m_configurationFile.isEmpty() ||
                m_compatibility == ConfigurationCompatibility::FutureVersion) {
                const QString error = QStringLiteral("Configuration storage is read-only");
                locker.unlock();
                setLastError(error);
                return StorageResult::failure(error);
            }
            document = m_document;
            revision = m_documentRevision;
        }
        // Explicit operations can journal their intent while holding the mutation lock.
        // Save the captured document without holding that lock during disk I/O.
        mutationLock.unlock();

        const QByteArray bytes = serializedDocument(document);
        QSaveFile file(m_configurationFile);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
            !file.commit()) {
            file.cancelWriting();
            const QString error = QStringLiteral("Unable to write config.json atomically");
            qCWarning(storageLog) << error << m_configurationFile;
            setLastError(error);
            return StorageResult::failure(error);
        }

        {
            QMutexLocker locker(&m_mutex);
            if (m_documentRevision == revision) {
                m_dirty = false;
                if (m_journalSnapshot == JournalSnapshot::Scheduled)
                    continue;
                locker.unlock();
                setLastError({});
                return StorageResult::ok();
            }
        }
    }
}

void ConfigurationStore::flushAsync(QObject* receiver,
                                    std::function<void(StorageResult)> completion) {
    Q_ASSERT(QThread::currentThread() == thread());
    m_flushTimer.stop();
    if (!m_flushWorker) {
        m_flushWorker = std::make_unique<QThreadPool>();
        m_flushWorker->setMaxThreadCount(1);
    }
    m_flushWorker->start([this, receiver = QPointer(receiver), completion = std::move(completion)] {
        const auto result = flushNow();
        if (completion) {
            QMetaObject::invokeMethod(
                this,
                [receiver, completion, result] {
                    if (receiver)
                        completion(result);
                },
                Qt::QueuedConnection);
        }
    });
}

void ConfigurationStore::suspendWrites(bool suspended) {
    {
        QMutexLocker mutationLock(&m_mutationMutex);
        // Account changes can still arrive during a storage move. Freeze the accepted edits
        // before suspension so a checkpoint cannot capture a later, rejected transition.
        if (suspended)
            materializeCloudSyncJournal();
        QMutexLocker lock(&m_mutex);
        if (m_suspended == suspended)
            return;
        m_suspended = suspended;
        if (suspended)
            m_flushTimer.stop();
    }
    if (!suspended)
        emit writesResumed();
}

void ConfigurationStore::relocate(const QString& directory) {
    QMutexLocker ioLock(&m_ioMutex);
    QMutexLocker lock(&m_mutex);
    m_configurationFile = QDir(directory).filePath(QStringLiteral("config.json"));
}

void ConfigurationStore::load() {
    cleanupCorruptBackups(m_configurationFile);
    QMap<QString, QJsonValue> loaded = m_defaults;
    QJsonObject document;
    for (auto it = m_defaults.cbegin(); it != m_defaults.cend(); ++it)
        insertPath(&document, it.key(), it.value());
    ConfigurationCompatibility compatibility = ConfigurationCompatibility::Current;
    int version = 1;
    bool dirty = false;
    QString error;

    if (!m_readAvailable || m_configurationFile.isEmpty()) {
        compatibility = ConfigurationCompatibility::Unavailable;
        dirty = true;
        error = QStringLiteral("Configuration storage is not readable");
    } else if (!QFileInfo::exists(m_configurationFile)) {
        dirty = true;
        qCInfo(storageLog) << "Configuration file is missing; schema defaults will be written";
    } else {
        QFile file(m_configurationFile);
        QJsonParseError parseError;
        const bool opened = file.open(QIODevice::ReadOnly);
        const QJsonDocument parsed =
            opened ? QJsonDocument::fromJson(file.readAll(), &parseError) : QJsonDocument();
        if (!opened || parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            compatibility = ConfigurationCompatibility::RecoveredDefaults;
            dirty = true;
            error = opened ? QStringLiteral("config.json is malformed; defaults were loaded")
                           : QStringLiteral("Unable to read config.json; defaults were loaded");
            preserveCorruptFile(m_configurationFile);
            qCWarning(storageLog) << error;
        } else {
            const QJsonObject parsedObject = parsed.object();
            const QJsonValue versionValue = valueAtPath(parsedObject, kSchemaVersionKey);
            if (!ConfigurationSchema::parseIntegerVersion(versionValue, &version)) {
                compatibility = ConfigurationCompatibility::RecoveredDefaults;
                version = 1;
                dirty = true;
                error = QStringLiteral(
                    "config.json has an invalid schema version; defaults were loaded");
                preserveCorruptFile(m_configurationFile);
                qCWarning(storageLog) << error;
            } else {
                document = parsedObject;
                if (version > ConfigurationSchema::currentVersion()) {
                    compatibility = ConfigurationCompatibility::FutureVersion;
                    error = QStringLiteral("Configuration schema is newer than this application; "
                                           "storage is read-only");
                    qCWarning(storageLog) << error << version;
                }
                const MaterializedConfiguration materialized = materializeConfiguration(
                    overlayFromDocument(parsedObject), m_defaults, parsedObject, version,
                    compatibility, ConfigurationOverlayPolicy::MergeFromDisk);
                loaded = materialized.values;
                document = materialized.document;
                dirty = materialized.dirty;
                if (materialized.customConfigurationsRepaired) {
                    error = tr("Some custom API configurations are invalid and were ignored");
                    qCWarning(storageLog) << error;
                }
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        m_values = std::move(loaded);
        m_mouseBindings = mouse::Bindings(m_values);
        m_document = std::move(document);
        m_compatibility = compatibility;
        m_dirty = dirty && compatibility != ConfigurationCompatibility::FutureVersion;
        m_lastError = error;
    }
    if (dirty && compatibility != ConfigurationCompatibility::FutureVersion) {
        scheduleFlush();
    }
}

void ConfigurationStore::scheduleFlush() {
    if (isWritable()) {
        m_flushTimer.start();
    }
}

void ConfigurationStore::setLastError(const QString& error) {
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        changed = m_lastError != error;
        m_lastError = error;
    }
    if (changed) {
        emit errorChanged(error);
    }
}

void ConfigurationStore::rejectMutation(const QString& key, const QString& error) {
    setLastError(error);
    if (QThread::currentThread() == thread()) {
        emit mutationRejected(key, error);
    } else {
        QMetaObject::invokeMethod(
            this, [this, key, error]() { emit mutationRejected(key, error); },
            Qt::QueuedConnection);
    }
}
} // namespace snow_shot::storage
