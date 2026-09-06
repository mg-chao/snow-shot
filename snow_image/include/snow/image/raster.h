#pragma once

#include "snow/image/document.h"
#include "snow/image/export.h"
#include "snow/image/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace snow::image {

enum class ColorModel : std::uint8_t {
    gray,
    rgb,
    ycbcr,
    cmyk,
    indexed,
};

enum class PlaneSemantic : std::uint8_t {
    packed,
    gray,
    red,
    green,
    blue,
    alpha,
    luma,
    chroma_blue,
    chroma_red,
    index,
};

enum class ChromaSubsampling : std::uint8_t {
    none,
    yuv444,
    yuv422,
    yuv420,
    yuv440,
    yuv411,
    yuv441,
};

enum class ColorRange : std::uint8_t { full, limited };

struct PlaneDescriptor final {
    PlaneSemantic semantic = PlaneSemantic::packed;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PixelFormat format;
    std::uint8_t significant_bits = 8;

    [[nodiscard]] Result<std::size_t> row_bytes() const;
    [[nodiscard]] Result<void> validate() const;
    friend bool operator==(const PlaneDescriptor&, const PlaneDescriptor&) = default;
};

struct RasterLayout final {
    ColorModel color_model = ColorModel::rgb;
    AlphaMode alpha = AlphaMode::straight;
    ChromaSubsampling chroma_subsampling = ChromaSubsampling::none;
    ColorRange color_range = ColorRange::full;
    std::vector<PlaneDescriptor> planes;

    [[nodiscard]] Result<void> validate() const;
    friend bool operator==(const RasterLayout&, const RasterLayout&) = default;
};

struct RasterFrameDescriptor final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::chrono::nanoseconds duration{0};
    FrameBlend blend = FrameBlend::source;
    FrameDisposal disposal = FrameDisposal::keep;
    Metadata metadata;
    ColorEncoding color;
    std::optional<std::array<std::uint32_t, 2>> cursor_hotspot;
    RasterLayout layout;

    [[nodiscard]] Result<void> validate(std::uint32_t canvas_width,
                                        std::uint32_t canvas_height) const;
};

enum class DocumentKind : std::uint8_t { raster, vector, multipart, deep };

struct DocumentDescriptor final {
    Format format = Format::unknown;
    DocumentKind kind = DocumentKind::raster;
    std::uint32_t canvas_width = 0;
    std::uint32_t canvas_height = 0;
    std::uint32_t loop_count = 1;
    Metadata metadata;
    ColorEncoding color;
    std::vector<RasterFrameDescriptor> frames;

    [[nodiscard]] Result<void> validate() const;
};

struct RasterRect final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct MutablePlaneView final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PixelFormat format;
    std::size_t row_stride = 0;
    std::span<std::byte> pixels;

    [[nodiscard]] Result<void> validate() const;
};

enum class RasterAccess : std::uint32_t {
    none = 0,
    sequential_rows = 1U << 0U,
    random_rows = 1U << 1U,
    random_regions = 1U << 2U,
    mapped_planes = 1U << 3U,
    concurrent_reads = 1U << 4U,
};

// Analysis is deliberately separate from the pixel descriptor.  A caller can
// retain a verified result across a transport boundary without pretending that
// the descriptor itself proves the contents of an alpha plane.
struct RasterAnalysis final {
    std::optional<AlphaContent> alpha_content;

    friend bool operator==(const RasterAnalysis&, const RasterAnalysis&) = default;
};

constexpr RasterAccess operator|(RasterAccess left, RasterAccess right) noexcept {
    // Bitmask combinations do not require a matching named enumerator.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<RasterAccess>(static_cast<std::uint32_t>(left) |
                                     static_cast<std::uint32_t>(right));
}

constexpr RasterAccess operator&(RasterAccess left, RasterAccess right) noexcept {
    return static_cast<RasterAccess>(static_cast<std::uint32_t>(left) &
                                     static_cast<std::uint32_t>(right));
}

constexpr bool has_access(RasterAccess set, RasterAccess value) noexcept {
    return (set & value) == value;
}

struct MappedPlane final {
    std::shared_ptr<const void> owner;
    std::span<const std::byte> pixels;
    std::size_t row_stride = 0;
};

struct MutableMappedPlane final {
    std::shared_ptr<void> owner;
    std::span<std::byte> pixels;
    std::size_t row_stride = 0;
};

