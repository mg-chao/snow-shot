#include "platform/windows_print.h"

#include <QColorSpace>
#include <QPromise>

#if defined(Q_OS_WIN)
#include <qt_windows.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d3d11.h>
#include <DocumentSource.h>
#include <PrintManagerInterop.h>
#include <printpreview.h>
#include <roapi.h>
#include <wincodec.h>
#include <windows.graphics.printing.h>
#include <wrl.h>
#include <wrl/implements.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Printing.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#endif

#include <memory>
#include <utility>

namespace snow::image_viewer {
namespace {

using PrintResult = WindowsPrintController::Result;

QFuture<PrintResult> readyPrintResult(PrintResult result) {
    QPromise<PrintResult> promise;
    promise.start();
    QFuture<PrintResult> future = promise.future();
    promise.addResult(std::move(result));
    promise.finish();
    return future;
}

PrintResult failedResult(const QString& message) {
    return {PrintResult::Completion::Failed, message};
}

#if defined(Q_OS_WIN)

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::WinRtClassicComMix;

using PrintDocumentSource = ABI::Windows::Graphics::Printing::IPrintDocumentSource;
using PrintManager = winrt::Windows::Graphics::Printing::PrintManager;
using PrintTaskCompletion = winrt::Windows::Graphics::Printing::PrintTaskCompletion;
using PrintTaskOptions = winrt::Windows::Graphics::Printing::PrintTaskOptions;

constexpr float kDefaultDpi = 96.0F;
constexpr UINT32 kFirstPage = 1;

QString hresultText(HRESULT result, const QString& context) {
    const QString code =
        QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    return context.isEmpty() ? QStringLiteral("Windows printing failed (%1).").arg(code)
                             : QStringLiteral("%1 (%2)").arg(context, code);
}

PrintResult hresultFailure(HRESULT result, const QString& context) {
    return failedResult(hresultText(result, context));
}

PrintResult winrtFailure(const winrt::hresult_error& error, const QString& context) {
    const QString detail = QString::fromWCharArray(error.message().c_str());
    const QString message = detail.isEmpty() || detail == context
                                ? context
                                : QStringLiteral("%1: %2").arg(context, detail);
    return hresultFailure(error.code(), message);
}

struct PageLayout {
    D2D1_SIZE_F pageSize{};
    D2D1_RECT_F imageableRect{};
    float dpiX = 0.0F;
    float dpiY = 0.0F;
};

bool isPositiveFinite(float value) {
    return std::isfinite(value) && value > 0.0F;
}

HRESULT getPageLayout(const PrintTaskOptions& options, UINT32 jobPage,
                      PageLayout* layout) noexcept {
    if (!options || !layout || jobPage < kFirstPage) {
        return E_INVALIDARG;
    }

    try {
        const auto description = options.GetPageDescription(jobPage);
        const double pageWidth = description.PageSize.Width;
        const double pageHeight = description.PageSize.Height;
        const double imageableX = description.ImageableRect.X;
        const double imageableY = description.ImageableRect.Y;
        const double imageableWidth = description.ImageableRect.Width;
        const double imageableHeight = description.ImageableRect.Height;
        const double imageableRight = imageableX + imageableWidth;
        const double imageableBottom = imageableY + imageableHeight;

        if (!std::isfinite(pageWidth) || !std::isfinite(pageHeight) || pageWidth <= 0.0 ||
            pageHeight <= 0.0 || !std::isfinite(imageableX) || !std::isfinite(imageableY) ||
            !std::isfinite(imageableWidth) || !std::isfinite(imageableHeight) ||
            imageableWidth <= 0.0 || imageableHeight <= 0.0 || !std::isfinite(imageableRight) ||
            !std::isfinite(imageableBottom) || description.DpiX == 0 || description.DpiY == 0) {
            return E_INVALIDARG;
        }

        const double left = std::clamp(imageableX, 0.0, pageWidth);
        const double top = std::clamp(imageableY, 0.0, pageHeight);
        const double right = std::clamp(imageableRight, 0.0, pageWidth);
        const double bottom = std::clamp(imageableBottom, 0.0, pageHeight);
        if (right <= left || bottom <= top) {
            return E_INVALIDARG;
        }

        layout->pageSize = {static_cast<float>(pageWidth), static_cast<float>(pageHeight)};
        layout->imageableRect = {static_cast<float>(left), static_cast<float>(top),
                                 static_cast<float>(right), static_cast<float>(bottom)};
        layout->dpiX = static_cast<float>(description.DpiX);
        layout->dpiY = static_cast<float>(description.DpiY);
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
}

class ImagePrintDocument final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, PrintDocumentSource,
                          IPrintPreviewPageCollection, IPrintDocumentPageSource, FtmBase> {
  public:
    HRESULT RuntimeClassInitialize(const QImage& image, float displayDpi) noexcept {
        displayDpi_ = isPositiveFinite(displayDpi) ? displayDpi : kDefaultDpi;
        image_ = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),
                                             QImage::Format_ARGB32_Premultiplied, Qt::AutoColor);
        if (image_.isNull()) {
            image_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        if (image_.isNull()) {
            return E_INVALIDARG;
        }
        return initializeGraphics();
    }

    HRESULT STDMETHODCALLTYPE
    GetPreviewPageCollection(IPrintDocumentPackageTarget* documentTarget,
                             IPrintPreviewPageCollection** pageCollection) noexcept override {
        if (!documentTarget || !pageCollection) {
            return E_INVALIDARG;
        }
        *pageCollection = nullptr;

        ComPtr<IPrintPreviewDxgiPackageTarget> previewTarget;
        const HRESULT result = documentTarget->GetPackageTarget(
            ID_PREVIEWPACKAGETARGET_DXGI, IID_PPV_ARGS(previewTarget.GetAddressOf()));
        if (FAILED(result)) {
            return result;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            previewTarget_ = std::move(previewTarget);
            previewOptions_ = nullptr;
        }

        *pageCollection = static_cast<IPrintPreviewPageCollection*>(this);
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Paginate(UINT32 currentJobPage,
                                       IInspectable* printTaskOptions) noexcept override {
        (void)currentJobPage;
        if (!printTaskOptions) {
            return E_INVALIDARG;
        }

        try {
            winrt::com_ptr<IInspectable> inspectable;
            inspectable.copy_from(printTaskOptions);
            const PrintTaskOptions options = inspectable.as<PrintTaskOptions>();
            ComPtr<IPrintPreviewDxgiPackageTarget> previewTarget;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                previewOptions_ = options;
                previewTarget = previewTarget_;
            }
            if (!previewTarget) {
                return E_UNEXPECTED;
            }

            return previewTarget->SetJobPageCount(FinalPageCount, 1);
        } catch (...) {
            return winrt::to_hresult();
        }
    }

    HRESULT STDMETHODCALLTYPE MakePage(UINT32 desiredJobPage, FLOAT width,
                                       FLOAT height) noexcept override {
        if (!isPositiveFinite(width) || !isPositiveFinite(height)) {
            return E_INVALIDARG;
        }

        const UINT32 jobPage =
            desiredJobPage == JOB_PAGE_APPLICATION_DEFINED ? kFirstPage : desiredJobPage;
        if (jobPage != kFirstPage) {
            return E_INVALIDARG;
        }

        PrintTaskOptions options{nullptr};
        ComPtr<IPrintPreviewDxgiPackageTarget> previewTarget;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            options = previewOptions_;
            previewTarget = previewTarget_;
        }
        if (!options || !previewTarget) {
            return E_UNEXPECTED;
        }

        PageLayout layout;
        const HRESULT layoutResult = getPageLayout(options, jobPage, &layout);
        if (FAILED(layoutResult)) {
            return layoutResult;
        }
        return makePreviewPage(previewTarget.Get(), jobPage, width, height, layout);
    }

