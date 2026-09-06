#pragma once

#include "platform/windows_open_with.h"

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shlobj.h>

namespace snow::image_viewer::detail {

using OpenWithDialogInvoker = HRESULT(WINAPI*)(HWND, const OPENASINFO*);

bool showWindowsOpenWithDialogUsing(quintptr ownerWindowId, const QString& filePath,
                                    QString* errorMessage, OpenWithDialogInvoker invokeDialog);

} // namespace snow::image_viewer::detail
#endif
