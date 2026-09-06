#include "planar_raster_io.h"

#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace snow::image::internal {
namespace {

Status plane_error(ErrorCode code, const char* message, std::string_view codec) {
    return Status::error(code, message, std::string(codec));
}

Result<std::size_t> checked_row_bytes(const PlaneDescriptor& plane, std::string_view codec) {
    Result<std::size_t> row_bytes = plane.row_bytes();
    if (!row_bytes)
        return row_bytes.error();
    if (row_bytes.value() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return plane_error(ErrorCode::limit_exceeded, "Planar row stride exceeds codec limits.",
                           codec);
    return row_bytes;
}

Result<std::size_t> checked_plane_bytes(std::size_t stride, std::uint32_t height,
                                        std::string_view codec) {
    if (height != 0 && stride > std::numeric_limits<std::size_t>::max() / height)
        return plane_error(ErrorCode::limit_exceeded,
                           "Planar storage size overflows the address space.", codec);
    return stride * height;
}

} // namespace

Result<WritablePlaneSet> prepare_writable_planes(RasterWriter& writer, std::uint32_t frame_index,
                                                 const RasterFrameDescriptor& frame,
                                                 const DecodeLimits& limits,
                                                 std::string_view codec) {
    const std::size_t count = frame.layout.planes.size();
    if (count == 0)
        return plane_error(ErrorCode::unsupported_feature, "Native output has no planes.", codec);
    WritablePlaneSet result;
    try {
        result.mappings.resize(count);
        result.owned.resize(count);
        result.pointers.resize(count);
        result.strides.resize(count);
    } catch (const std::bad_alloc&) {
        return plane_error(ErrorCode::out_of_memory, "Could not allocate planar output state.",
                           codec);
    }

    bool mapping_supported = true;
    for (std::size_t index = 0; index < count; ++index) {
        Result<MutableMappedPlane> mapped =
            writer.map_plane_for_write(frame_index, static_cast<std::uint32_t>(index));
        if (!mapped) {
            if (mapped.error().code != ErrorCode::unsupported_feature)
                return mapped.error();
            mapping_supported = false;
            break;
        }
        result.mappings[index] = std::move(mapped).value();
    }
    if (!mapping_supported) {
        for (MutableMappedPlane& mapping : result.mappings)
            mapping = {};
    }
    result.mapped = mapping_supported;

    std::uint64_t owned_bytes = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const PlaneDescriptor& plane = frame.layout.planes[index];
        Result<std::size_t> row_bytes = checked_row_bytes(plane, codec);
        if (!row_bytes)
            return row_bytes.error();
        if (result.mapped) {
            MutableMappedPlane& mapping = result.mappings[index];
            Result<std::size_t> required =
                checked_plane_bytes(mapping.row_stride, plane.height, codec);
            if (!required)
                return required.error();
            if (mapping.row_stride < row_bytes.value() ||
                mapping.row_stride > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                mapping.pixels.size() < required.value())
                return plane_error(ErrorCode::invalid_argument,
                                   "Writable plane mapping is invalid.", codec);
            result.pointers[index] = reinterpret_cast<unsigned char*>(mapping.pixels.data());
            result.strides[index] = static_cast<int>(mapping.row_stride);
            continue;
        }
        Result<std::size_t> plane_bytes =
            checked_plane_bytes(row_bytes.value(), plane.height, codec);
        if (!plane_bytes)
            return plane_bytes.error();
        const std::uint64_t bytes = plane_bytes.value();
        if (owned_bytes > limits.maximum_working_bytes ||
            bytes > limits.maximum_working_bytes - owned_bytes)
            return plane_error(ErrorCode::limit_exceeded,
                               "Planar output exceeds the decode working-memory limit.", codec);
        owned_bytes += bytes;
        try {
            result.owned[index].resize(plane_bytes.value());
        } catch (const std::bad_alloc&) {
            return plane_error(ErrorCode::out_of_memory, "Could not allocate planar output.",
                               codec);
        }
        result.pointers[index] = reinterpret_cast<unsigned char*>(result.owned[index].data());
        result.strides[index] = static_cast<int>(row_bytes.value());
    }
    return result;
}

Result<void> publish_writable_planes(RasterWriter& writer, std::uint32_t frame_index,
                                     const RasterFrameDescriptor& frame, WritablePlaneSet& planes,
                                     std::stop_token stop) {
    if (planes.pointers.size() != frame.layout.planes.size())
        return Status::error(ErrorCode::invalid_argument,
                             "Prepared plane count does not match its frame.");
    for (std::size_t index = 0; index < planes.pointers.size(); ++index) {
        Result<void> status;
        if (planes.mapped) {
            status = writer.finish_mapped_plane(frame_index, static_cast<std::uint32_t>(index),
                                                planes.mappings[index]);
        } else {
            status = writer.write_rows(frame_index, static_cast<std::uint32_t>(index), 0,
                                       frame.layout.planes[index].height,
                                       static_cast<std::size_t>(planes.strides[index]),
                                       planes.owned[index], stop);
        }
        if (!status)
            return status;
    }
    return writer.commit();
}