    HRESULT STDMETHODCALLTYPE
    MakeDocument(IInspectable* printTaskOptions,
                 IPrintDocumentPackageTarget* documentTarget) noexcept override {
        if (!printTaskOptions || !documentTarget) {
            return E_INVALIDARG;
        }

        try {
            winrt::com_ptr<IInspectable> inspectable;
            inspectable.copy_from(printTaskOptions);
            const PrintTaskOptions options = inspectable.as<PrintTaskOptions>();
            PageLayout layout;
            const HRESULT layoutResult = getPageLayout(options, kFirstPage, &layout);
            if (FAILED(layoutResult)) {
                return layoutResult;
            }
            return makeDocument(documentTarget, layout);
        } catch (...) {
            return winrt::to_hresult();
        }
    }

  private:
    HRESULT initializeGraphics() noexcept {
        const auto width = static_cast<quint64>(image_.width());
        const auto height = static_cast<quint64>(image_.height());
        const auto stride = static_cast<quint64>(image_.bytesPerLine());
        const auto byteCount = static_cast<quint64>(image_.sizeInBytes());
        constexpr quint64 kMaxUint = std::numeric_limits<UINT>::max();
        constexpr quint64 kBytesPerPixel = 4;
        const quint64 minimumStride = width * kBytesPerPixel;
        if (width == 0 || height == 0 || stride == 0 || byteCount == 0 || width > kMaxUint ||
            height > kMaxUint || stride > kMaxUint || byteCount > kMaxUint ||
            minimumStride > stride || stride * height > byteCount) {
            return E_INVALIDARG;
        }

        constexpr UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL featureLevel{};
        HRESULT result =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, nullptr, 0,
                              D3D11_SDK_VERSION, d3dDevice_.GetAddressOf(), &featureLevel, nullptr);
        if (FAILED(result)) {
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags, nullptr,
                                       0, D3D11_SDK_VERSION, d3dDevice_.GetAddressOf(),
                                       &featureLevel, nullptr);
        }
        if (FAILED(result)) {
            return result;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        result = d3dDevice_.As(&dxgiDevice);
        if (FAILED(result)) {
            return result;
        }

        D2D1_FACTORY_OPTIONS factoryOptions{};
        result = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, factoryOptions,
                                   d2dFactory_.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        result = d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        result = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(result)) {
            return result;
        }

