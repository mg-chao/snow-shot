#ifndef SNOW_SHOT_PLATFORM_MACOS_IMAGECLIPBOARD_H
#define SNOW_SHOT_PLATFORM_MACOS_IMAGECLIPBOARD_H

#include <QStringList>
#include <QtTypes>
#include <functional>
#include <optional>

class QByteArray;
class QClipboard;
class QImage;
class QMimeData;
class QString;

namespace snow_shot::platform::macos {
void initializeImageClipboardConverter();
// Cocoa only. The pasteboard owns encoded data; the app retains no image payload.
[[nodiscard]] std::optional<quint64>
publishImageClipboard(QClipboard* clipboard, const QByteArray& png, const QByteArray& placement,
                      const QByteArray& appearance, const QString& filePath);
// True only for this process's current successful publication: its TIFF is
// derived from its PNG, so a reader retaining PNG needs no bitmap fallback.
// Callers must check the clipboard revision around the complete snapshot.
[[nodiscard]] bool imageClipboardBitmapIsDerivedFromPng();
// A single native PNG item has no independent bitmap to snapshot.
[[nodiscard]] bool imageClipboardHasOnlyPng();
[[nodiscard]] QByteArray copyTiffOnlyClipboardItem(qint64 maximumBytes,
                                                   const std::function<bool(qint64)>& reserve);
// The reservation covers the conversion's working storage. The caller owns and accounts
// for the encoded input, which remains alive throughout the decode.
[[nodiscard]] QImage
decodeNativeClipboardImage(const QByteArray& encoded, qint64 maximumPixels = 0,
                           const std::function<bool(qint64)>& reserveWorkingBytes = {});
[[nodiscard]] QStringList clipboardFileIconMimeTypes(const QMimeData& mimeData);
} // namespace snow_shot::platform::macos

#endif
