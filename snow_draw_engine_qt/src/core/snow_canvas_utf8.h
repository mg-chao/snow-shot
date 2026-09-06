#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace snow_canvas_utf8 {

QString stringFromField(const char* utf8, std::uint32_t length, std::uint32_t capacity);
int prefixLengthAtCodePointBoundary(const QByteArray& utf8, int capacity);
void copyStringToField(const QString& text, char* target, std::uint32_t& length,
                       std::uint8_t& truncated, std::uint32_t capacity);
bool fieldsEqual(const char* lhs, std::uint32_t lhsLength, const char* rhs, std::uint32_t rhsLength,
                 std::uint32_t capacity);

} // namespace snow_canvas_utf8