        return wicFactory_->CreateBitmapFromMemory(
            static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppPBGRA,
            static_cast<UINT>(stride), static_cast<UINT>(byteCount), image_.bits(),
            wicBitmap_.GetAddressOf());
    }

    HRESULT drawImage(ID2D1DeviceContext* deviceContext, const PageLayout& layout) noexcept {
        if (!deviceContext || !wicBitmap_) {
            return E_UNEXPECTED;
        }

        ComPtr<ID2D1Bitmap1> bitmap;
        HRESULT result = deviceContext->CreateBitmapFromWicBitmap(wicBitmap_.Get(), nullptr,
                                                                  bitmap.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        const D2D1_SIZE_F bitmapSize = bitmap->GetSize();
        const float printableWidth = layout.imageableRect.right - layout.imageableRect.left;
        const float printableHeight = layout.imageableRect.bottom - layout.imageableRect.top;
        if (!isPositiveFinite(bitmapSize.width) || !isPositiveFinite(bitmapSize.height) ||
            !isPositiveFinite(printableWidth) || !isPositiveFinite(printableHeight)) {
            return E_INVALIDARG;
        }

        const float scale =
            std::min(printableWidth / bitmapSize.width, printableHeight / bitmapSize.height);
        const float drawnWidth = bitmapSize.width * scale;
        const float drawnHeight = bitmapSize.height * scale;
        const float originX = layout.imageableRect.left + (printableWidth - drawnWidth) / 2.0F;
        const float originY = layout.imageableRect.top + (printableHeight - drawnHeight) / 2.0F;
        const D2D1_RECT_F destination = {originX, originY, originX + drawnWidth,
                                         originY + drawnHeight};
        deviceContext->DrawBitmap(bitmap.Get(), &destination, 1.0F,
                                  D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        return S_OK;
    }

    HRESULT createCommandList(const PageLayout& layout,
                              ComPtr<ID2D1CommandList>* commandList) noexcept {
        if (!commandList || !d2dDevice_) {
            return E_INVALIDARG;
        }

        ComPtr<ID2D1DeviceContext> deviceContext;
        HRESULT result = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                         deviceContext.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }
        deviceContext->SetDpi(layout.dpiX, layout.dpiY);

        result = deviceContext->CreateCommandList(commandList->GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        deviceContext->SetTarget(commandList->Get());
        deviceContext->BeginDraw();
        deviceContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
        const HRESULT imageResult = drawImage(deviceContext.Get(), layout);
        const HRESULT endDrawResult = deviceContext->EndDraw();
        deviceContext->SetTarget(nullptr);
        if (FAILED(imageResult)) {
            return imageResult;
        }
        if (FAILED(endDrawResult)) {
            return endDrawResult;
        }
        return (*commandList)->Close();
    }

    HRESULT makePreviewPage(IPrintPreviewDxgiPackageTarget* previewTarget, UINT32 jobPage,
                            float width, float height, const PageLayout& layout) noexcept {
        if (!previewTarget || !d3dDevice_ || !d2dDevice_) {
            return E_UNEXPECTED;
        }

        const double previewPixelsWide =
            std::floor(static_cast<double>(width) * displayDpi_ / kDefaultDpi + 0.5);
        const double previewPixelsHigh =
            std::floor(static_cast<double>(height) * displayDpi_ / kDefaultDpi + 0.5);
        if (previewPixelsWide < 1.0 || previewPixelsHigh < 1.0 ||
            previewPixelsWide > std::numeric_limits<UINT>::max() ||
            previewPixelsHigh > std::numeric_limits<UINT>::max()) {
            return E_INVALIDARG;
        }

        const double dpiX = previewPixelsWide / layout.pageSize.width * kDefaultDpi;
        const double dpiY = previewPixelsHigh / layout.pageSize.height * kDefaultDpi;
        const double previewDpiValue = std::min(dpiX, dpiY);
        if (!std::isfinite(previewDpiValue) || previewDpiValue <= 0.0 ||
            previewDpiValue > std::numeric_limits<float>::max()) {
            return E_INVALIDARG;
        }
        const float previewDpi = static_cast<float>(previewDpiValue);

        const double surfaceWidth =
            std::floor(layout.pageSize.width * previewDpi / kDefaultDpi + 0.5);
        const double surfaceHeight =
            std::floor(layout.pageSize.height * previewDpi / kDefaultDpi + 0.5);
        if (surfaceWidth < 1.0 || surfaceHeight < 1.0 ||
            surfaceWidth > std::numeric_limits<UINT>::max() ||
            surfaceHeight > std::numeric_limits<UINT>::max()) {
            return E_INVALIDARG;
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = static_cast<UINT>(surfaceWidth);
        textureDescription.Height = static_cast<UINT>(surfaceHeight);
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ComPtr<ID3D11Texture2D> texture;
        HRESULT result =
            d3dDevice_->CreateTexture2D(&textureDescription, nullptr, texture.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        ComPtr<IDXGISurface> surface;
        result = texture.As(&surface);
        if (FAILED(result)) {
            return result;
        }

        ComPtr<ID2D1DeviceContext> deviceContext;
        result = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                 deviceContext.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }
        deviceContext->SetDpi(previewDpi, previewDpi);

        const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            previewDpi, previewDpi);
        ComPtr<ID2D1Bitmap1> surfaceBitmap;
        result = deviceContext->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProperties,
                                                            surfaceBitmap.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        deviceContext->SetTarget(surfaceBitmap.Get());
        deviceContext->BeginDraw();
        deviceContext->Clear(D2D1::ColorF(D2D1::ColorF::White));
        const HRESULT imageResult = drawImage(deviceContext.Get(), layout);
        const HRESULT endDrawResult = deviceContext->EndDraw();
        deviceContext->SetTarget(nullptr);
        if (FAILED(imageResult)) {
            return imageResult;
        }
        if (FAILED(endDrawResult)) {
            return endDrawResult;
        }

        return previewTarget->DrawPage(jobPage, surface.Get(), previewDpi, previewDpi);
    }

    HRESULT makeDocument(IPrintDocumentPackageTarget* documentTarget,
                         const PageLayout& layout) noexcept {
        const D2D1_PRINT_CONTROL_PROPERTIES properties = D2D1::PrintControlProperties(
            D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT, layout.dpiX, D2D1_COLOR_SPACE_SRGB);
        ComPtr<ID2D1PrintControl> printControl;
        HRESULT result = d2dDevice_->CreatePrintControl(wicFactory_.Get(), documentTarget,
                                                        &properties, printControl.GetAddressOf());
        if (FAILED(result)) {
            return result;
        }

        ComPtr<ID2D1CommandList> commandList;
        result = createCommandList(layout, &commandList);
        if (FAILED(result)) {
            return result;
        }

        result =
            printControl->AddPage(commandList.Get(), layout.pageSize, nullptr, nullptr, nullptr);
        if (FAILED(result)) {
            return result;
        }
        return printControl->Close();
    }

    QImage image_;
    float displayDpi_ = kDefaultDpi;
    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<IWICImagingFactory2> wicFactory_;
    ComPtr<IWICBitmap> wicBitmap_;
    std::mutex stateMutex_;
    ComPtr<IPrintPreviewDxgiPackageTarget> previewTarget_;
    PrintTaskOptions previewOptions_{nullptr};
};

class PrintSession final {
  public:
    PrintSession(QString documentTitle,
                 winrt::Windows::Graphics::Printing::IPrintDocumentSource documentSource)
        : title(std::move(documentTitle)), source(std::move(documentSource)),
          promise_(std::make_shared<QPromise<PrintResult>>()) {
        promise_->start();
        future_ = promise_->future();
    }

    QFuture<PrintResult> future() const {
        return future_;
    }

    void finish(PrintResult result) {
        std::shared_ptr<QPromise<PrintResult>> promise;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (finished_) {
                return;
            }
            finished_ = true;
            promise = promise_;
        }
        promise->addResult(std::move(result));
        promise->finish();
    }

    const QString title;
    const winrt::Windows::Graphics::Printing::IPrintDocumentSource source;

  private:
    std::shared_ptr<QPromise<PrintResult>> promise_;
    QFuture<PrintResult> future_;
    std::mutex mutex_;
    bool finished_ = false;
};

