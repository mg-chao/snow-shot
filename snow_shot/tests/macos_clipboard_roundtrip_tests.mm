#include "snow_shot/presentation/screenshotclipboardservice.h"
#include "snow_shot/presentation/screenshotclipboardcontent.h"
#include "snow_shot/presentation/screenshotimageconversionclipboard.h"
#include "pinned_animation_fixtures.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QEventLoop>
#include <QFile>
#include <QMimeData>
#include <QTimer>
#include <QColorSpace>
#include <QPointer>
#include <QProcess>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QUrl>

#import <AppKit/AppKit.h>
#include <ImageIO/ImageIO.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class PasteboardRestore final {
  public:
    PasteboardRestore() {
        NSMutableArray* copies = [NSMutableArray array];
        for (NSPasteboardItem* item in [NSPasteboard generalPasteboard].pasteboardItems) {
            NSPasteboardItem* copy = [[NSPasteboardItem alloc] init];
            for (NSString* type in item.types) {
                NSData* data = [item dataForType:type];
                if (data)
                    [copy setData:data forType:type];
            }
            [copies addObject:copy];
            [copy release];
        }
        m_items = [copies copy];
    }
    ~PasteboardRestore() {
        NSPasteboard* board = [NSPasteboard generalPasteboard];
        [board clearContents];
        if (m_items.count)
            [board writeObjects:m_items];
        [m_items release];
    }

  private:
    NSArray* m_items = nil;
};

bool hasSamePixels(const QImage& actual, const QImage& expected) {
    if (actual.size() != expected.size())
        return false;
    // QImage equality also compares storage format and color-space metadata.
    // Native and codec readers may represent the same pixels differently.
    for (int y = 0; y < actual.height(); ++y) {
        for (int x = 0; x < actual.width(); ++x) {
            if (actual.pixelColor(x, y) != expected.pixelColor(x, y))
                return false;
        }
    }
    return true;
}

QImage imageFixture() {
    QImage source(17, 11, QImage::Format_ARGB32);
    source.setColorSpace(QColorSpace::SRgb);
    source.fill(QColor(40, 80, 120, 255));
    source.setPixelColor(0, 0, QColor(0, 0, 0, 0));
    source.setPixelColor(8, 5, QColor(40, 80, 120, 128));
    return source;
}

void nativeHtmlFormatsRoundTrip() {
    using Format = SnowShotImageConversionFormat;
    const QString html = QStringLiteral("<p><b>Snow \u96ea \u03a9</b> &amp; HTML</p>");
    auto* clipboard = QApplication::clipboard();
    auto source = snow_shot::presentation::imageConversionClipboardMimeData(Format::Html, html);
    clipboard->setMimeData(source.release());
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    require([board.types containsObject:NSPasteboardTypeHTML] &&
                QString::fromNSString([board stringForType:NSPasteboardTypeHTML]) == html &&
                QString::fromNSString([board stringForType:NSPasteboardTypeString]) == html,
            "HTML source Copy must publish native HTML and the source text fallback");

    QTextDocument document;
    document.setHtml(html);
    const QTextCursor cursor = document.find(QStringLiteral("Snow \u96ea \u03a9"));
    auto selected =
        snow_shot::presentation::imageConversionClipboardMimeData(Format::Html, html, cursor);
    clipboard->setMimeData(selected.release());
    QTextDocument copied;
    copied.setHtml(QString::fromNSString([board stringForType:NSPasteboardTypeHTML]));
    require([board.types containsObject:NSPasteboardTypeHTML] &&
                copied.toPlainText() == cursor.selectedText() &&
                copied.find(cursor.selectedText()).charFormat().fontWeight() > QFont::Normal &&
                QString::fromNSString([board stringForType:NSPasteboardTypeString]) ==
                    cursor.selectedText(),
            "selected HTML must retain native Unicode text and bold formatting");

    const QString markdown = QStringLiteral("# Markdown");
    auto plain =
        snow_shot::presentation::imageConversionClipboardMimeData(Format::Markdown, markdown);
    clipboard->setMimeData(plain.release());
    require(![board.types containsObject:NSPasteboardTypeHTML] &&
                QString::fromNSString([board stringForType:NSPasteboardTypeString]) == markdown,
            "Markdown Copy must replace native HTML with plain source text");
}

