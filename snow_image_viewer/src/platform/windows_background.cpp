#include "platform/windows_background.h"

#include <snow/image/service.h>

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QtConcurrent>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

#if defined(Q_OS_WIN)
#include <qt_windows.h>

#include <objbase.h>
#include <roapi.h>
#include <shobjidl_core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/base.h>

#include <string>
#endif

namespace snow::image_viewer {
namespace {

#if defined(Q_OS_WIN)
class SaveFileSink final : public snow::image::ByteSink {
  public:
    explicit SaveFileSink(QSaveFile& file) : file_(file) {}

    snow::image::Result<void> write(std::span<const std::byte> source) override {
        if (source.size() > static_cast<std::size_t>(std::numeric_limits<qint64>::max())) {
            return failure("The encoded PNG chunk exceeds Qt's file size limit.");
        }
        const qint64 size = static_cast<qint64>(source.size());
        if (file_.write(reinterpret_cast<const char*>(source.data()), size) != size) {
            return failure(file_.errorString().toStdString());
        }
        position_ += source.size();
        return {};
    }

    snow::image::Result<std::uint64_t> position() const override {
        return position_;
    }

    snow::image::Result<void> seek(std::uint64_t) override {
        return snow::image::Status::error(snow::image::ErrorCode::unsupported_feature,
                                          "The background PNG output is not seekable.",
                                          "snow_image_viewer");
    }

    snow::image::Result<void> flush() override {
        return {};
    }

    bool seekable() const noexcept override {
        return false;
    }

  private:
    static snow::image::Status failure(std::string message) {
        return snow::image::Status::error(snow::image::ErrorCode::io_error, std::move(message),
                                          "snow_image_viewer");
    }

    QSaveFile& file_;
    std::uint64_t position_ = 0;
};

class ComApartment final {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

