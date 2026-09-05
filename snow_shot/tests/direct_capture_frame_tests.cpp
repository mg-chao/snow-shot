#include "captureframeimage.h"

#include <array>
#include <cstdlib>
#include <iostream>

struct SnowCaptureFrameLeaseImpl {
    std::array<uint8_t, 16> pixels{};
};

namespace {
int releases = 0;
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace

extern "C" void snow_capture_frame_lease_release(SnowCaptureFrameLease* lease) {
    ++releases;
    delete lease;
}

int main() {
    using namespace snow_shot::presentation::capture;
    auto* lease = new SnowCaptureFrameLease;
    lease->pixels.fill(123);
    auto image = imageFromFrameLease(lease, lease->pixels.data(), 16, 2, 2, 8,
                                     SNOW_CAPTURE_PIXEL_FORMAT_BGRA8);
    require(!image.isNull() && image.format() == QImage::Format_RGB32 &&
                image.pixelColor(1, 1).red() == 123,
            "native image pixels were not wrapped correctly");
    auto retained = image;
    image = {};
    require(releases == 0 && retained.pixelColor(0, 0).green() == 123,
            "image copy lost its native lease");
    retained = {};
    require(releases == 1, "final image release did not release the native lease exactly once");
    for (const auto width : {0u, 3u, 0xffffffffu}) {
        auto* invalid = new SnowCaptureFrameLease;
        require(imageFromFrameLease(invalid, invalid->pixels.data(), 16, width, 2, 8, 0).isNull(),
                "invalid native dimensions were accepted");
    }
    require(releases == 4, "invalid image leaked a native lease");
    SnowCaptureFrameInfo info{};
    require(!validFrameInfo(info), "empty frame info was accepted");
    return 0;
}
