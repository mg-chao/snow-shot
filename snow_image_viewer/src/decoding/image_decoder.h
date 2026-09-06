#pragma once

#include "core/image_types.h"

#include <QByteArray>
#include <QString>

#include <stop_token>

namespace snow::image_viewer {

class DecodeCancellation final {
  public:
    void cancel() noexcept {
        stopSource_.request_stop();
    }
    bool isCancelled() const noexcept {
        return stopSource_.stop_requested();
    }
    std::stop_token token() const noexcept {
        return stopSource_.get_token();
    }

  private:
    std::stop_source stopSource_;
};

class ImageDecoder {
  public:
    virtual ~ImageDecoder() = default;
    virtual bool canDecode(const QString& filePath, const QByteArray& header) const = 0;
    virtual DecodeResult decode(const QString& filePath,
                                const DecodeCancellation& cancellation) const = 0;
};

} // namespace snow::image_viewer
