#pragma once

#include "../button.h"

namespace adqt::widgets::detail {

enum class SegmentPosition : unsigned char {
  Standalone,
  Leading,
  Middle,
  Trailing,
};

void setButtonSegmentPosition(AdButton* button, SegmentPosition value);
SegmentPosition buttonSegmentPosition(const AdButton* button);

}  // namespace adqt::widgets::detail
