#pragma once

#include "snow/image/export.h"
#include "snow/image/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace snow::image {

class SNOW_IMAGE_API ByteSource {
  public:
    virtual ~ByteSource() = default;
    [[nodiscard]] virtual Result<std::uint64_t> size() const = 0;
    [[nodiscard]] virtual Result<std::size_t> read_at(std::uint64_t offset,
                                                      std::span<std::byte> destination) const = 0;
};

class SNOW_IMAGE_API ByteSink {
  public:
    virtual ~ByteSink() = default;
    [[nodiscard]] virtual Result<void> write(std::span<const std::byte> source) = 0;
    [[nodiscard]] virtual Result<std::uint64_t> position() const = 0;
    [[nodiscard]] virtual Result<void> seek(std::uint64_t position) = 0;
    [[nodiscard]] virtual Result<void> flush() = 0;
    [[nodiscard]] virtual bool seekable() const noexcept = 0;
};

struct Input final {
    std::shared_ptr<const ByteSource> source;
    std::string name_hint;
};

struct Output final {
    std::shared_ptr<ByteSink> sink;
    std::string name_hint;
};

SNOW_IMAGE_API Result<Input> file_input(const std::filesystem::path& path);
SNOW_IMAGE_API Result<Output> file_output(const std::filesystem::path& path);
SNOW_IMAGE_API Input memory_input(std::shared_ptr<const std::vector<std::byte>> bytes,
                                  std::string name_hint = {});
SNOW_IMAGE_API Input memory_input(std::span<const std::byte> bytes, std::string name_hint = {});
// Memory outputs are single-writer sinks. The capacity hint is applied before
// encoding and seek support is retained for container codecs.
SNOW_IMAGE_API Output memory_output(std::shared_ptr<std::vector<std::byte>> bytes,
                                    std::string name_hint = {}, std::size_t initial_capacity = 0);
SNOW_IMAGE_API Result<std::vector<std::byte>> read_all(const ByteSource& source,
                                                       std::uint64_t maximum_bytes);

} // namespace snow::image
