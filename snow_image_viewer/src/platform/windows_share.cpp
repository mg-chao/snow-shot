#include "platform/windows_share.h"

#include <QDir>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <qt_windows.h>

#include <roapi.h>
#include <shobjidl_core.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#endif

namespace snow::image_viewer {

#if defined(Q_OS_WIN)
namespace {

namespace data_transfer = winrt::Windows::ApplicationModel::DataTransfer;
namespace foundation = winrt::Windows::Foundation;
namespace storage = winrt::Windows::Storage;
namespace streams = winrt::Windows::Storage::Streams;

using DataPackage = data_transfer::DataPackage;
using DataRequest = data_transfer::DataRequest;
using DataRequestDeferral = data_transfer::DataRequestDeferral;
using DataRequestedEventArgs = data_transfer::DataRequestedEventArgs;
using DataTransferManager = data_transfer::DataTransferManager;

constexpr wchar_t kShareDescription[] = L"Image shared from Snow Image Viewer";
constexpr wchar_t kShareDataError[] = L"Snow Image Viewer could not access the image to share it.";

class WindowsRuntimeApartment final {
  public:
    WindowsRuntimeApartment() : result_(RoInitialize(RO_INIT_SINGLETHREADED)) {}

    ~WindowsRuntimeApartment() {
        // S_FALSE is successful and, like S_OK, must be balanced with RoUninitialize.
        if (SUCCEEDED(result_)) {
            RoUninitialize();
        }
    }

    HRESULT result() const {
        return result_;
    }

    // RPC_E_CHANGED_MODE means the thread was already initialized in another
    // apartment. The Windows Runtime remains available, but this call is not balanced.
    bool isUsable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

  private:
    HRESULT result_ = E_UNEXPECTED;
};

struct ShareContent {
    std::uint64_t requestId = 0;
    std::wstring filePath;
    std::wstring title;
};

class ShareRequestQueue final {
  public:
    void push(ShareContent content) {
        const std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(std::move(content));
    }

    std::optional<ShareContent> takeNext() {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.empty()) {
            return std::nullopt;
        }

        ShareContent content = std::move(requests_.front());
        requests_.pop_front();
        return content;
    }

    void remove(std::uint64_t requestId) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto request = std::find_if(
            requests_.begin(), requests_.end(),
            [requestId](const ShareContent& content) { return content.requestId == requestId; });
        if (request != requests_.end()) {
            requests_.erase(request);
        }
    }

    void clear() {
        const std::lock_guard<std::mutex> lock(mutex_);
        requests_.clear();
    }

  private:
    std::mutex mutex_;
    std::deque<ShareContent> requests_;
};

void failRequest(const DataRequest& request) noexcept {
    if (!request) {
        return;
    }
    try {
        request.FailWithDisplayText(kShareDataError);
    } catch (...) {
        (void)winrt::to_hresult();
    }
}

void completeDeferral(DataRequestDeferral& deferral) noexcept {
    if (!deferral) {
        return;
    }

    DataRequestDeferral pending = std::exchange(deferral, nullptr);
    try {
        pending.Complete();
    } catch (...) {
        (void)winrt::to_hresult();
    }
}

class DeferredShareRequest final {
  public:
    DeferredShareRequest(DataRequest request, DataPackage data, DataRequestDeferral deferral,
                         std::wstring filePath)
        : request_(std::move(request)), data_(std::move(data)), deferral_(std::move(deferral)),
          filePath_(std::move(filePath)) {}

    ~DeferredShareRequest() {
        completeDeferral(deferral_);
    }

    void start(const std::shared_ptr<DeferredShareRequest>& self) {
        fileOperation_ = storage::StorageFile::GetFileFromPathAsync(filePath_);
        fileOperation_.Completed(
            [self](const foundation::IAsyncOperation<storage::StorageFile>& operation,
                   foundation::AsyncStatus status) noexcept {
                self->fileOperation_ = nullptr;
                if (status != foundation::AsyncStatus::Completed) {
                    failRequest(self->request_);
                    completeDeferral(self->deferral_);
                    return;
                }

                try {
                    const storage::StorageFile file = operation.GetResults();
                    const std::vector<storage::IStorageItem> items{file};
                    const streams::RandomAccessStreamReference imageStream =
                        streams::RandomAccessStreamReference::CreateFromFile(file);

                    // A single image should be offered as both a file and a bitmap so
                    // targets that support only one representation can still receive it.
                    self->data_.SetStorageItems(items, true);
                    self->data_.Properties().Thumbnail(imageStream);
                    self->data_.SetBitmap(imageStream);
                } catch (...) {
                    failRequest(self->request_);
                }
                completeDeferral(self->deferral_);
            });
    }

  private:
    DataRequest request_{nullptr};
    DataPackage data_{nullptr};
    DataRequestDeferral deferral_{nullptr};
    std::wstring filePath_;
    foundation::IAsyncOperation<storage::StorageFile> fileOperation_{nullptr};
};