class PrintControllerState final {
  public:
    bool begin(const std::shared_ptr<PrintSession>& session) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeSession_) {
            return false;
        }
        activeSession_ = session;
        return true;
    }

    std::shared_ptr<PrintSession> activeSession() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeSession_;
    }

    void finish(const std::shared_ptr<PrintSession>& session, PrintResult result) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeSession_ == session) {
                activeSession_.reset();
            }
        }
        session->finish(std::move(result));
    }

    void cancelActive(const QString& message) {
        std::shared_ptr<PrintSession> session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = std::move(activeSession_);
        }
        if (session) {
            session->finish({PrintResult::Completion::Canceled, message});
        }
    }

  private:
    mutable std::mutex mutex_;
    std::shared_ptr<PrintSession> activeSession_;
};

void finishSession(const std::weak_ptr<PrintControllerState>& weakState,
                   const std::shared_ptr<PrintSession>& session, PrintResult result) {
    if (const auto state = weakState.lock()) {
        state->finish(session, std::move(result));
    } else {
        session->finish(std::move(result));
    }
}

#endif

} // namespace

class WindowsPrintController::Impl final {
  public:
#if defined(Q_OS_WIN)
    Impl()
        : apartmentResult_(RoInitialize(RO_INIT_SINGLETHREADED)),
          state_(std::make_shared<PrintControllerState>()) {}

