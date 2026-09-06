#pragma once

#include <QString>
#include <QtGlobal>

namespace snow::image_viewer {

// Returns true when Windows reports that it successfully handled the dialog
// request. SHOpenWithDialog does not return the application the user selected.
bool showWindowsOpenWithDialog(quintptr ownerWindowId, const QString& filePath,
                               QString* errorMessage);

} // namespace snow::image_viewer
