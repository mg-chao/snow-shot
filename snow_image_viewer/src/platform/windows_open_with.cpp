#include "platform/windows_open_with.h"

#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include "platform/windows_open_with_internal.h"

#include <array>
#endif

namespace snow::image_viewer {

#if defined(Q_OS_WIN)
namespace {

QString hresultCode(HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

QString hresultMessage(HRESULT result) {
    std::array<wchar_t, 512> buffer{};
    DWORD messageId = static_cast<DWORD>(result);
    if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
        messageId = HRESULT_CODE(result);
    }

    const DWORD length =
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                       messageId, 0, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    return length == 0 ? QString() : QString::fromWCharArray(buffer.data(), length).trimmed();
}

void setNativeError(QString* errorMessage, HRESULT result) {
    if (!errorMessage) {
        return;
    }

    const QString detail = hresultMessage(result);
    *errorMessage = detail.isEmpty()
                        ? QStringLiteral("Windows could not open the app chooser (%1).")
                              .arg(hresultCode(result))
                        : QStringLiteral("%1 (%2)").arg(detail, hresultCode(result));
}

} // namespace

bool detail::showWindowsOpenWithDialogUsing(quintptr ownerWindowId, const QString& filePath,
                                            QString* errorMessage,
                                            OpenWithDialogInvoker invokeDialog) {
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!invokeDialog) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The Windows app chooser is not available.");
        }
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The image file is no longer available.");
        }
        return false;
    }

    const QString nativeFilePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    OPENASINFO openAsInfo{};
    openAsInfo.pcszFile = reinterpret_cast<LPCWSTR>(nativeFilePath.utf16());
    openAsInfo.pcszClass = nullptr;
    // Windows 10+ ignores registration flags. OAIF_EXEC makes the selected app
    // open this file instead of redirecting the user to Default Apps settings.
    openAsInfo.oaifInFlags = OAIF_EXEC;

    // Qt transports the native HWND through its integer-valued window ID type.
    const HWND owner = reinterpret_cast<HWND>(ownerWindowId); // NOLINT(performance-no-int-to-ptr)
    const HRESULT result = invokeDialog(owner, &openAsInfo);
    if (FAILED(result)) {
        setNativeError(errorMessage, result);
        return false;
    }
    return true;
}

bool showWindowsOpenWithDialog(quintptr ownerWindowId, const QString& filePath,
                               QString* errorMessage) {
    return detail::showWindowsOpenWithDialogUsing(ownerWindowId, filePath, errorMessage,
                                                  &SHOpenWithDialog);
}
#else
bool showWindowsOpenWithDialog(quintptr, const QString&, QString* errorMessage) {
    if (errorMessage) {
        *errorMessage =
            QStringLiteral("The Windows app chooser is not available on this platform.");
    }
    return false;
}
#endif

} // namespace snow::image_viewer