    ~Impl() {
        releaseManager();
        state_->cancelActive(QStringLiteral("The print operation was canceled."));
        if (SUCCEEDED(apartmentResult_)) {
            RoUninitialize();
        }
    }

    QFuture<Result> showPrintUI(quintptr ownerWindowId, const QString& documentTitle,
                                const QImage& image) {
        if (FAILED(apartmentResult_)) {
            return readyPrintResult(hresultFailure(
                apartmentResult_, QStringLiteral("Windows Runtime initialization failed")));
        }

        // Qt transports the native HWND through its integer-valued window ID type.
        const HWND owner =
            reinterpret_cast<HWND>(ownerWindowId); // NOLINT(performance-no-int-to-ptr)
        if (!owner || !IsWindow(owner)) {
            return readyPrintResult(
                failedResult(QStringLiteral("The viewer window is not available.")));
        }
        if (image.isNull()) {
            return readyPrintResult(
                failedResult(QStringLiteral("The image is not available for printing.")));
        }
        if (state_->activeSession()) {
            return readyPrintResult(
                failedResult(QStringLiteral("Another print operation is already in progress.")));
        }

        try {
            if (!PrintManager::IsSupported()) {
                return readyPrintResult(
                    failedResult(QStringLiteral("Printing is not supported on this device.")));
            }

            ensureManager(owner);
            auto document = Make<ImagePrintDocument>();
            const UINT windowDpi = GetDpiForWindow(owner);
            winrt::check_hresult(document->RuntimeClassInitialize(
                image, windowDpi == 0 ? kDefaultDpi : static_cast<float>(windowDpi)));

            ComPtr<PrintDocumentSource> abiSource;
            winrt::check_hresult(document.As(&abiSource));
            winrt::Windows::Graphics::Printing::IPrintDocumentSource source{
                abiSource.Detach(), winrt::take_ownership_from_abi};
            const QString title =
                documentTitle.isEmpty() ? QStringLiteral("Snow Image Viewer") : documentTitle;
            const auto session = std::make_shared<PrintSession>(title, std::move(source));
            const QFuture<Result> future = session->future();
            if (!state_->begin(session)) {
                session->finish(failedResult(
                    QStringLiteral("Another print operation is already in progress.")));
                return future;
            }

            try {
                winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
                winrt::check_hresult(interop_->ShowPrintUIForWindowAsync(
                    owner, winrt::guid_of<winrt::Windows::Foundation::IAsyncOperation<bool>>(),
                    winrt::put_abi(operation)));
                const std::weak_ptr<PrintControllerState> weakState = state_;
                operation.Completed([weakState,
                                     session](const auto& completedOperation,
                                              winrt::Windows::Foundation::AsyncStatus status) {
                    if (status == winrt::Windows::Foundation::AsyncStatus::Canceled) {
                        finishSession(weakState, session,
                                      {Result::Completion::Canceled, QString()});
                        return;
                    }

                    try {
                        if (status != winrt::Windows::Foundation::AsyncStatus::Completed ||
                            !completedOperation.GetResults()) {
                            finishSession(weakState, session,
                                          failedResult(QStringLiteral(
                                              "Windows could not show the print UI.")));
                        }
                    } catch (const winrt::hresult_error& error) {
                        finishSession(
                            weakState, session,
                            winrtFailure(error,
                                         QStringLiteral("Windows could not show the print UI")));
                    } catch (...) {
                        finishSession(
                            weakState, session,
                            hresultFailure(winrt::to_hresult(),
                                           QStringLiteral("Windows could not show the print UI")));
                    }
                });
            } catch (const winrt::hresult_error& error) {
                state_->finish(
                    session,
                    winrtFailure(error, QStringLiteral("Windows could not show the print UI")));
            } catch (...) {
                state_->finish(
                    session, hresultFailure(winrt::to_hresult(),
                                            QStringLiteral("Windows could not show the print UI")));
            }
            return future;
        } catch (const winrt::hresult_error& error) {
            return readyPrintResult(
                winrtFailure(error, QStringLiteral("Windows printing could not be initialized")));
        } catch (...) {
            return readyPrintResult(hresultFailure(
                winrt::to_hresult(), QStringLiteral("Windows printing could not be initialized")));
        }
    }