void populateShareData(const std::shared_ptr<ShareRequestQueue>& requests,
                       const DataRequestedEventArgs& args) noexcept {
    DataRequest request{nullptr};
    DataRequestDeferral deferral{nullptr};
    std::shared_ptr<DeferredShareRequest> deferredRequest;
    try {
        request = args.Request();
        std::optional<ShareContent> content = requests->takeNext();
        if (!content) {
            failRequest(request);
            return;
        }

        const DataPackage data = request.Data();
        data.Properties().Title(content->title);
        data.Properties().Description(kShareDescription);
        data.RequestedOperation(data_transfer::DataPackageOperation::Copy);

        // StorageFile acquisition is asynchronous, so the request must stay open
        // until all data formats have been populated or the request has failed.
        deferral = request.GetDeferral();
        deferredRequest = std::make_shared<DeferredShareRequest>(request, data, deferral,
                                                                 std::move(content->filePath));
        deferral = nullptr;
        deferredRequest->start(deferredRequest);
    } catch (...) {
        failRequest(request);
        deferredRequest.reset();
        completeDeferral(deferral);
    }
}

QString hresultCode(HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

QString shareError(HRESULT result, const QString& detail) {
    const QString message = detail.trimmed();
    return message.isEmpty()
               ? QStringLiteral("Windows sharing failed (%1).").arg(hresultCode(result))
               : QStringLiteral("%1 (%2)").arg(message, hresultCode(result));
}

} // namespace
#endif

class WindowsShareController::Impl final {
  public:
#if defined(Q_OS_WIN)
    ~Impl() {
        releaseManager();
    }

    bool showShareUI(quintptr ownerWindowId, const QString& filePath, QString* errorMessage) {
        if (errorMessage) {
            errorMessage->clear();
        }
        if (!apartment_.isUsable()) {
            setError(errorMessage, apartment_.result(),
                     QStringLiteral("Windows Runtime initialization failed"));
            return false;
        }

        // Qt transports the native HWND through its integer-valued window ID type.
        const HWND owner =
            reinterpret_cast<HWND>(ownerWindowId); // NOLINT(performance-no-int-to-ptr)
        if (!owner || !IsWindow(owner)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The viewer window is not available.");
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

        const std::uint64_t requestId = nextRequestId_++;
        ShareContent content;
        content.requestId = requestId;
        content.filePath =
            QDir::toNativeSeparators(QDir::cleanPath(fileInfo.absoluteFilePath())).toStdWString();
        content.title = fileInfo.fileName().toStdWString();

        try {
            ensureManager(owner);
            requests_->push(std::move(content));
            winrt::check_hresult(interop_->ShowShareUIForWindow(owner));
            return true;
        } catch (const winrt::hresult_error& error) {
            requests_->remove(requestId);
            setError(errorMessage, error.code(), QString::fromWCharArray(error.message().c_str()));
        } catch (const std::exception& error) {
            requests_->remove(requestId);
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows sharing failed: %1")
                                    .arg(QString::fromLocal8Bit(error.what()));
            }
        } catch (...) {
            requests_->remove(requestId);
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows sharing failed unexpectedly.");
            }
        }
        return false;
    }

  private:
    void ensureManager(HWND owner) {
        if (owner_ == owner && manager_ && interop_) {
            return;
        }

        releaseManager();
        requests_->clear();

        auto interop =
            winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
        DataTransferManager manager{nullptr};
        winrt::check_hresult(interop->GetForWindow(
            owner, winrt::guid_of<data_transfer::IDataTransferManager>(), winrt::put_abi(manager)));
        if (!manager) {
            winrt::throw_hresult(E_UNEXPECTED);
        }

        const winrt::event_token token = manager.DataRequested(
            [requests = requests_](const DataTransferManager&,
                                   const DataRequestedEventArgs& args) noexcept {
                populateShareData(requests, args);
            });

        interop_ = std::move(interop);
        manager_ = std::move(manager);
        dataRequestedToken_ = token;
        dataRequestedSubscribed_ = true;
        owner_ = owner;
    }

    void releaseManager() noexcept {
        if (manager_ && dataRequestedSubscribed_) {
            manager_.DataRequested(dataRequestedToken_);
        }
        dataRequestedSubscribed_ = false;
        dataRequestedToken_ = {};
        manager_ = nullptr;
        interop_ = nullptr;
        owner_ = nullptr;
    }

    static void setError(QString* errorMessage, HRESULT result, const QString& detail) {
        if (errorMessage) {
            *errorMessage = shareError(result, detail);
        }
    }

    WindowsRuntimeApartment apartment_;
    HWND owner_ = nullptr;
    winrt::com_ptr<IDataTransferManagerInterop> interop_;
    DataTransferManager manager_{nullptr};
    winrt::event_token dataRequestedToken_{};
    bool dataRequestedSubscribed_ = false;
    std::shared_ptr<ShareRequestQueue> requests_ = std::make_shared<ShareRequestQueue>();
    std::uint64_t nextRequestId_ = 1;
#else
    bool showShareUI(quintptr, const QString&, QString* errorMessage) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Windows sharing is not available on this platform.");
        }
        return false;
    }
#endif
};

WindowsShareController::WindowsShareController() : impl_(std::make_unique<Impl>()) {}

WindowsShareController::~WindowsShareController() = default;

bool WindowsShareController::showShareUI(quintptr ownerWindowId, const QString& filePath,
                                         QString* errorMessage) {
    return impl_->showShareUI(ownerWindowId, filePath, errorMessage);
}

} // namespace snow::image_viewer
