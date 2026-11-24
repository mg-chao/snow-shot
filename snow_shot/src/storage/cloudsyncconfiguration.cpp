// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/storage/cloudsyncconfiguration.h"

#include "cloudsyncconfigurationkeys_p.h"
#include "snow_shot/mouseoperations.h"

#include "snow_shot/app/edition.h"
#include "snow_shot/shortcuts/shortcutbinding.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/exporttasks.h"
#include "snow_shot/storage/settingsadapters.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <iterator>

namespace snow_shot::storage {
namespace {

static_assert(std::size(detail::kCloudSettingKeys) == 291);

const auto kTranslationModelKey = QStringLiteral("screenshot_translation/model");
const auto kInterfaceLanguageKey = QStringLiteral("interface/language");
const auto kTrayMenuKey = QStringLiteral("tray/menu_options");
const auto kDarkTrayIconKey = QStringLiteral("tray/dark_icon");
const auto kGlobalShortcutPrefix = QStringLiteral("global_shortcuts/");
const auto kGlobalMousePrefix = QStringLiteral("global_mouse/");

const auto kLocalLayoutPositions = QStringLiteral("positions");
const auto kLocalLayoutHidden = QStringLiteral("hidden");

const QJsonValue kUnavailableCloudValue(QJsonValue::Undefined);

const ExportTaskRow* cloudSettingTaskRow(const QString& key) {
    const auto row = std::find_if(exportTaskRows.cbegin(), exportTaskRows.cend(),
                                  [&key](const auto& item) { return item.key == key; });
    return row == exportTaskRows.cend() ? nullptr : &*row;
}

QString builtInCloudTranslationId(const QString& id) {
    if (id == QStringLiteral("qwen3.8-flash") || id == QStringLiteral("qwen-mt-flash"))
        return QStringLiteral("ai-model");
    if (id == QStringLiteral("ai-model") || id == QStringLiteral("free-translation") ||
        id == QStringLiteral("fast-translation"))
        return id;
    return {};
}

bool layoutIds(const QString& key, const QJsonValue& raw, QSet<QString>& ids) {
    const auto addIds = [&ids](const QJsonValue& list) {
        if (!list.isArray())
            return false;
        for (const auto& item : list.toArray()) {
            if (!item.isString() || item.toString().isEmpty() || ids.contains(item.toString()))
                return false;
            ids.insert(item.toString());
        }
        return true;
    };
    if (key == kTrayMenuKey)
        return addIds(raw);
    const auto object = raw.toObject();
    const auto positions = object.value(kLocalLayoutPositions);
    if (!positions.isArray() || !addIds(object.value(kLocalLayoutHidden)))
        return false;
    for (const auto& position : positions.toArray()) {
        if (position.toArray().isEmpty() || !addIds(position))
            return false;
    }
    return true;
}

bool platformSupportsCloudKey(const QString& key) {
    if (legacyMouseCloudKey(key))
        return false;

#ifndef Q_OS_WIN
    if (key == QStringLiteral("system/annotation_mode_when_opening_for_editing") ||
        key == QStringLiteral("global_shortcuts/toggle_window_under_cursor_always_on_top"))
        return false;
#endif
#ifdef Q_OS_MACOS
    return key != QStringLiteral("tray/middle_click_action");
#else
    return key != QStringLiteral("export_tasks/screenshot_airdrop") &&
           key != QStringLiteral("export_tasks/pinned_airdrop");
#endif
}

bool deviceSupportsCloudValue(const QString& key, const QJsonValue& value) {
    const QString id = value.toString();
    if (!app::edition::textTranslation &&
        (id == QStringLiteral("translation") || id == QStringLiteral("text_translation")) &&
        (key == QStringLiteral("screenshot/double_click_action") ||
         key == QStringLiteral("screenshot/middle_mouse_button_action")))
        return false;
    return true;
}

QJsonArray projectCloudExportTasks(const ExportTaskRow& row, const QJsonArray& local,
                                   const QJsonArray& preserve) {
    QJsonArray result;
    for (const auto& option : exportTaskOptions) {
        const auto value = QJsonValue(option.id.toString());
        if (row.allowed.testFlag(option.task) &&
            (option.task == ExportTask::Upload ? preserve.contains(value) : local.contains(value)))
            result.append(value);
    }
    return result;
}

} // namespace

const QStringList& cloudSettingKeys() {
    static const QStringList keys = [] {
        QStringList result;
        result.reserve(static_cast<qsizetype>(std::size(detail::kCloudSettingKeys)));
        for (const auto* key : detail::kCloudSettingKeys)
            result.append(QString::fromLatin1(key));
        for (const auto& a : mouse::definitions())
            if (a.action != mouse::Action::Back)
                result.append(a.key());
        return result;
    }();
    return keys;
}

bool legacyMouseCloudKey(const QString& key) {
    return key == u"screenshot/double_click_action" ||
           key == u"screenshot/middle_mouse_button_action" ||
           key == u"pin_to_screen/double_click_action" ||
           key == u"pin_to_screen/middle_mouse_button_action";
}

bool cloudSettingSupported(const QString& key) {
    static const QSet<QString> keys(cloudSettingKeys().cbegin(), cloudSettingKeys().cend());
    return !app::edition::isMini && keys.contains(key) && ConfigurationSchema::contains(key) &&
           platformSupportsCloudKey(key);
}

bool cloudSettingPlatformSpecific(const QString& key) {
    return mouse::isKey(key) || key.startsWith(kGlobalShortcutPrefix) ||
           key.startsWith(kGlobalMousePrefix) ||
           key.startsWith(QStringLiteral("screenshot_shortcuts/")) ||
           key.startsWith(QStringLiteral("drawing_shortcuts/")) ||
           key.startsWith(QStringLiteral("pin_to_screen_shortcuts/")) ||
           key.startsWith(QStringLiteral("screen_recording_shortcuts/"));
}

bool cloudSettingLayout(const QString& key) {
    return key == kTrayMenuKey || key == QStringLiteral("floating_toolbar/layout") ||
           key == QStringLiteral("screenshot_toolbar/layout") ||
           key == QStringLiteral("screenshot_toolbar/action_tools_layout") ||
           key == QStringLiteral("pin_to_screen/action_tools_layout") ||
           key == QStringLiteral("screen_recording/action_tools_layout");
}

bool cloudSettingLayoutComplete(const QString& key, const QJsonValue& raw) {
    if (!cloudSettingLayout(key))
        return true;
    QSet<QString> originalIds;
    QSet<QString> normalizedIds;
    const auto normalized = ConfigurationSchema::normalize(key, raw);
    return normalized.valid && layoutIds(key, raw, originalIds) &&
           layoutIds(key, normalized.value, normalizedIds) &&
           std::all_of(originalIds.cbegin(), originalIds.cend(),
                       [&normalizedIds](const auto& id) { return normalizedIds.contains(id); });
}

QJsonValue cloudSettingValue(const QString& key, const QJsonValue& local) {
    if (!cloudSettingSupported(key) || !deviceSupportsCloudValue(key, local))
        return kUnavailableCloudValue;
    if (key == kTranslationModelKey) {
        const QString model = builtInCloudTranslationId(local.toString());
        return model.isEmpty() ? kUnavailableCloudValue : QJsonValue(model);
    }
    const auto normalized = ConfigurationSchema::normalize(key, local);
    if (!normalized.valid)
        return kUnavailableCloudValue;
    // Local file recovery salvages individual bindings. A cloud action is one atomic list.
    if (ConfigurationSchema::entry(key)->valueKind == ConfigurationValueKind::ShortcutList &&
        normalized.value.toArray().size() != local.toArray().size())
        return kUnavailableCloudValue;
    if (key == kInterfaceLanguageKey) {
        const QString locale = normalized.value.toString();
        if (locale != QStringLiteral("system") && locale != QStringLiteral("en_US") &&
            !QFile::exists(QStringLiteral(":/i18n/snow_shot_%1.qm").arg(locale))) {
            return kUnavailableCloudValue;
        }
    }
    if (const auto* row = cloudSettingTaskRow(key))
        return projectCloudExportTasks(*row, normalized.value.toArray(), {});
    return normalized.value;
}

QJsonValue cloudSettingLocalValue(const QString& key, const QJsonValue& cloud,
                                  const QJsonValue& local, const QString& localCustomTrayIconPath) {
    if (!cloudSettingSupported(key) || !deviceSupportsCloudValue(key, cloud))
        return kUnavailableCloudValue;
    if (key == kTranslationModelKey && !local.toString().isEmpty() &&
        builtInCloudTranslationId(local.toString()).isEmpty())
        return kUnavailableCloudValue;
    if (key == kDarkTrayIconKey && local.toString() == QStringLiteral("inherit") &&
        !localCustomTrayIconPath.trimmed().isEmpty() && cloud != local)
        return kUnavailableCloudValue;
    if (cloudSettingLayout(key)) {
        QSet<QString> ids;
        if (!layoutIds(key, cloud, ids))
            return kUnavailableCloudValue;
    }
    const auto value = cloudSettingValue(key, cloud);
    if (value.isUndefined())
        return kUnavailableCloudValue;
    if (const auto* row = cloudSettingTaskRow(key))
        return projectCloudExportTasks(*row, value.toArray(), local.toArray());
    return value;
}

bool cloudSettingShortcutChangesValid(const QMap<QString, QJsonValue>& current,
                                      const QMap<QString, QJsonValue>& changes) {
    if (!mouse::changesValid(current, changes))
        return false;
    QMap<QString, QJsonValue> appChanges;
    QMap<QString, QJsonValue> globalChanges;
    QMap<QString, QJsonValue> mouseChanges;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const auto* entry = ConfigurationSchema::entry(it.key());
        if (!entry)
            continue;
        if (it.key().startsWith(kGlobalMousePrefix)) {
            mouseChanges.insert(it.key(), it.value());
        } else if (entry->valueKind == ConfigurationValueKind::ShortcutList) {
            auto& group = it.key().startsWith(kGlobalShortcutPrefix) ? globalChanges : appChanges;
            group.insert(it.key(), it.value());
        }
    }