void readForeignPngInFreshProcess() {
    QByteArray png;
    QBuffer buffer(&png);
    require(buffer.open(QIODevice::WriteOnly) && imageFixture().save(&buffer, "PNG"),
            "foreign PNG fixture must encode");
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    [board clearContents];
    require([board setData:png.toNSData() forType:NSPasteboardTypePNG],
            "foreign PNG-only clipboard must be written");
    QProcess reader;
    reader.start(QCoreApplication::applicationFilePath(),
                 {QStringLiteral("--read-foreign-png"), QStringLiteral("-platform"),
                  QStringLiteral("cocoa")});
    require(reader.waitForFinished(10000) && reader.exitStatus() == QProcess::NormalExit &&
                reader.exitCode() == 0,
            "a fresh process must read native PNG before publishing any image");
}

void readForeignPngBeforePublication() {
    auto snapshot = ScreenshotClipboardContentReader::snapshot(QApplication::clipboard(), 1.0);
    require(snapshot && snapshot->encodedImages.size() == 1 &&
                snapshot->encodedImages.front().mimeType == QStringLiteral("image/png") &&
                snapshot->detachedImage.isNull(),
            "the first PNG-only read must snapshot the encoded bytes without synthesizing TIFF");
    auto content = ScreenshotClipboardContentReader::decode(std::move(*snapshot));
    require(content && hasSamePixels(content->image, imageFixture()) &&
                content->image.colorSpace() == QColorSpace(QColorSpace::SRgb),
            "a fresh PNG reader must preserve pixels, transparency, and color space");
}