    ~ComApartment() {
        // CoInitializeEx requires balancing both S_OK and S_FALSE with CoUninitialize.
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    bool isAvailable() const {
        // RPC_E_CHANGED_MODE means this thread was already initialized in another apartment.
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT result() const {
        return result_;
    }

  private:
    HRESULT result_ = E_UNEXPECTED;
};

class WindowsRuntimeApartment final {
  public:
    WindowsRuntimeApartment() : result_(RoInitialize(RO_INIT_MULTITHREADED)) {}

    ~WindowsRuntimeApartment() {
        if (SUCCEEDED(result_)) {
            RoUninitialize();
        }
    }

    HRESULT result() const {
        return result_;
    }

  private:
    HRESULT result_ = E_UNEXPECTED;
};

QString hresultCode(HRESULT result) {
    const QString digits =
        QStringLiteral("%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    return QStringLiteral("0x%1").arg(digits.toUpper());
}

QString hresultMessage(HRESULT result) {
    const winrt::hresult_error error(result);
    QString message = QString::fromWCharArray(error.message().c_str()).trimmed();
    while (message.endsWith(QLatin1Char('.')) || message.endsWith(QLatin1Char('\r')) ||
           message.endsWith(QLatin1Char('\n'))) {
        message.chop(1);
    }
    return message.trimmed();
}

void setHresultError(QString* errorMessage, const QString& operation, HRESULT result) {
    if (!errorMessage) {
        return;
    }

    const QString detail = hresultMessage(result);
    *errorMessage =
        detail.isEmpty()
            ? QStringLiteral("%1 (%2).").arg(operation, hresultCode(result))
            : QStringLiteral("%1: %2 (%3).").arg(operation, detail, hresultCode(result));
}

QString preparedFilePrefix(WindowsBackgroundController::Target target) {
    return target == WindowsBackgroundController::Target::LockScreen
               ? QStringLiteral("snow-lock-screen-")
               : QStringLiteral("snow-desktop-background-");
}
#endif

} // namespace

class WindowsBackgroundController::Impl final {
  public:
    QFuture<Result> setImage(Target target, QImage image) {
        return QtConcurrent::run([target, image = std::move(image)]() mutable {
            return setImageInWorker(target, image);
        });
    }

  private:
#if defined(Q_OS_WIN)
    static Result setImageInWorker(Target target, const QImage& image) {
        Result result;
        const QString imagePath = saveCompatibleImage(target, image, &result.errorMessage);
        if (imagePath.isEmpty()) {
            return result;
        }

        result = target == Target::LockScreen ? setLockScreenImage(imagePath)
                                              : setDesktopWallpaper(imagePath);
        if (!result.changed) {
            QFile::remove(imagePath);
        }
        // On success the file intentionally remains in app data. SetWallpaper consumes a path,
        // so deleting the active image would make the persisted Windows setting invalid.
        return result;
    }

    static Result setDesktopWallpaper(const QString& imagePath) {
        Result result;
        const ComApartment apartment;
        if (!apartment.isAvailable()) {
            setHresultError(&result.errorMessage,
                            QStringLiteral("Windows could not initialize COM"), apartment.result());
            return result;
        }

        winrt::com_ptr<IDesktopWallpaper> desktopWallpaper;
        HRESULT operationResult =
            CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
                             __uuidof(IDesktopWallpaper), desktopWallpaper.put_void());
        if (FAILED(operationResult)) {
            setHresultError(&result.errorMessage,
                            QStringLiteral("Windows could not open the desktop wallpaper service"),
                            operationResult);
            return result;
        }

        const std::wstring nativeImagePath =
            QDir::toNativeSeparators(QDir::cleanPath(imagePath)).toStdWString();
        // Microsoft documents a null monitor ID as applying the image to every monitor.
        operationResult = desktopWallpaper->SetWallpaper(nullptr, nativeImagePath.c_str());
        if (operationResult != S_OK) {
            setHresultError(&result.errorMessage,
                            QStringLiteral("Windows could not set the desktop background"),
                            operationResult);
            return result;
        }

        removeSupersededDesktopImages(imagePath);
        result.changed = true;
        return result;
    }

    static void removeSupersededDesktopImages(const QString& activeImagePath) {
        const QFileInfo activeImage(activeImagePath);
        const QString activePath = QDir::cleanPath(activeImage.absoluteFilePath());
        QDir outputDirectory(activeImage.absolutePath());
        const QStringList filters{QStringLiteral("snow-desktop-background-*.png"),
                                  QStringLiteral("snow-background-*.png")};
        const QFileInfoList candidates =
            outputDirectory.entryInfoList(filters, QDir::Files | QDir::NoSymLinks);
        for (const QFileInfo& candidate : candidates) {
            if (QDir::cleanPath(candidate.absoluteFilePath()) != activePath) {
                QFile::remove(candidate.absoluteFilePath());
            }
        }
    }

    static Result setLockScreenImage(const QString& imagePath) {
        Result result;
        const WindowsRuntimeApartment apartment;
        if (FAILED(apartment.result())) {
            setHresultError(&result.errorMessage,
                            QStringLiteral("Windows could not initialize the Windows Runtime"),
                            apartment.result());
            return result;
        }

        QString operation = QStringLiteral("Windows could not open the prepared lock screen image");
        try {
            const std::wstring nativeImagePath =
                QDir::toNativeSeparators(QDir::cleanPath(imagePath)).toStdWString();
            const winrt::Windows::Storage::StorageFile imageFile =
                winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(nativeImagePath).get();

            operation = QStringLiteral("Windows could not set the lock screen background");
            winrt::Windows::System::UserProfile::LockScreen::SetImageFileAsync(imageFile).get();
            result.changed = true;
        } catch (const winrt::hresult_error& error) {
            setHresultError(&result.errorMessage, operation, error.code());
        }
        return result;
    }

    static QString saveCompatibleImage(Target target, const QImage& image, QString* errorMessage) {
        if (image.isNull() || !image.size().isValid()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The image is not available.");
            }
            return {};
        }

        const QString appDataPath =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (appDataPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows could not locate the app data folder.");
            }
            return {};
        }

