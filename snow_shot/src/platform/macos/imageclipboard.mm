#include "imageclipboard.h"
#include "snowimageqtcodec.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QUtiMimeConverter>
#include <QUrl>
#include <QVariant>

#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstring>
#include <memory>
#include <type_traits>

namespace snow_shot::platform::macos {
namespace {
template <typename T>
using CfPointer = std::unique_ptr<std::remove_pointer_t<T>, decltype(&CFRelease)>;

CfPointer<CFMutableDataRef> encodeImageData(CGImageRef image, CFStringRef type) {
    if (image == nullptr)
        return {nullptr, CFRelease};
    CfPointer<CFMutableDataRef> bytes(CFDataCreateMutable(nullptr, 0), CFRelease);
    if (!bytes)
        return {nullptr, CFRelease};
    CfPointer<CGImageDestinationRef> destination(
        CGImageDestinationCreateWithData(bytes.get(), type, 1, nullptr), CFRelease);
    if (!destination)
        return {nullptr, CFRelease};
    CGImageDestinationAddImage(destination.get(), image, nullptr);
    if (!CGImageDestinationFinalize(destination.get()))
        return {nullptr, CFRelease};
    return bytes;
}

QByteArray encodeImage(CGImageRef image, CFStringRef type) {
    const auto bytes = encodeImageData(image, type);
    if (!bytes)
        return {};
    return QByteArray::fromCFData(bytes.get());
}

CfPointer<CFMutableDataRef> tiffFromPng(NSData* png) {
    CfPointer<CGImageSourceRef> source(
        CGImageSourceCreateWithData(reinterpret_cast<CFDataRef>(png), nullptr), CFRelease);
    CfPointer<CGImageRef> image(
        source ? CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr) : nullptr, CFRelease);
    return encodeImageData(image.get(), CFSTR("public.tiff"));
}

class ImageClipboardConverter final : public QObject, public QUtiMimeConverter {
  public:
    QString utiForMime(const QString& mime) const override {
        if (mime == QStringLiteral("image/png"))
            return QStringLiteral("public.png");
        return mime == QStringLiteral("application/x-qt-image") ? QStringLiteral("public.tiff")
                                                                : QString{};
    }
    QString mimeForUti(const QString& uti) const override {
        if (uti == QStringLiteral("public.png"))
            return QStringLiteral("image/png");
        return uti == QStringLiteral("public.tiff") ? QStringLiteral("application/x-qt-image")
                                                    : QString{};
    }
    QList<QByteArray> convertFromMime(const QString& mime, const QVariant& data,
                                      const QString& uti) const override {
        if (!canConvert(mime, uti))
            return {};
        if (mime == QStringLiteral("image/png"))
            return {data.toByteArray()};
        // Qt 6.11.1 and 6.12.0's TIFF converters lose this retained CGImage. Keep the
        // native representation and its pixel provider owned through encoding.
        const QImage source = qvariant_cast<QImage>(data);
        CfPointer<CGImageRef> image(source.toCGImage(), CFRelease);
        const QByteArray bytes = encodeImage(image.get(), CFSTR("public.tiff"));
        return bytes.isEmpty() ? QList<QByteArray>{} : QList<QByteArray>{bytes};
    }
    QVariant convertToMime(const QString& mime, const QList<QByteArray>& data,
                           const QString& uti) const override {
        if (!canConvert(mime, uti) || data.isEmpty())
            return {};
        if (mime == QStringLiteral("image/png"))
            return data.first();
        return decodeNativeClipboardImage(data.first());
    }
};
} // namespace

void initializeImageClipboardConverter() {
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return;
    static QPointer<ImageClipboardConverter> converter;
    if (!converter) {
        // Initialize Qt's lazy built-in MIME registry before prepending ours.
        static_cast<void>(QGuiApplication::clipboard()->mimeData());
        // The platform registry deletes registered converters at shutdown.
        // An application QObject parent would delete this earlier, while Qt
        // deliberately stops unregistering converters during closingDown().
        converter = new ImageClipboardConverter;
    }
}
} // namespace snow_shot::platform::macos

// The pasteboard server owns the canonical PNG. This owner retains only its
// revision, so losing ownership while the app is inactive cannot retain pixels
// or encoded image data. TIFF is materialized only for consumers that need it.
@interface SnowShotImagePasteboardOwner : NSObject <NSPasteboardTypeOwner> {
  @public
    NSInteger revision;
}
- (void)pasteboard:(NSPasteboard*)pasteboard provideDataForType:(NSPasteboardType)type;
@end

@implementation SnowShotImagePasteboardOwner
- (void)pasteboard:(NSPasteboard*)pasteboard provideDataForType:(NSPasteboardType)type {
    @autoreleasepool {
        if (![type isEqualToString:NSPasteboardTypeTIFF] || pasteboard.changeCount != revision)
            return;
        NSData* png = [pasteboard dataForType:NSPasteboardTypePNG];
        if (png == nil)
            return;
        const auto tiff = snow_shot::platform::macos::tiffFromPng(png);
        if (tiff && pasteboard.changeCount == revision)
            [pasteboard setData:reinterpret_cast<NSData*>(tiff.get()) forType:type];
    }
}
@end