    if (!appChanges.isEmpty() && !localShortcutChangesValid(current, appChanges))
        return false;

    for (auto it = mouseChanges.cbegin(); it != mouseChanges.cend(); ++it) {
        if (it.value().toObject().isEmpty() || current.value(it.key()) == it.value())
            continue;
        for (auto old = current.lowerBound(kGlobalMousePrefix);
             old != current.cend() && old.key().startsWith(kGlobalMousePrefix); ++old) {
            if (old.key() != it.key() && mouseChanges.value(old.key(), old.value()) == it.value())
                return false;
        }
    }

    for (auto it = globalChanges.cbegin(); it != globalChanges.cend(); ++it) {
        if (current.value(it.key()) == it.value())
            continue;
        const auto bindings = shortcuts::shortcutBindingsFromJson(it.value(), false);
        if (bindings.isEmpty())
            continue;
        for (auto old = current.lowerBound(kGlobalShortcutPrefix);
             old != current.cend() && old.key().startsWith(kGlobalShortcutPrefix); ++old) {
            if (old.key() == it.key() || !cloudSettingSupported(old.key()))
                continue;
            const auto candidate = globalChanges.value(old.key(), old.value());
            const auto otherBindings = shortcuts::shortcutBindingsFromJson(candidate, false);
            for (const auto& binding : bindings) {
                if (std::any_of(otherBindings.cbegin(), otherBindings.cend(),
                                [&binding](const auto& other) {
                                    return shortcuts::bindingsConflict(binding, other);
                                }))
                    return false;
            }
        }
    }
    return true;
}

} // namespace snow_shot::storage