void finderFileIconDoesNotMaskGif() {
    const auto gif = pinned_animation_fixtures::images().constFirst();
    QTemporaryDir directory;
    require(directory.isValid(), "GIF clipboard fixture needs temporary storage");
    const QString path = directory.filePath(QStringLiteral("Finder GIF 雪.gif"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(gif.bytes) == gif.bytes.size(),
            "write the animated GIF fixture");
    file.close();

    QImage icon(128, 128, QImage::Format_RGB32);
    icon.fill(Qt::blue);
    const auto iconImage = icon.toCGImage();
    auto* iconData = [NSMutableData data];
    const auto encoder = CGImageDestinationCreateWithData(
        reinterpret_cast<CFMutableDataRef>(iconData), CFSTR("com.apple.icns"), 1, nullptr);
    require(encoder && iconImage, "create an ICNS file icon fixture");
    CGImageDestinationAddImage(encoder, iconImage, nullptr);
    const bool iconEncoded = CGImageDestinationFinalize(encoder);
    CFRelease(encoder);
    CGImageRelease(iconImage);
    require(iconEncoded, "encode the ICNS file icon fixture");

    const auto publishFile = [&](NSData* bitmap, NSPasteboardType type) {
        NSPasteboardItem* item = [[[NSPasteboardItem alloc] init] autorelease];
        require([item setString:QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toNSString()
                        forType:NSPasteboardTypeFileURL] &&
                    [item setData:iconData forType:@"com.apple.icns"] &&
                    (!bitmap || [item setData:bitmap forType:type]),
                "write the file URL and image representations");
        NSPasteboard* board = NSPasteboard.generalPasteboard;
        [board clearContents];
        require([board writeObjects:@[ item ]], "publish a file copy");
    };
    publishFile(nil, nil);

    auto* clipboard = QApplication::clipboard();
    const auto* mime = clipboard->mimeData();
    require(mime->hasImage() && clipboard->image().size() == icon.size(),
            "Cocoa must expose the synthesized file icon as a Qt clipboard image");
    require(!ScreenshotClipboardContentReader::snapshotImage(clipboard, 1.0),
            "the image-only reader must not capture a file icon");
    require(ScreenshotClipboardContentReader::filePathsForImport(mime) == QStringList{path},
            "clipboard pinning must route Finder copies through file import");
    const auto direct = ScreenshotClipboardContentReader::readMimeData(mime, 1.0);
    require(direct && direct->animationSource && direct->originalContent.localFilePath == path,
            "a direct MIME read must also import the GIF file");
    auto snapshot = ScreenshotClipboardContentReader::snapshot(clipboard, 1.0);
    require(snapshot && snapshot->localImage && snapshot->detachedImage.isNull(),
            "the clipboard reader must retain the GIF file without capturing its icon");
    auto content = ScreenshotClipboardContentReader::decode(std::move(*snapshot));
    require(content && content->image.size() == QSize(2, 2) &&
                content->image.pixelColor(0, 0) == Qt::red && content->animationSource &&
                content->animationSource->encoded == gif.bytes &&
                content->originalContent.localFilePath == path,
            "Finder GIF pinning must keep the actual pixels, file path, and animation");

    const auto source = imageFixture();
    const auto sourceImage = source.toCGImage();
    NSBitmapImageRep* representation =
        [[[NSBitmapImageRep alloc] initWithCGImage:sourceImage] autorelease];
    CGImageRelease(sourceImage);
    NSData* tiff = representation.TIFFRepresentation;
    require(tiff.length > 0, "encode an explicitly copied TIFF bitmap");
    publishFile(tiff, NSPasteboardTypeTIFF);
    require(ScreenshotClipboardContentReader::filePathsForImport(clipboard->mimeData()).isEmpty(),
            "an explicitly copied bitmap must take priority over a file URL and icon");
    auto mixed = ScreenshotClipboardContentReader::snapshot(clipboard, 1.0);
    require(mixed && mixed->encodedImages.isEmpty() && hasSamePixels(mixed->detachedImage, source),
            "the clipboard reader must retain the explicitly copied bitmap");

    const auto png = ScreenshotClipboardService::prepareImage(source).pngBytes();
    publishFile(png.toNSData(), NSPasteboardTypePNG);
    require(ScreenshotClipboardContentReader::filePathsForImport(clipboard->mimeData()).isEmpty(),
            "an explicitly copied PNG must take priority over a file URL and icon");
    mixed = ScreenshotClipboardContentReader::snapshot(clipboard, 1.0);
    require(mixed && mixed->encodedImages.size() == 1 && mixed->detachedImage.isNull(),
            "the clipboard reader must retain an explicitly copied PNG without the icon");
    content = ScreenshotClipboardContentReader::decode(std::move(*mixed));
    require(content && hasSamePixels(content->image, source),
            "the explicitly copied PNG must retain its original pixels");
}

void snapshotSkipsDerivedBitmapAndRetainsForeignFallback() {
    const QImage source = imageFixture();
    auto* clipboard = QApplication::clipboard();
    require(ScreenshotClipboardService::publishImage(clipboard, source),
            "snapshot fixture publication must succeed");
    auto snapshot = ScreenshotClipboardContentReader::snapshot(clipboard, 1.0);
    require(snapshot && snapshot->encodedImages.size() == 1 && snapshot->detachedImage.isNull(),
            "a published PNG snapshot must not materialize its derived bitmap");

    // Save TIFF for an independent foreign item, then replace the clipboard.
    // The snapshot must remain usable without any live pasteboard access.
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    NSData* tiff = [[board dataForType:NSPasteboardTypeTIFF] retain];
    require(tiff.length > 0, "native TIFF compatibility must remain available");
    [board clearContents];
    require([board setData:tiff forType:NSPasteboardTypeTIFF] &&
                [board setData:[@"corrupt PNG" dataUsingEncoding:NSUTF8StringEncoding]
                       forType:NSPasteboardTypePNG],
            "foreign clipboard with corrupt PNG and valid TIFF must be written");
    [tiff release];
    auto content = ScreenshotClipboardContentReader::decode(std::move(*snapshot));
    require(content && hasSamePixels(content->image, source),
            "an encoded snapshot must survive replacement of the clipboard");

    auto foreign = ScreenshotClipboardContentReader::snapshot(clipboard, 1.0);
    require(foreign && !foreign->detachedImage.isNull(),
            "foreign clipboard replacement must retain its independent bitmap fallback");
    clipboard->clear();
    content = ScreenshotClipboardContentReader::decode(std::move(*foreign));
    require(content && hasSamePixels(content->image, source),
            "a foreign bitmap must survive corrupt PNG and later clipboard replacement");
}

void tiffSnapshotSurvivesReplacement() {
    const auto source = imageFixture();
    const auto image = source.toCGImage();
    auto* representation = [[[NSBitmapImageRep alloc] initWithCGImage:image] autorelease];
    CGImageRelease(image);
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    [board clearContents];
    require([board setData:representation.TIFFRepresentation forType:NSPasteboardTypeTIFF],
            "write an independent TIFF-only clipboard fixture");
    auto* clipboard = QApplication::clipboard();
    auto snapshot = ScreenshotClipboardContentReader::snapshotImage(clipboard, 1.0);
    require(snapshot && !snapshot->nativeTiff.isEmpty() && snapshot->detachedImage.isNull(),
            "a TIFF-only snapshot must capture the original bytes");
    const auto tooSmall = [](qint64 bytes) { return bytes <= 32; };
    require(!ScreenshotClipboardContentReader::snapshotImage(clipboard, 1.0, tooSmall),
            "a native TIFF snapshot must respect the caller's byte budget");
    clipboard->clear();
    auto result = ScreenshotClipboardContentReader::decode(std::move(*snapshot));
    require(result && hasSamePixels(result->image, source),
            "a TIFF-only snapshot must preserve pixels after clipboard replacement");
}

void publicationReleasesEncodedInputs() {
    QImage source(17, 11, QImage::Format_ARGB32);
    source.fill(QColor(30, 60, 90, 128));
    auto payload = ScreenshotClipboardService::prepareImage(source);
    const QByteArray png = payload.pngBytes();
    require(ScreenshotClipboardService::publish(QApplication::clipboard(), std::move(payload)),
            "native image publication must succeed");
    require(png.isDetached(),
            "the pasteboard must own published bytes without retaining the application's PNG");
    const QImage decoded = QApplication::clipboard()->image();
    require(decoded.isDetached() && hasSamePixels(decoded, source),
            "native readers must own their decoded pixels without a clipboard image cache");
}

void nativeMetadataAndNotifications() {
    const QImage source = imageFixture();
    ScreenshotClipboardPlacement placement;
    placement.placement = {QStringLiteral("display"), QStringLiteral("serial"), QPointF(20, 30),
                           source.size()};
    placement.windowRect = QRect(QPoint(20, 30), source.size());
    placement.rasterSize = source.size();
    placement.displays = {{QStringLiteral("display"), QStringLiteral("serial"),
                           QRect(0, 0, 1920, 1080), QRect(0, 0, 1920, 1080),
                           QRect(0, 0, 1920, 1040), 1.0}};
    ScreenshotClipboardAppearance appearance;
    appearance.rasterSize = source.size();
    require(placement.isValid() && appearance.isValid(), "metadata fixture must be valid");

    auto* clipboard = QApplication::clipboard();
    auto* previous = new QMimeData;
    previous->setImageData(source);
    const QPointer<QMimeData> previousLifetime(previous);
    clipboard->setMimeData(previous);
    QObject observer;
    int imageNotifications = 0;
    int modeNotifications = 0;
    bool completeAtNotification = false;
    QObject::connect(clipboard, &QClipboard::dataChanged, &observer, [&] {
        ++imageNotifications;
        const auto* mime = clipboard->mimeData();
        completeAtNotification = previousLifetime.isNull() &&
                                 readScreenshotClipboardPlacement(mime).has_value() &&
                                 readScreenshotClipboardAppearance(mime).has_value() &&
                                 hasSamePixels(clipboard->image(), source);
    });
    QObject::connect(clipboard, &QClipboard::changed, &observer, [&](QClipboard::Mode mode) {
        if (mode == QClipboard::Clipboard)
            ++modeNotifications;
    });
    require(ScreenshotClipboardService::publish(clipboard, ScreenshotClipboardService::prepareImage(
                                                               source, {}, placement, appearance)),
            "native image and metadata publication must succeed");
    require(imageNotifications == 1 && modeNotifications == 1 && completeAtNotification,
            "native publication must retire Qt's old payload and notify complete content once");
    const auto* mime = clipboard->mimeData();
    const auto readPlacement = readScreenshotClipboardPlacement(mime);
    const auto readAppearance = readScreenshotClipboardAppearance(mime);
    require(readPlacement && readPlacement->placement == placement.placement && readAppearance &&
                readAppearance->rasterSize == source.size(),
            "native publication must preserve screenshot placement and appearance metadata");
}

void foreignReplacementSurvivesShutdown() {
    auto payload = ScreenshotClipboardService::prepareImage(imageFixture());
    const QByteArray png = payload.pngBytes();
    require(payload.isValid(), "external replacement fixture must encode successfully");
    require(ScreenshotClipboardService::publish(QApplication::clipboard(), std::move(payload)),
            "publication before external replacement must succeed");
    require(png.isDetached(),
            "publication must release image payloads before external replacement");
    QProcess foreign;
    foreign.start(QStringLiteral("/usr/bin/pbcopy"));
    require(foreign.waitForStarted(), "foreign clipboard writer must start");
    foreign.write("external clipboard replacement");
    foreign.closeWriteChannel();
    require(foreign.waitForFinished() && foreign.exitCode() == 0,
            "foreign clipboard writer must complete");
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    const NSInteger revision = board.changeCount;
    // Exercise normal Qt shutdown while the last TIFF promise belongs to an
    // obsolete screenshot. Cleanup must neither read nor overwrite the new item.
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    QApplication::exec();
    require(board.changeCount == revision && [[board stringForType:NSPasteboardTypeString]
                                                 isEqualToString:@"external clipboard replacement"],
            "shutdown must preserve another application's replacement clipboard");
}

void promisedImageSurvivesPublisherExit() {
    QProcess publisher;
    publisher.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--publish-and-exit"), QStringLiteral("-platform"),
                     QStringLiteral("cocoa")});
    require(publisher.waitForFinished(10000) && publisher.exitCode() == 0,
            "image publisher must complete orderly shutdown");
    NSPasteboard* board = NSPasteboard.generalPasteboard;
    NSData* tiff = [board dataForType:NSPasteboardTypeTIFF];
    require(tiff.length > 0 && hasSamePixels(QApplication::clipboard()->image(), imageFixture()),
            "PNG and promised TIFF must remain pasteable after the publishing process exits");
}