        QDir outputDirectory(QDir(appDataPath).filePath(QStringLiteral("backgrounds")));
        if (!outputDirectory.mkpath(QStringLiteral("."))) {
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("Windows could not create the background image folder.");
            }
            return {};
        }

        const QColorSpace srgb(QColorSpace::SRgb);
        QImage compatibleImage;
        if (image.colorSpace().isValid()) {
            compatibleImage =
                image.convertedToColorSpace(srgb, QImage::Format_RGBX8888, Qt::AutoColor);
        }
        if (compatibleImage.isNull()) {
            compatibleImage = image.convertToFormat(QImage::Format_RGBX8888);
            compatibleImage.setColorSpace(srgb);
        }
        if (compatibleImage.isNull()) {
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("The image could not be converted to sRGB for Windows.");
            }
            return {};
        }

        snow::image::Result<snow::image::MutableImage> allocated =
            snow::image::MutableImage::allocate(
                static_cast<std::uint32_t>(compatibleImage.width()),
                static_cast<std::uint32_t>(compatibleImage.height()), snow::image::kRgb8);
        if (!allocated) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The background image could not be prepared for PNG "
                                               "encoding: %1")
                                    .arg(QString::fromStdString(allocated.error().message));
            }
            return {};
        }

        snow::image::MutableImage encodedImage = std::move(allocated).value();
        const std::size_t outputStride = encodedImage.row_stride();
        std::span<std::byte> outputPixels = encodedImage.pixels();
        for (int y = 0; y < compatibleImage.height(); ++y) {
            const uchar* source = compatibleImage.constScanLine(y);
            std::byte* destination =
                outputPixels.data() + static_cast<std::size_t>(y) * outputStride;
            for (int x = 0; x < compatibleImage.width(); ++x) {
                destination[0] = static_cast<std::byte>(source[0]);
                destination[1] = static_cast<std::byte>(source[1]);
                destination[2] = static_cast<std::byte>(source[2]);
                source += 4;
                destination += 3;
            }
        }

        snow::image::Document document;
        document.format = snow::image::Format::png;
        document.canvas_width = encodedImage.width();
        document.canvas_height = encodedImage.height();
        document.color.primaries = snow::image::ColorPrimaries::srgb;
        document.color.transfer = snow::image::TransferFunction::srgb;
        snow::image::Frame frame;
        frame.image = std::move(encodedImage).freeze();
        frame.color = document.color;
        document.frames.push_back(std::move(frame));

        QString outputPath = outputDirectory.absoluteFilePath(
            QStringLiteral("%1%2.png")
                .arg(preparedFilePrefix(target),
                     QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QSaveFile outputFile(outputPath);
        if (!outputFile.open(QIODevice::WriteOnly)) {
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("The prepared background image could not be created: %1")
                        .arg(outputFile.errorString());
            }
            return {};
        }

        const snow::image::Output output{std::make_shared<SaveFileSink>(outputFile),
                                         "background.png"};
        snow::image::EncodeOptions options;
        options.format = snow::image::Format::png;
        const snow::image::Service service;
        const snow::image::Result<snow::image::EncodeResult> encoded =
            service.encode(document, output, options);
        if (!encoded) {
            outputFile.cancelWriting();
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("The background image could not be encoded as PNG: %1")
                        .arg(QString::fromStdString(encoded.error().message));
            }
            return {};
        }
        if (!outputFile.commit()) {
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("The prepared background image could not be saved: %1")
                        .arg(outputFile.errorString());
            }
            return {};
        }
        return outputPath;
    }

#else
    static Result setImageInWorker(Target, const QImage&) {
        Result result;
        result.errorMessage =
            QStringLiteral("Windows personalization is not available on this platform.");
        return result;
    }
#endif
};

WindowsBackgroundController::WindowsBackgroundController() : impl_(std::make_unique<Impl>()) {}

WindowsBackgroundController::~WindowsBackgroundController() = default;

QFuture<WindowsBackgroundController::Result> WindowsBackgroundController::setImage(Target target,
                                                                                   QImage image) {
    return impl_->setImage(target, std::move(image));
}

} // namespace snow::image_viewer
