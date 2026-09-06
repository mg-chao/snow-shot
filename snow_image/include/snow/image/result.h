#pragma once

#include "snow/image/export.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace snow::image {

enum class ErrorCode : std::uint8_t {
    ok = 0,
    cancelled,
    invalid_argument,
    io_error,
    truncated_data,
    corrupt_data,
    unsupported_format,
    unsupported_feature,
    codec_unavailable,
    limit_exceeded,
    out_of_memory,
    decode_failed,
    encode_failed,
    internal_error,
};

struct SNOW_IMAGE_API Status final {
    ErrorCode code = ErrorCode::ok;
    std::string message;
    std::string codec;
    std::int64_t native_code = 0;
    std::optional<std::uint64_t> byte_offset;

    [[nodiscard]] bool ok() const noexcept {
        return code == ErrorCode::ok;
    }
    explicit operator bool() const noexcept {
        return ok();
    }

    static Status success() {
        return {};
    }
    static Status error(ErrorCode error_code, std::string error_message,
                        std::string codec_name = {}, std::int64_t backend_code = 0) {
        return {error_code, std::move(error_message), std::move(codec_name), backend_code,
                std::nullopt};
    }
};

template <typename T> class [[nodiscard]] Result final {
  public:
    Result(T value) : value_(std::move(value)) {}
    Result(Status error) : error_(std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept {
        return value_.has_value();
    }
    explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T& value() & {
        return value_.value();
    }
    [[nodiscard]] const T& value() const& {
        return value_.value();
    }
    [[nodiscard]] T&& value() && {
        return std::move(value_).value();
    }
    [[nodiscard]] const Status& error() const& {
        return error_;
    }
    [[nodiscard]] Status&& error() && {
        return std::move(error_);
    }

  private:
    std::optional<T> value_;
    Status error_ = Status::success();
};

template <> class [[nodiscard]] Result<void> final {
  public:
    Result() = default;
    Result(Status error) : error_(std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept {
        return error_.ok();
    }
    explicit operator bool() const noexcept {
        return has_value();
    }
    [[nodiscard]] const Status& error() const& {
        return error_;
    }
    [[nodiscard]] Status&& error() && {
        return std::move(error_);
    }

  private:
    Status error_ = Status::success();
};

SNOW_IMAGE_API std::string_view error_code_name(ErrorCode code) noexcept;

} // namespace snow::image