class SNOW_IMAGE_API RasterSource {
  public:
    virtual ~RasterSource() = default;
    [[nodiscard]] virtual const DocumentDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual RasterAccess access() const noexcept = 0;
    [[nodiscard]] virtual Result<void>
    read_rows(std::uint32_t frame_index, std::uint32_t plane_index, std::uint32_t first_row,
              std::uint32_t row_count, std::size_t destination_stride,
              std::span<std::byte> destination, std::stop_token stop = {}) const = 0;
    [[nodiscard]] virtual Result<void> read_region(std::uint32_t frame_index,
                                                   std::uint32_t plane_index, RasterRect region,
                                                   MutablePlaneView destination,
                                                   std::stop_token stop = {}) const;
    [[nodiscard]] virtual Result<MappedPlane> map_plane(std::uint32_t frame_index,
                                                        std::uint32_t plane_index) const;
};

class SNOW_IMAGE_API RasterWriter {
  public:
    virtual ~RasterWriter() = default;
    [[nodiscard]] virtual const DocumentDescriptor& descriptor() const noexcept = 0;
    [[nodiscard]] virtual Result<void>
    write_rows(std::uint32_t frame_index, std::uint32_t plane_index, std::uint32_t first_row,
               std::uint32_t row_count, std::size_t source_stride,
               std::span<const std::byte> source, std::stop_token stop = {}) = 0;
    [[nodiscard]] virtual Result<MutableMappedPlane> map_plane_for_write(std::uint32_t frame_index,
                                                                         std::uint32_t plane_index);
    [[nodiscard]] virtual Result<void> finish_mapped_plane(std::uint32_t frame_index,
                                                           std::uint32_t plane_index,
                                                           const MutableMappedPlane& mapping);
    [[nodiscard]] virtual Result<void> commit() = 0;
    virtual void abort() noexcept = 0;
};

struct RasterStoreLimits final {
    std::uint64_t maximum_manifest_bytes = std::uint64_t{64} << 20U;
    std::uint64_t maximum_pixel_bytes = std::uint64_t{8} << 40U;
    std::uint32_t maximum_frames = 100'000;
    std::uint32_t maximum_planes_per_frame = 16;
    std::uint64_t maximum_chunks = std::uint64_t{16} << 20U;
};

struct RasterStoreOptions final {
    std::uint32_t chunk_rows = 256;
    std::uint32_t row_alignment = 64;
    std::array<std::byte, 16> session_nonce{};
    RasterStoreLimits limits;
    RasterAnalysis analysis;
};

