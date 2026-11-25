// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/storage/exporttasks.h"

#include <QCoreApplication>

namespace snow_shot::storage {
namespace {
using Type = ScreenshotType;
constexpr ExportTaskOption options[] = {
    {ExportTask::CopyImage, Type::All, u"copy_to_clipboard",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy to clipboard")},
    {ExportTask::CopyImage, Type::Normal, u"copy_to_clipboard.normal",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy to clipboard (Normal)")},
    {ExportTask::CopyImage, Type::Scrolling, u"copy_to_clipboard.scrolling",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy to clipboard (Scrolling screenshot)")},
    {ExportTask::CopyFile, Type::All, u"copy_file",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy file")},
    {ExportTask::CopyFile, Type::Normal, u"copy_file.normal",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy file (Normal)")},
    {ExportTask::CopyFile, Type::Scrolling, u"copy_file.scrolling",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Copy file (Scrolling screenshot)")},
    {ExportTask::AutoSave, Type::All, u"auto_save",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Auto-save")},
    {ExportTask::AutoSave, Type::Normal, u"auto_save.normal",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Auto-save (Normal)")},
    {ExportTask::AutoSave, Type::Scrolling, u"auto_save.scrolling",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Auto-save (Scrolling screenshot)")},
    {ExportTask::AutoSave, Type::FocusedWindow, u"auto_save.focused_window",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Auto-save (Focused window)")},
    {ExportTask::AutoSave, Type::CurrentMonitor, u"auto_save.current_monitor",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Auto-save (Current monitor)")},
    {ExportTask::Pin, Type::All, u"pin_to_screen",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Pin to screen")},
    {ExportTask::Pin, Type::Normal, u"pin_to_screen.normal",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Pin to screen (Normal)")},
    {ExportTask::Pin, Type::Scrolling, u"pin_to_screen.scrolling",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Pin to screen (Scrolling screenshot)")},
    {ExportTask::Pin, Type::FocusedWindow, u"pin_to_screen.focused_window",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Pin to screen (Focused window)")},
    {ExportTask::Pin, Type::CurrentMonitor, u"pin_to_screen.current_monitor",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Pin to screen (Current monitor)")},
    {ExportTask::Upload, Type::All, u"upload_to_cloud",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Upload to cloud")},
    {ExportTask::Upload, Type::Normal, u"upload_to_cloud.normal",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Upload to cloud (Normal)")},
    {ExportTask::Upload, Type::Scrolling, u"upload_to_cloud.scrolling",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Upload to cloud (Scrolling screenshot)")},
    {ExportTask::Upload, Type::FocusedWindow, u"upload_to_cloud.focused_window",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Upload to cloud (Focused window)")},
    {ExportTask::Upload, Type::CurrentMonitor, u"upload_to_cloud.current_monitor",
     QT_TRANSLATE_NOOP("SettingsCatalog", "Upload to cloud (Current monitor)")},
};

bool supportsType(ExportTrigger trigger, ScreenshotType type) {
    if (type == Type::All || trigger == ExportTrigger::ScreenshotCopy)
        return true;
    switch (trigger) {
    case ExportTrigger::ScreenshotSave:
    case ExportTrigger::ScreenshotPin:
    case ExportTrigger::ScreenshotPrint:
    case ExportTrigger::ScreenshotAirDrop:
    case ExportTrigger::ScreenshotUpload:
        return type == Type::Normal || type == Type::Scrolling;
    default:
        return false;
    }
}
} // namespace

const QList<ExportTaskOption>& exportTaskOptions(ExportTrigger trigger) {
    static const auto byTrigger = [] {
        std::array<QList<ExportTaskOption>, exportTaskRows.size()> rows;
        for (const auto& row : exportTaskRows) {
            auto& allowed = rows.at(static_cast<size_t>(row.trigger));
            for (const auto& option : options)
                if (row.allowed.testFlag(option.task) && supportsType(row.trigger, option.type))
                    allowed.append(option);
        }
        return rows;
    }();
    return byTrigger.at(static_cast<size_t>(trigger));
}

QString exportTaskScopedKey(ExportTrigger trigger) {
    if (!supportsType(trigger, Type::Normal))
        return {};
    return exportTaskRow(trigger).key + QStringLiteral("_scoped");
}

QStringList exportTaskScopedOptionIds(ExportTrigger trigger) {
    QStringList ids;
    for (const auto& option : exportTaskOptions(trigger))
        if (option.type != Type::All)
            ids.append(option.id.toString());
    return ids;
}

ExportTasks resolveExportTasks(ExportTrigger trigger, ScreenshotType type, const QStringList& ids) {
    if (!supportsType(trigger, type))
        return {};
    ExportTasks tasks;
    for (const auto& option : exportTaskOptions(trigger)) {
        if ((option.type == ScreenshotType::All || option.type == type) && ids.contains(option.id))
            tasks |= option.task;
    }
    return tasks;
}

QStringList exportTaskIds(ExportTasks tasks) {
    QStringList ids;
    for (const auto& option : options)
        if (option.type == Type::All && tasks.testFlag(option.task))
            ids.append(option.id.toString());
    return ids;
}
} // namespace snow_shot::storage