namespace snow_shot::platform::macos {
namespace {
class ImagePasteboardOwner final : public QObject {
  public:
    explicit ImagePasteboardOwner(QObject* parent) : QObject(parent) {
        native->revision = -1;
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                &ImagePasteboardOwner::finishPromises);
    }
    ~ImagePasteboardOwner() override {
        finishPromises();
        [native release];
    }

    void finishPromises() const {
        @autoreleasepool {
            NSPasteboard* board = NSPasteboard.generalPasteboard;
            if (board.changeCount == native->revision)
                static_cast<void>([board dataForType:NSPasteboardTypeTIFF]);
        }
    }

    SnowShotImagePasteboardOwner* native = [[SnowShotImagePasteboardOwner alloc] init];
    bool published = false;
};

QPointer<ImagePasteboardOwner> imagePasteboardOwner;
} // namespace

bool imageClipboardBitmapIsDerivedFromPng() {
    if (QGuiApplication::platformName() != QStringLiteral("cocoa") || !imagePasteboardOwner ||
        !imagePasteboardOwner->published)
        return false;
    return NSPasteboard.generalPasteboard.changeCount == imagePasteboardOwner->native->revision;
}

bool imageClipboardHasOnlyPng() {
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return false;
    @autoreleasepool {
        NSArray<NSPasteboardItem*>* items = NSPasteboard.generalPasteboard.pasteboardItems;
        return items.count == 1 &&
               [items.firstObject.types isEqualToArray:@[ NSPasteboardTypePNG ]];
    }
}

QByteArray copyTiffOnlyClipboardItem(qint64 maximumBytes,
                                     const std::function<bool(qint64)>& reserve) {
    if (QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return {};
    @autoreleasepool {
        NSArray<NSPasteboardItem*>* items = NSPasteboard.generalPasteboard.pasteboardItems;
        if (items.count != 1 || ![items.firstObject.types isEqualToArray:@[ NSPasteboardTypeTIFF ]])
            return {};
        NSData* data = [items.firstObject dataForType:NSPasteboardTypeTIFF];
        if (!data || data.length == 0 || data.length > static_cast<NSUInteger>(maximumBytes))
            return {};
        if (reserve && !reserve(static_cast<qint64>(data.length)))
            return {};
        return QByteArray::fromNSData(data);
    }
}

namespace {
QSize nativeClipboardImageSize(CGImageSourceRef source) {
    const CfPointer<CFDictionaryRef> properties(
        CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr), CFRelease);
    if (!properties)
        return {};
    const auto readDimension = [&properties](CFStringRef key, int* value) {
        const auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(properties.get(), key));
        return number && CFNumberGetTypeID() == CFGetTypeID(number) &&
               CFNumberGetValue(number, kCFNumberIntType, value);
    };
    int width = 0;
    int height = 0;
    if (!readDimension(kCGImagePropertyPixelWidth, &width) ||
        !readDimension(kCGImagePropertyPixelHeight, &height))
        return {};
    return {width, height};
}

QByteArray encodeBoundedClipboardPng(CGImageRef image, qsizetype capacity) {
    struct Output {
        char* bytes;
        size_t capacity;
        size_t length = 0;
        bool overflow = false;
    };
    QByteArray png(capacity, Qt::Uninitialized);
    Output output{png.data(), static_cast<size_t>(capacity)};
    const CGDataConsumerCallbacks callbacks{
        [](void* context, const void* bytes, size_t length) -> size_t {
            auto& sink = *static_cast<Output*>(context);
            if (sink.overflow || length > sink.capacity - sink.length) {
                sink.overflow = true;
                return 0;
            }
            std::memcpy(sink.bytes + sink.length, bytes, length);
            sink.length += length;
            return length;
        },
        nullptr,
    };
    const CfPointer<CGDataConsumerRef> consumer(CGDataConsumerCreate(&output, &callbacks),
                                                CFRelease);
    const CfPointer<CGImageDestinationRef> destination(
        consumer ? CGImageDestinationCreateWithDataConsumer(consumer.get(), CFSTR("public.png"), 1,
                                                            nullptr)
                 : nullptr,
        CFRelease);
    if (!destination)
        return {};
    CGImageDestinationAddImage(destination.get(), image, nullptr);
    if (!CGImageDestinationFinalize(destination.get()) || output.overflow)
        return {};
    png.resize(static_cast<qsizetype>(output.length));
    return png;
}
} // namespace