  private:
    void ensureManager(HWND owner) {
        if (owner_ == owner && manager_) {
            return;
        }

        releaseManager();
        interop_ = winrt::get_activation_factory<PrintManager, IPrintManagerInterop>();
        PrintManager manager{nullptr};
        winrt::check_hresult(interop_->GetForWindow(
            owner, winrt::guid_of<winrt::Windows::Graphics::Printing::IPrintManager>(),
            winrt::put_abi(manager)));

        const std::weak_ptr<PrintControllerState> weakState = state_;
        printTaskRequestedToken_ = manager.PrintTaskRequested([weakState](const PrintManager&,
                                                                          const auto& args) {
            const auto state = weakState.lock();
            const auto session = state ? state->activeSession() : nullptr;
            if (!state || !session) {
                return;
            }

            try {
                const auto request = args.Request();
                const auto printTask = request.CreatePrintTask(
                    session->title.toStdWString(), [weakState, session](const auto& sourceArgs) {
                        try {
                            sourceArgs.SetSource(session->source);
                        } catch (const winrt::hresult_error& error) {
                            finishSession(
                                weakState, session,
                                winrtFailure(
                                    error,
                                    QStringLiteral("Windows could not read the print content")));
                            throw;
                        } catch (...) {
                            finishSession(
                                weakState, session,
                                hresultFailure(
                                    winrt::to_hresult(),
                                    QStringLiteral("Windows could not read the print content")));
                            throw;
                        }
                    });

                printTask.Completed([weakState, session](const auto&, const auto& completedArgs) {
                    switch (completedArgs.Completion()) {
                    case PrintTaskCompletion::Submitted:
                        finishSession(weakState, session,
                                      {Result::Completion::Submitted, QString()});
                        break;
                    case PrintTaskCompletion::Failed:
                        finishSession(weakState, session,
                                      failedResult(QStringLiteral(
                                          "Windows could not submit the print job.")));
                        break;
                    case PrintTaskCompletion::Abandoned:
                        finishSession(weakState, session,
                                      {Result::Completion::Abandoned, QString()});
                        break;
                    case PrintTaskCompletion::Canceled:
                    default:
                        finishSession(weakState, session,
                                      {Result::Completion::Canceled, QString()});
                        break;
                    }
                });
            } catch (const winrt::hresult_error& error) {
                finishSession(
                    weakState, session,
                    winrtFailure(error, QStringLiteral("Windows could not create the print task")));
            } catch (...) {
                finishSession(
                    weakState, session,
                    hresultFailure(winrt::to_hresult(),
                                   QStringLiteral("Windows could not create the print task")));
            }
        });
        manager_ = std::move(manager);
        owner_ = owner;
    }

