#include "snow_canvas_utf8.h"

#include <cstring>

namespace snow_canvas_utf8 {
namespace {

std::uint32_t boundedLength(std::uint32_t length, std::uint32_t capacity) {
    return qMin<std::uint32_t>(length, capacity);
}

bool isUtf8ContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & 0xc0) == 0x80;
}

} // namespace

QString stringFromField(const char* utf8, std::uint32_t length, std::uint32_t capacity) {
    if (utf8 == nullptr) {
        return {};
    }
    return QString::fromUtf8(utf8, static_cast<int>(boundedLength(length, capacity)));
}

int prefixLengthAtCodePointBoundary(const QByteArray& utf8, int capacity) {
    qsizetype length = qBound<qsizetype>(0, static_cast<qsizetype>(capacity), utf8.size());
    while (length > 0 && length < utf8.size() && isUtf8ContinuationByte(utf8.at(length))) {
        --length;
    }
    return static_cast<int>(length);
}

void copyStringToField(const QString& text, char* target, std::uint32_t& length,
                       std::uint8_t& truncated, std::uint32_t capacity) {
    if (target == nullptr) {
        length = 0;
        truncated = text.isEmpty() ? 0 : 1;
        return;
    }

    std::memset(target, 0, static_cast<std::size_t>(capacity));
    const QByteArray utf8 = text.toUtf8();
    const int copyLength = prefixLengthAtCodePointBoundary(utf8, static_cast<int>(capacity));
    if (copyLength > 0) {
        std::memcpy(target, utf8.constData(), static_cast<std::size_t>(copyLength));
    }
    length = static_cast<std::uint32_t>(copyLength);
    truncated = utf8.size() > copyLength ? 1 : 0;
}

bool fieldsEqual(const char* lhs, std::uint32_t lhsLength, const char* rhs, std::uint32_t rhsLength,
                 std::uint32_t capacity) {
    const std::uint32_t boundedLhsLength = boundedLength(lhsLength, capacity);
    const std::uint32_t boundedRhsLength = boundedLength(rhsLength, capacity);
    if (boundedLhsLength != boundedRhsLength) {
        return false;
    }
    if (boundedLhsLength == 0) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return std::memcmp(lhs, rhs, static_cast<std::size_t>(boundedLhsLength)) == 0;
}

} // namespace snow_canvas_utf8