class SNOW_IMAGE_API RasterStore final : public RasterSource,
                                         public RasterWriter,
                                         public std::enable_shared_from_this<RasterStore> {
  public:
    ~RasterStore() override;
    RasterStore(const RasterStore&) = delete;
    RasterStore& operator=(const RasterStore&) = delete;

    [[nodiscard]] static Result<std::shared_ptr<RasterStore>>
    create(const std::filesystem::path& path, DocumentDescriptor descriptor,
           const RasterStoreOptions& options = {});
    [[nodiscard]] static Result<std::shared_ptr<RasterStore>>
    open(const std::filesystem::path& path, const RasterStoreLimits& limits = {});

    [[nodiscard]] const DocumentDescriptor& descriptor() const noexcept override;
    [[nodiscard]] RasterAccess access() const noexcept override;
    [[nodiscard]] Result<void> read_rows(std::uint32_t frame_index, std::uint32_t plane_index,
                                         std::uint32_t first_row, std::uint32_t row_count,
                                         std::size_t destination_stride,
                                         std::span<std::byte> destination,
                                         std::stop_token stop = {}) const override;
    [[nodiscard]] Result<void> read_region(std::uint32_t frame_index, std::uint32_t plane_index,
                                           RasterRect region, MutablePlaneView destination,
                                           std::stop_token stop = {}) const override;
    [[nodiscard]] Result<MappedPlane> map_plane(std::uint32_t frame_index,
                                                std::uint32_t plane_index) const override;
    [[nodiscard]] Result<MutableMappedPlane>
    map_plane_for_write(std::uint32_t frame_index, std::uint32_t plane_index) override;
    [[nodiscard]] Result<void> finish_mapped_plane(std::uint32_t frame_index,
                                                   std::uint32_t plane_index,
                                                   const MutableMappedPlane& mapping) override;
    [[nodiscard]] Result<void> copy_plane(std::uint32_t frame_index, std::uint32_t plane_index,
                                          std::size_t source_stride,
                                          std::span<const std::byte> source,
                                          std::stop_token stop = {});
    [[nodiscard]] Result<void> write_rows(std::uint32_t frame_index, std::uint32_t plane_index,
                                          std::uint32_t first_row, std::uint32_t row_count,
                                          std::size_t source_stride,
                                          std::span<const std::byte> source,
                                          std::stop_token stop = {}) override;
    [[nodiscard]] Result<void> commit() override;
    void abort() noexcept override;

    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::uint64_t file_bytes() const noexcept;
    [[nodiscard]] std::array<std::byte, 16> session_nonce() const noexcept;
    [[nodiscard]] RasterAnalysis analysis() const noexcept;
    [[nodiscard]] Result<void> set_analysis(RasterAnalysis analysis);

  private:
    class Impl;
    explicit RasterStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct RasterBufferStoreOptions final {
    std::uint32_t row_alignment = 64;
    std::array<std::byte, 16> session_nonce{};
    RasterStoreLimits limits;
    RasterAnalysis analysis;
};

// A sealed, self-describing raster package stored directly in caller-owned
// memory.  The caller owns the lifetime of the supplied span; the store owns
// only its descriptor and mapping state.  Pixel bytes are trusted after the
// sealed header and manifest have been validated.
class SNOW_IMAGE_API RasterBufferStore final
    : public RasterSource,
      public RasterWriter,
      public std::enable_shared_from_this<RasterBufferStore> {
  public:
    ~RasterBufferStore() override;
    RasterBufferStore(const RasterBufferStore&) = delete;
    RasterBufferStore& operator=(const RasterBufferStore&) = delete;

    [[nodiscard]] static Result<std::shared_ptr<RasterBufferStore>>
    create(std::span<std::byte> storage, DocumentDescriptor descriptor,
           const RasterBufferStoreOptions& options = {});
    [[nodiscard]] static Result<std::uint64_t>
    required_bytes(DocumentDescriptor descriptor, const RasterBufferStoreOptions& options = {});
    [[nodiscard]] static Result<std::shared_ptr<RasterBufferStore>>
    open(std::span<const std::byte> storage, const RasterStoreLimits& limits = {},
         std::optional<std::array<std::byte, 16>> expected_nonce = {},
         std::optional<std::uint64_t> expected_size = {});

    [[nodiscard]] const DocumentDescriptor& descriptor() const noexcept override;
    [[nodiscard]] RasterAccess access() const noexcept override;
    [[nodiscard]] Result<void> read_rows(std::uint32_t frame_index, std::uint32_t plane_index,
                                         std::uint32_t first_row, std::uint32_t row_count,
                                         std::size_t destination_stride,
                                         std::span<std::byte> destination,
                                         std::stop_token stop = {}) const override;
    [[nodiscard]] Result<void> read_region(std::uint32_t frame_index, std::uint32_t plane_index,
                                           RasterRect region, MutablePlaneView destination,
                                           std::stop_token stop = {}) const override;
    [[nodiscard]] Result<MappedPlane> map_plane(std::uint32_t frame_index,
                                                std::uint32_t plane_index) const override;
    [[nodiscard]] Result<MutableMappedPlane>
    map_plane_for_write(std::uint32_t frame_index, std::uint32_t plane_index) override;
    [[nodiscard]] Result<void> finish_mapped_plane(std::uint32_t frame_index,
                                                   std::uint32_t plane_index,
                                                   const MutableMappedPlane& mapping) override;
    [[nodiscard]] Result<void> write_rows(std::uint32_t frame_index, std::uint32_t plane_index,
                                          std::uint32_t first_row, std::uint32_t row_count,
                                          std::size_t source_stride,
                                          std::span<const std::byte> source,
                                          std::stop_token stop = {}) override;
    [[nodiscard]] Result<void> commit() override;
    void abort() noexcept override;

    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] std::uint64_t byte_size() const noexcept;
    [[nodiscard]] std::array<std::byte, 16> session_nonce() const noexcept;
    [[nodiscard]] RasterAnalysis analysis() const noexcept;

  private:
    class Impl;
    explicit RasterBufferStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] SNOW_IMAGE_API Result<DocumentDescriptor> describe_document(const Document& document);
[[nodiscard]] SNOW_IMAGE_API Result<DocumentDescriptor>
describe_document(const DocumentInfo& document);

} // namespace snow::image
