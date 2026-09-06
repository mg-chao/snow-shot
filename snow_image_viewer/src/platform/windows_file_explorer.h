#pragma once

#include <QFuture>
#include <QString>

namespace snow::image_viewer {

struct WindowsFileExplorerResult {
    bool opened = false;
    QString errorMessage;
};

// Resolves the file through the Windows Shell and opens its parent folder with
// the file selected. The Shell work runs off the UI thread.
QFuture<WindowsFileExplorerResult> openInWindowsFileExplorer(QString filePath);

} // namespace snow::image_viewer
