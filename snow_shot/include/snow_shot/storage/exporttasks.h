// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFlags>
#include <QStringList>
#include <array>

namespace snow_shot::storage {
enum class ExportTrigger {
    ScreenshotCopy,
    ScreenshotSave,
    ScreenshotPin,
    ScreenshotPrint,
    ScreenshotAirDrop,
    ScreenshotUpload,
    PinnedCopy,
    PinnedSave,
    PinnedPrint,
    PinnedAirDrop,
    PinnedUpload
};
enum class ExportTask { CopyImage = 1, CopyFile = 2, AutoSave = 4, Pin = 8, Upload = 16 };
Q_DECLARE_FLAGS(ExportTasks, ExportTask)
Q_DECLARE_OPERATORS_FOR_FLAGS(ExportTasks)

enum class ScreenshotType { All, Normal, Scrolling, FocusedWindow, CurrentMonitor };

struct ExportTaskOption {
    ExportTask task;
    ScreenshotType type;
    QStringView id;
    const char* label;
};

struct ExportTaskRow {
    ExportTrigger trigger;
    QStringView key;
    ExportTasks allowed;
};
inline constexpr std::array exportTaskRows{
    ExportTaskRow{ExportTrigger::ScreenshotCopy, u"export_tasks/screenshot_copy",
                  ExportTask::AutoSave | ExportTask::Pin | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::ScreenshotSave, u"export_tasks/screenshot_save",
                  ExportTask::CopyFile | ExportTask::Pin | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::ScreenshotPin, u"export_tasks/screenshot_pin",
                  ExportTask::CopyImage | ExportTask::AutoSave | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::ScreenshotPrint, u"export_tasks/screenshot_print",
                  ExportTask::CopyImage | ExportTask::Pin | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::ScreenshotAirDrop, u"export_tasks/screenshot_airdrop",
                  ExportTask::CopyImage | ExportTask::AutoSave | ExportTask::Pin |
                      ExportTask::Upload},
    ExportTaskRow{ExportTrigger::ScreenshotUpload, u"export_tasks/screenshot_upload",
                  ExportTask::AutoSave | ExportTask::Pin},
    ExportTaskRow{ExportTrigger::PinnedCopy, u"export_tasks/pinned_copy",
                  ExportTask::AutoSave | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::PinnedSave, u"export_tasks/pinned_save",
                  ExportTask::CopyFile | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::PinnedPrint, u"export_tasks/pinned_print",
                  ExportTask::CopyImage | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::PinnedAirDrop, u"export_tasks/pinned_airdrop",
                  ExportTask::CopyImage | ExportTask::AutoSave | ExportTask::Upload},
    ExportTaskRow{ExportTrigger::PinnedUpload, u"export_tasks/pinned_upload", ExportTask::AutoSave},
};

inline const ExportTaskRow& exportTaskRow(ExportTrigger trigger) {
    return exportTaskRows.at(static_cast<size_t>(trigger));
}

const QList<ExportTaskOption>& exportTaskOptions(ExportTrigger trigger);
QString exportTaskScopedKey(ExportTrigger trigger);
QStringList exportTaskScopedOptionIds(ExportTrigger trigger);
ExportTasks resolveExportTasks(ExportTrigger trigger, ScreenshotType type, const QStringList& ids);
QStringList exportTaskIds(ExportTasks tasks);
} // namespace snow_shot::storage
