#include <snow/image/service.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

double median_milliseconds(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2U;
    return samples.size() % 2U == 0 ? (samples[middle - 1U] + samples[middle]) * 0.5
                                    : samples[middle];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: snow_image_benchmark <image> [iterations]\n";
        return 2;
    }
    const int iterations = argc > 2 ? std::max(1, std::atoi(argv[2])) : 3;
    snow::image::Result<snow::image::Input> input =
        snow::image::file_input(std::filesystem::path(argv[1]));
    if (!input) {
        std::cerr << input.error().message << '\n';
        return 1;
    }
    snow::image::Service service;
    snow::image::Result<snow::image::Document> source = service.decode(input.value());
    if (!source) {
        std::cerr << source.error().message << '\n';
        return 1;
    }
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t decodedBytes = 0;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        snow::image::Result<snow::image::Document> document = service.decode(input.value());
        if (!document) {
            std::cerr << document.error().message << '\n';
            return 1;
        }
        for (const snow::image::Frame& frame : document.value().frames) {
            decodedBytes += frame.image.pixels().size();
        }
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    const double mebibytes = static_cast<double>(decodedBytes) / (1024.0 * 1024.0);
    std::cout << iterations << " decode(s), " << elapsed.count() << " s, "
              << mebibytes / elapsed.count() << " MiB/s\n";

    if (source.value().format == snow::image::Format::jpeg) {
        for (const auto [name, layout] :
             {std::pair{"packed", snow::image::RasterLayoutPolicy::packed},
              std::pair{"native", snow::image::RasterLayoutPolicy::native}}) {
            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(iterations));
            std::uint64_t storeBytes = 0;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                const std::filesystem::path path =
                    std::filesystem::temp_directory_path() /
                    ("snow-image-benchmark-" + std::string(name) + "-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ".srs");
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
                snow::image::DecodeOptions options;
                options.raster_layout = layout;
                const auto decodeStarted = std::chrono::steady_clock::now();
                auto store = service.decode_to_store(input.value(), path, options);
                const auto decodeElapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - decodeStarted);
                if (!store) {
                    std::cerr << store.error().message << '\n';
                    return 1;
                }
                samples.push_back(decodeElapsed.count());
                storeBytes = store.value()->file_bytes();
                store.value().reset();
                std::filesystem::remove(path, ignored);
            }
            std::cout << "JPEG raster decode: layout=" << name << ", iterations=" << iterations
                      << ", median=" << std::fixed << std::setprecision(3)
                      << median_milliseconds(samples) << " ms, store-bytes=" << storeBytes << '\n';
        }
    }

    if (service.encoder_info(snow::image::Format::png)) {
        const std::string_view backend =
            snow::image::compression_backend_version(snow::image::Format::png);
        std::cout << "PNG compression backend: " << backend << '\n';
        for (const int compression : {1, 6, 9}) {
            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(iterations));
            std::size_t outputBytes = 0;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                auto encoded = std::make_shared<std::vector<std::byte>>();
                snow::image::EncodeOptions options;
                options.format = snow::image::Format::png;
                options.compression_level = compression;
                const auto encodeStarted = std::chrono::steady_clock::now();
                snow::image::Result<snow::image::EncodeResult> status =
                    service.encode(source.value(), snow::image::memory_output(encoded), options);
                const auto encodeElapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - encodeStarted);
                if (!status) {
                    std::cerr << status.error().message << '\n';
                    return 1;
                }
                samples.push_back(encodeElapsed.count());
                outputBytes = encoded->size();
            }
            std::cout << "PNG encode: compression=" << compression
                      << ", filter=libpng-adaptive, zlib=" << backend
                      << ", iterations=" << iterations << ", median=" << std::fixed
                      << std::setprecision(3) << median_milliseconds(samples)
                      << " ms, bytes=" << outputBytes << '\n';
        }
    }
    return 0;
}