    void releaseManager() noexcept {
        if (manager_ && printTaskRequestedToken_.value != 0) {
            try {
                manager_.PrintTaskRequested(printTaskRequestedToken_);
            } catch (...) {
                (void)winrt::to_hresult();
            }
        }
        printTaskRequestedToken_ = {};
        manager_ = nullptr;
        interop_ = nullptr;
        owner_ = nullptr;
    }

    HRESULT apartmentResult_ = E_UNEXPECTED;
    HWND owner_ = nullptr;
    winrt::com_ptr<IPrintManagerInterop> interop_;
    PrintManager manager_{nullptr};
    winrt::event_token printTaskRequestedToken_{};
    std::shared_ptr<PrintControllerState> state_;
#else
    QFuture<Result> showPrintUI(quintptr, const QString&, const QImage&) {
        return readyPrintResult(
            failedResult(QStringLiteral("Windows printing is not available on this platform.")));
    }
#endif
};

WindowsPrintController::WindowsPrintController() : impl_(std::make_unique<Impl>()) {}

WindowsPrintController::~WindowsPrintController() = default;

QFuture<WindowsPrintController::Result>
WindowsPrintController::showPrintUI(quintptr ownerWindowId, const QString& documentTitle,
                                    const QImage& image) {
    return impl_->showPrintUI(ownerWindowId, documentTitle, image);
}

} // namespace snow::image_viewer