void nativeImageFormatsRoundTrip() {
    const QImage source = imageFixture();
    QObject receiver;
    QTemporaryDir directory;
    require(directory.isValid(), "clipboard file fixture must be available");
    const QString path = directory.filePath(QStringLiteral("Snow Shot \u56fe\u50cf.png"));
    require(source.save(path, "PNG"), "clipboard file fixture must be saved");
    for (int cycle = 0; cycle < 24; ++cycle) {
        QEventLoop loop;
        bool completed = false;
        const bool includeFile = cycle % 2 == 0;
        auto payload = ScreenshotClipboardService::prepareImage(source);
        if (includeFile)
            payload = ScreenshotClipboardService::withFile(std::move(payload), path);
        const auto handle = ScreenshotClipboardService::commit(
            QApplication::clipboard(), &receiver, std::move(payload),
            [&](ScreenshotClipboardCommitResult result) {
                require(result.succeeded(), "native image publication must succeed");
                completed = true;
                loop.quit();
            });
        require(handle.isValid(), "native clipboard commit must be scheduled");
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        require(completed && handle.isFinished(), "native clipboard commit timed out");
        NSPasteboard* board = [NSPasteboard generalPasteboard];
        require([board.types containsObject:NSPasteboardTypePNG] &&
                    [board.types containsObject:NSPasteboardTypeTIFF],
                "native clipboard must retain both PNG and TIFF compatibility");
        require(bool([board.types containsObject:NSPasteboardTypeFileURL]) == includeFile,
                "native file references must accompany images only when requested");
        if (includeFile) {
            const auto url = QUrl::fromLocalFile(path);
            require(QUrl(QString::fromNSString([board stringForType:NSPasteboardTypeFileURL])) ==
                            url &&
                        QApplication::clipboard()->mimeData()->urls() == QList<QUrl>{url},
                    "native and Qt readers must preserve the same Unicode file URL");
        }
        const NSData* png = [board dataForType:NSPasteboardTypePNG];
        require(QImage::fromData(static_cast<const uchar*>(png.bytes), static_cast<int>(png.length),
                                 "PNG")
                        .colorSpace() == QColorSpace(QColorSpace::SRgb),
                "native PNG must declare the screenshot's sRGB color space");
        require(hasSamePixels(QImage::fromData(static_cast<const uchar*>(png.bytes),
                                               static_cast<int>(png.length), "PNG"),
                              source),
                "native PNG must preserve screenshot pixels and transparency");
        NSData* tiff = [board dataForType:NSPasteboardTypeTIFF];
        require(tiff.length > 0, "native consumers must receive the promised TIFF bytes");
        CGImageSourceRef decoder =
            CGImageSourceCreateWithData(reinterpret_cast<CFDataRef>(tiff), nullptr);
        CGImageRef image = decoder ? CGImageSourceCreateImageAtIndex(decoder, 0, nullptr) : nullptr;
        require(image && CGImageGetWidth(image) == 17 && CGImageGetHeight(image) == 11,
                "promised TIFF must decode with the original pixel dimensions");
        CGImageRelease(image);
        CFRelease(decoder);
        const QImage local = QApplication::clipboard()->image();
        require(hasSamePixels(local, source),
                "local Qt image readers must retain the original screenshot pixels");

        // Read a foreign TIFF-only pasteboard through the same converter.
        [tiff retain];
        [board clearContents];
        [board setData:tiff forType:NSPasteboardTypeTIFF];
        QCoreApplication::processEvents();
        const QImage foreign = QApplication::clipboard()->image();
        require(hasSamePixels(foreign, source),
                "foreign TIFF images must retain dimensions, color, orientation, and alpha");
        [tiff release];
        QApplication::clipboard()->clear();
    }
    QApplication::clipboard()->setText(QStringLiteral("native text conversion"));
    require([[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString] &&
                QApplication::clipboard()->text() == QStringLiteral("native text conversion"),
            "the image converter must preserve ordinary native clipboard converters");
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        if (app.arguments().contains(QStringLiteral("--read-foreign-png"))) {
            readForeignPngBeforePublication();
            return EXIT_SUCCESS;
        }
        if (app.arguments().contains(QStringLiteral("--publish-and-exit"))) {
            require(ScreenshotClipboardService::publishImage(app.clipboard(), imageFixture()),
                    "child image publication must succeed");
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
            return app.exec();
        }
        PasteboardRestore restore;
        if (app.arguments().contains(QStringLiteral("--html-only"))) {
            nativeHtmlFormatsRoundTrip();
            return EXIT_SUCCESS;
        }
        nativeHtmlFormatsRoundTrip();
        readForeignPngInFreshProcess();
        finderFileIconDoesNotMaskGif();
        snapshotSkipsDerivedBitmapAndRetainsForeignFallback();
        tiffSnapshotSurvivesReplacement();
        publicationReleasesEncodedInputs();
        nativeMetadataAndNotifications();
        nativeImageFormatsRoundTrip();
        promisedImageSurvivesPublisherExit();
        foreignReplacementSurvivesShutdown();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