Result<void> publish_readable_plane_views(RasterWriter& writer, std::uint32_t frame_index,
                                          const RasterFrameDescriptor& frame,
                                          std::span<const ReadablePlaneView> planes,
                                          std::stop_token stop, bool commit,
                                          std::string_view codec) {
    if (planes.size() != frame.layout.planes.size())
        return plane_error(ErrorCode::invalid_argument,
                           "Decoded plane count does not match its frame.", codec);
    std::vector<MutableMappedPlane> mappings;
    try {
        mappings.resize(planes.size());
    } catch (const std::bad_alloc&) {
        return plane_error(ErrorCode::out_of_memory,
                           "Could not allocate decoded plane publication state.", codec);
    }
    bool mapped = true;
    for (std::size_t index = 0; index < planes.size(); ++index) {
        Result<MutableMappedPlane> mapping =
            writer.map_plane_for_write(frame_index, static_cast<std::uint32_t>(index));
        if (!mapping) {
            if (mapping.error().code != ErrorCode::unsupported_feature)
                return mapping.error();
            mapped = false;
            break;
        }
        mappings[index] = std::move(mapping).value();
    }
    if (!mapped) {
        for (MutableMappedPlane& mapping : mappings)
            mapping = {};
    }
    for (std::size_t index = 0; index < planes.size(); ++index) {
        if (stop.stop_requested())
            return plane_error(ErrorCode::cancelled, "Decoded plane publication was cancelled.",
                               codec);
        const PlaneDescriptor& descriptor = frame.layout.planes[index];
        Result<std::size_t> row_bytes = checked_row_bytes(descriptor, codec);
        if (!row_bytes)
            return row_bytes.error();
        Result<std::size_t> source_bytes =
            checked_plane_bytes(planes[index].row_stride, descriptor.height, codec);
        if (!source_bytes)
            return source_bytes.error();
        if (planes[index].row_stride < row_bytes.value() ||
            planes[index].pixels.size() < source_bytes.value())
            return plane_error(ErrorCode::invalid_argument, "Decoded plane view is invalid.",
                               codec);
        if (!mapped) {
            Result<void> written = writer.write_rows(frame_index, static_cast<std::uint32_t>(index),
                                                     0, descriptor.height, planes[index].row_stride,
                                                     planes[index].pixels, stop);
            if (!written)
                return written;
            continue;
        }
        Result<std::size_t> destination_bytes =
            checked_plane_bytes(mappings[index].row_stride, descriptor.height, codec);
        if (!destination_bytes)
            return destination_bytes.error();
        if (mappings[index].row_stride < row_bytes.value() ||
            mappings[index].pixels.size() < destination_bytes.value())
            return plane_error(ErrorCode::invalid_argument, "Writable plane mapping is invalid.",
                               codec);
        for (std::uint32_t row = 0; row < descriptor.height; ++row) {
            std::memcpy(mappings[index].pixels.data() +
                            static_cast<std::size_t>(row) * mappings[index].row_stride,
                        planes[index].pixels.data() +
                            static_cast<std::size_t>(row) * planes[index].row_stride,
                        row_bytes.value());
        }
        Result<void> finished = writer.finish_mapped_plane(
            frame_index, static_cast<std::uint32_t>(index), mappings[index]);
        if (!finished)
            return finished;
    }
    return commit ? writer.commit() : Result<void>{};
}

Result<ReadablePlaneSet>
prepare_readable_planes(const RasterSource& source, std::uint32_t frame_index,
                        const RasterFrameDescriptor& frame, std::stop_token stop,
                        std::uint64_t maximum_owned_bytes, std::string_view codec) {
    const std::size_t count = frame.layout.planes.size();
    if (count == 0)
        return plane_error(ErrorCode::unsupported_feature, "Native input has no planes.", codec);
    ReadablePlaneSet result;
    try {
        result.mappings.resize(count);
        result.owned.resize(count);
        result.pointers.resize(count);
        result.strides.resize(count);
    } catch (const std::bad_alloc&) {
        return plane_error(ErrorCode::out_of_memory, "Could not allocate planar input state.",
                           codec);
    }
    const bool mapped = has_access(source.access(), RasterAccess::mapped_planes);
    std::uint64_t owned_bytes = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const PlaneDescriptor& plane = frame.layout.planes[index];
        Result<std::size_t> row_bytes = checked_row_bytes(plane, codec);
        if (!row_bytes)
            return row_bytes.error();
        if (mapped) {
            Result<MappedPlane> plane_mapping =
                source.map_plane(frame_index, static_cast<std::uint32_t>(index));
            if (!plane_mapping)
                return plane_mapping.error();
            result.mappings[index] = std::move(plane_mapping).value();
            const MappedPlane& mapping = result.mappings[index];
            Result<std::size_t> required =
                checked_plane_bytes(mapping.row_stride, plane.height, codec);
            if (!required)
                return required.error();
            if (mapping.row_stride < row_bytes.value() ||
                mapping.row_stride > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                mapping.pixels.size() < required.value())
                return plane_error(ErrorCode::invalid_argument,
                                   "Readable plane mapping is invalid.", codec);
            result.pointers[index] = reinterpret_cast<const unsigned char*>(mapping.pixels.data());
            result.strides[index] = static_cast<int>(mapping.row_stride);
            continue;
        }
        Result<std::size_t> plane_bytes =
            checked_plane_bytes(row_bytes.value(), plane.height, codec);
        if (!plane_bytes)
            return plane_bytes.error();
        const std::uint64_t bytes = plane_bytes.value();
        if (owned_bytes > maximum_owned_bytes || bytes > maximum_owned_bytes - owned_bytes)
            return plane_error(ErrorCode::limit_exceeded,
                               "Planar input exceeds its materialization limit.", codec);
        owned_bytes += bytes;
        try {
            result.owned[index].resize(plane_bytes.value());
        } catch (const std::bad_alloc&) {
            return plane_error(ErrorCode::out_of_memory, "Could not allocate planar input.", codec);
        }
        Result<void> read =
            source.read_rows(frame_index, static_cast<std::uint32_t>(index), 0, plane.height,
                             row_bytes.value(), result.owned[index], stop);
        if (!read)
            return read.error();
        result.pointers[index] = reinterpret_cast<const unsigned char*>(result.owned[index].data());
        result.strides[index] = static_cast<int>(row_bytes.value());
    }
    return result;
}

} // namespace snow::image::internal