QImage decodeNativeClipboardImage(const QByteArray& encoded, qint64 maximumPixels,
                                  const std::function<bool(qint64)>& reserveWorkingBytes) {
    @autoreleasepool {
        CfPointer<CFMutableDataRef> nativePng(nullptr, CFRelease);
        QByteArray png;
        {
            const CfPointer<CFDataRef> bytes(encoded.toRawCFData(), CFRelease);
            const CfPointer<CGImageSourceRef> source(
                bytes ? CGImageSourceCreateWithData(bytes.get(), nullptr) : nullptr, CFRelease);
            if (!source)
                return {};
            qint64 pngCapacity = 0;
            if (maximumPixels > 0 || reserveWorkingBytes) {
                const QSize size = nativeClipboardImageSize(source.get());
                if (size.isEmpty() ||
                    (maximumPixels > 0 &&
                     static_cast<qint64>(size.width()) * size.height() > maximumPixels))
                    return {};
                if (reserveWorkingBytes) {
                    // Allow two aligned 16-bit RGBA rasters for each conversion stage. The
                    // bounded PNG buffer also allows for compression overhead and metadata.
                    const qint64 rowBytes =
                        (static_cast<qint64>(size.width()) * 8 + 63) & ~qint64{63};
                    const qint64 rasterBytes = rowBytes * size.height();
                    pngCapacity = rasterBytes + rasterBytes / 8 + encoded.size() + 64 * 1024;
                    if (!reserveWorkingBytes(2 * rasterBytes + pngCapacity))
                        return {};
                }
            }
            const CfPointer<CGImageRef> image(
                CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr), CFRelease);
            if (!image)
                return {};
            if (reserveWorkingBytes)
                png = encodeBoundedClipboardPng(image.get(), pngCapacity);
            else
                nativePng = encodeImageData(image.get(), CFSTR("public.png"));
        }
        if (nativePng)
            png = QByteArray::fromRawCFData(nativePng.get());
        if (png.isEmpty())
            return {};
        // Preserve the managed reader's alpha, 16-bit color and metadata behavior.
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::ReadOnly))
            return {};
        QImageReader reader(&buffer, "PNG");
        return image_codec::readManagedImage(reader);
    }
}

QStringList clipboardFileIconMimeTypes(const QMimeData& mimeData) {
    if (QGuiApplication::platformName() != QStringLiteral("cocoa") || !mimeData.hasUrls() ||
        QGuiApplication::clipboard()->mimeData() != &mimeData)
        return {};

    @autoreleasepool {
        bool hasFileIcon = false;
        bool hasExplicitPng = false;
        bool hasExplicitTiff = false;
        // Cocoa can synthesize PNG and TIFF from Finder's ICNS. The individual
        // items list the original representations without those conversions.
        for (NSPasteboardItem* item in NSPasteboard.generalPasteboard.pasteboardItems) {
            const auto* types = item.types;
            if ([types containsObject:NSPasteboardTypePNG])
                hasExplicitPng = true;
            if ([types containsObject:NSPasteboardTypeTIFF])
                hasExplicitTiff = true;
            if ([types containsObject:NSPasteboardTypeFileURL] &&
                [types containsObject:@"com.apple.icns"])
                hasFileIcon = true;
        }
        QStringList formats;
        if (hasFileIcon && !hasExplicitPng)
            formats.append(QStringLiteral("image/png"));
        if (hasFileIcon && !hasExplicitTiff)
            formats.append(QStringLiteral("application/x-qt-image"));
        return formats;
    }
}

std::optional<quint64> publishImageClipboard(QClipboard* clipboard, const QByteArray& png,
                                             const QByteArray& placement,
                                             const QByteArray& appearance,
                                             const QString& filePath) {
    if (clipboard == nullptr || png.isEmpty())
        return std::nullopt;
    initializeImageClipboardConverter();
    if (!imagePasteboardOwner)
        imagePasteboardOwner = new ImagePasteboardOwner(QCoreApplication::instance());
    auto* owner = imagePasteboardOwner.data();
    owner->published = false;
    bool published = false;
    @autoreleasepool {
        NSPasteboard* board = NSPasteboard.generalPasteboard;
        NSMutableArray<NSPasteboardType>* types =
            [NSMutableArray arrayWithObjects:NSPasteboardTypePNG, NSPasteboardTypeTIFF, nil];
        if (!placement.isEmpty())
            [types addObject:@"com.snowshot.screenshot-placement"];
        if (!appearance.isEmpty())
            [types addObject:@"com.snowshot.screenshot-appearance"];
        if (!filePath.isEmpty())
            [types addObject:NSPasteboardTypeFileURL];
        owner->native->revision = [board declareTypes:types owner:owner->native];
        published = [board setData:png.toNSData() forType:NSPasteboardTypePNG];
        if (published && !placement.isEmpty())
            published = [board setData:placement.toNSData()
                               forType:@"com.snowshot.screenshot-placement"];
        if (published && !appearance.isEmpty())
            published = [board setData:appearance.toNSData()
                               forType:@"com.snowshot.screenshot-appearance"];
        if (published && !filePath.isEmpty())
            published = [board
                setString:QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded).toNSString()
                  forType:NSPasteboardTypeFileURL];
    }
    const auto revision = static_cast<quint64>(owner->native->revision);
    owner->published = published;
    // Synchronize Qt's native reader to retire any previously owned QMimeData.
    // Reading here neither decodes the PNG nor changes the native clipboard.
    static_cast<void>(clipboard->mimeData());
    if (published) {
        emit clipboard->dataChanged();
        emit clipboard->changed(QClipboard::Clipboard);
    }
    return published ? std::optional(revision) : std::nullopt;
}
} // namespace snow_shot::platform::macos
