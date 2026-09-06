#include "platform/windows_file_explorer.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

#if defined(Q_OS_WIN)
#include <qt_windows.h>

#include <shlobj.h>

#include <array>
#include <memory>
#include <string>
#endif

#include <utility>

namespace snow::image_viewer {
namespace {

#if defined(Q_OS_WIN)
class ComApartment final {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}

    ~ComApartment() {
        // S_FALSE is a successful initialization and must also be balanced.
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    HRESULT result() const {
        return result_;
    }

    // RPC_E_CHANGED_MODE means this thread was already initialized in another
    // apartment model. COM is available, but this call must not be balanced.
    bool isUsable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

  private:
    HRESULT result_;
};

struct ItemIdListDeleter {
    using pointer = PIDLIST_ABSOLUTE;

    void operator()(pointer itemIdList) const noexcept {
        CoTaskMemFree(itemIdList);
    }
};

using ItemIdListPtr = std::unique_ptr<ITEMIDLIST_ABSOLUTE, ItemIdListDeleter>;

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

QString nativeError(HRESULT result, const QString& context) {
    const QString detail = hresultMessage(result);
    return detail.isEmpty()
               ? QStringLiteral("%1 (%2)").arg(context, hresultCode(result))
               : QStringLiteral("%1: %2 (%3)").arg(context, detail, hresultCode(result));
}

WindowsFileExplorerResult openInWorker(const QString& filePath) {
    WindowsFileExplorerResult result;
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) {
        result.errorMessage = QStringLiteral("The image file is no longer available.");
        return result;
    }

    const ComApartment apartment;
    if (!apartment.isUsable()) {
        result.errorMessage =
            nativeError(apartment.result(), QStringLiteral("Windows could not initialize COM"));
        return result;
    }

    const QString nativeFilePath =
        QDir::toNativeSeparators(QDir::cleanPath(fileInfo.absoluteFilePath()));
    const std::wstring shellPath = nativeFilePath.toStdWString();
    PIDLIST_ABSOLUTE rawItemIdList = nullptr;
    const HRESULT parseResult =
        SHParseDisplayName(shellPath.c_str(), nullptr, &rawItemIdList, 0, nullptr);
    ItemIdListPtr itemIdList(rawItemIdList);
    if (FAILED(parseResult) || !itemIdList) {
        const HRESULT errorResult = FAILED(parseResult) ? parseResult : E_UNEXPECTED;
        result.errorMessage =
            nativeError(errorResult, QStringLiteral("Windows could not resolve the image path"));
        return result;
    }

    // With cidl == 0, the documented contract treats this fully qualified PIDL
    // as the item to select and opens its parent folder.
    const HRESULT openResult = SHOpenFolderAndSelectItems(itemIdList.get(), 0, nullptr, 0);
    if (FAILED(openResult)) {
        result.errorMessage =
            nativeError(openResult, QStringLiteral("Windows could not open File Explorer"));
        return result;
    }

    result.opened = true;
    return result;
}
#else
WindowsFileExplorerResult openInWorker(const QString&) {
    WindowsFileExplorerResult result;
    result.errorMessage = QStringLiteral("File Explorer is only available on Windows.");
    return result;
}
#endif

} // namespace

QFuture<WindowsFileExplorerResult> openInWindowsFileExplorer(QString filePath) {
    return QtConcurrent::run([filePath = std::move(filePath)]() { return openInWorker(filePath); });
}

} // namespace snow::image_viewer
