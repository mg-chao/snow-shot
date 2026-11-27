// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/screenshotcontroller.h"
#include "snow_shot/presentation/screenshotinteractionstate.h"
#include "snow_shot/presentation/screenshottoolbarcommands.h"
#include <QStringView>
#include <optional>

namespace snow_shot::presentation {
struct ScreenshotCaptureCommandState {
    bool supported = false;
    bool enabled = false;
    bool checked = false;
    bool busy = false;
};

struct ScreenshotCaptureCommand {
    ScreenshotController::CaptureAction pending;
    QLatin1StringView shortcut;
    QLatin1StringView item;
    ScreenshotToolbarCommandSink::Action execute;
    std::optional<ScreenshotActiveTool> tool;
};

inline constexpr ScreenshotCaptureCommand screenshotCaptureCommands[] = {
    {ScreenshotController::CaptureAction::Pin,
     QLatin1StringView("pin_to_screen"),
     QLatin1StringView("pin-to-screen"),
     &ScreenshotToolbarCommandSink::pinSelectionToScreen,
     {}},
    {ScreenshotController::CaptureAction::PinWindow,
     QLatin1StringView("pin_window_to_screen"),
     QLatin1StringView("pin-window-to-screen"),
     &ScreenshotToolbarCommandSink::pinWindowSelectionToScreen,
     {}},
    {ScreenshotController::CaptureAction::RecognizeText, QLatin1StringView("text_recognition"),
     QLatin1StringView("text-recognition"), &ScreenshotToolbarCommandSink::setOcrTool,
     ScreenshotActiveTool::Ocr},
    {ScreenshotController::CaptureAction::RecognizeTextAndCopy,
     QLatin1StringView("text_recognition_copy"), QLatin1StringView("text-recognition"),
     &ScreenshotToolbarCommandSink::recognizeTextAndCopy, ScreenshotActiveTool::Ocr},
    {ScreenshotController::CaptureAction::RecognizeTextTranslation,
     QLatin1StringView("text_translation"), QLatin1StringView("text-translation"),
     &ScreenshotToolbarCommandSink::setTextTranslationTool, ScreenshotActiveTool::TextTranslation},
    {ScreenshotController::CaptureAction::RecognizeTable, QLatin1StringView("table_recognition"),
     QLatin1StringView("table-recognition"), &ScreenshotToolbarCommandSink::setTableTool,
     ScreenshotActiveTool::Table},
    {ScreenshotController::CaptureAction::RecognizeQr, QLatin1StringView("qr_code_recognition"),
     QLatin1StringView("barcode-recognition"), &ScreenshotToolbarCommandSink::setQrTool,
     ScreenshotActiveTool::Qr},
    {ScreenshotController::CaptureAction::None,
     {},
     QLatin1StringView("document-recognition"),
     &ScreenshotToolbarCommandSink::setDocumentTool,
     ScreenshotActiveTool::Document},
    {ScreenshotController::CaptureAction::RecognizeFormula,
     {},
     QLatin1StringView("latex-recognition"),
     &ScreenshotToolbarCommandSink::setLatexTool,
     ScreenshotActiveTool::Latex},
    {ScreenshotController::CaptureAction::ConvertMarkdown,
     {},
     QLatin1StringView("convert-to-markdown"),
     &ScreenshotToolbarCommandSink::setMarkdownTool,
     ScreenshotActiveTool::Markdown},
    {ScreenshotController::CaptureAction::ConvertHtml,
     {},
     QLatin1StringView("convert-to-html"),
     &ScreenshotToolbarCommandSink::setHtmlTool,
     ScreenshotActiveTool::Html},
    {ScreenshotController::CaptureAction::StartVideo,
     QLatin1StringView("video_recording"),
     QLatin1StringView("record-screen"),
     &ScreenshotToolbarCommandSink::startScreenRecording,
     {}},
    {ScreenshotController::CaptureAction::StartScrolling,
     QLatin1StringView("scrolling_screenshot"),
     QLatin1StringView("scrolling-screenshot"),
     &ScreenshotToolbarCommandSink::startScrollingScreenshot,
     {}},
    {ScreenshotController::CaptureAction::Copy,
     QLatin1StringView("copy_to_clipboard"),
     QLatin1StringView("copy"),
     &ScreenshotToolbarCommandSink::copySelectionToClipboard,
     {}},
    {ScreenshotController::CaptureAction::Save,
     QLatin1StringView("save_as_file"),
     QLatin1StringView("save-as-file"),
     &ScreenshotToolbarCommandSink::saveSelectionToFile,
     {}},
    {ScreenshotController::CaptureAction::QuickSave,
     QLatin1StringView("quick_save"),
     QLatin1StringView("quick-save"),
     &ScreenshotToolbarCommandSink::quickSaveSelection,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("print"),
     QLatin1StringView("print"),
     &ScreenshotToolbarCommandSink::printSelection,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("upload_to_cloud"),
     QLatin1StringView("upload-to-cloud"),
     &ScreenshotToolbarCommandSink::uploadSelectionToCloud,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("cancel_screenshot"),
     QLatin1StringView("cancel"),
     &ScreenshotToolbarCommandSink::cancelCapture,
     {}},
    {ScreenshotController::CaptureAction::None,
     {},
     QLatin1StringView("airdrop"),
     &ScreenshotToolbarCommandSink::airDropSelection,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("undo"),
     QLatin1StringView("undo"),
     &ScreenshotToolbarCommandSink::undoCanvasEdit,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("redo"),
     QLatin1StringView("redo"),
     &ScreenshotToolbarCommandSink::redoCanvasEdit,
     {}},
    {ScreenshotController::CaptureAction::None,
     QLatin1StringView("recapture"),
     QLatin1StringView("recapture"),
     &ScreenshotToolbarCommandSink::requestRecapture,
     {}},
    {ScreenshotController::CaptureAction::None, QLatin1StringView("move_tool"),
     QLatin1StringView("move"), &ScreenshotToolbarCommandSink::setMoveTool,
     ScreenshotActiveTool::Move},
};

inline const ScreenshotCaptureCommand*
captureCommandForAction(ScreenshotToolbarCommandSink::Action action) {
    for (const auto& command : screenshotCaptureCommands)
        if (command.execute == action)
            return &command;
    return nullptr;
}

inline const ScreenshotCaptureCommand* captureCommandForShortcut(QStringView id) {
    if (!id.isEmpty())
        for (const auto& command : screenshotCaptureCommands)
            if (command.shortcut == id)
                return &command;
    return nullptr;
}

inline const ScreenshotCaptureCommand*
captureCommandForPendingAction(ScreenshotController::CaptureAction action) {
    if (action != ScreenshotController::CaptureAction::None)
        for (const auto& command : screenshotCaptureCommands)
            if (command.pending == action)
                return &command;
    return nullptr;
}
} // namespace snow_shot::presentation
