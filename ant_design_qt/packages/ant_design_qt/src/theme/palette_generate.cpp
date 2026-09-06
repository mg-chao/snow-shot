#include "palette_generate.h"

#include "fast_color_lite.h"

#include <cmath>

namespace adqt::theme {

namespace {

constexpr int kHueStep = 2;
constexpr double kSaturationStep = 0.16;
constexpr double kSaturationStep2 = 0.05;
constexpr double kBrightnessStep1 = 0.05;
constexpr double kBrightnessStep2 = 0.15;
constexpr int kLightColorCount = 5;
constexpr int kDarkColorCount = 4;

struct DarkColorMapEntry {
  int index;
  int amount;
};

const DarkColorMapEntry kDarkColorMap[] = {
    {7, 15}, {6, 25}, {5, 30}, {5, 45}, {5, 65}, {5, 85}, {4, 90}, {3, 95}, {2, 97}, {1, 98},
};

double clamp(double value, double minValue, double maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

double getHue(const HsvColor& hsv, int i, bool light) {
  double hue;
  if (std::round(hsv.h) >= 60.0 && std::round(hsv.h) <= 240.0) {
    hue = light ? std::round(hsv.h) - kHueStep * i : std::round(hsv.h) + kHueStep * i;
  } else {
    hue = light ? std::round(hsv.h) + kHueStep * i : std::round(hsv.h) - kHueStep * i;
  }

  if (hue < 0.0) {
    hue += 360.0;
  } else if (hue >= 360.0) {
    hue -= 360.0;
  }

  return hue;
}

double getSaturation(const HsvColor& hsv, int i, bool light) {
  if (hsv.h == 0.0 && hsv.s == 0.0) {
    return hsv.s;
  }

  double saturation;
  if (light) {
    saturation = hsv.s - (kSaturationStep * i);
  } else if (i == kDarkColorCount) {
    saturation = hsv.s + kSaturationStep;
  } else {
    saturation = hsv.s + (kSaturationStep2 * i);
  }

  if (saturation > 1.0) {
    saturation = 1.0;
  }

  if (light && i == kLightColorCount && saturation > 0.1) {
    saturation = 0.1;
  }

  if (saturation < 0.06) {
    saturation = 0.06;
  }

  return std::round(saturation * 100.0) / 100.0;
}

double getValue(const HsvColor& hsv, int i, bool light) {
  double value;
  if (light) {
    value = hsv.v + (kBrightnessStep1 * i);
  } else {
    value = hsv.v - (kBrightnessStep2 * i);
  }

  value = clamp(value, 0.0, 1.0);
  return std::round(value * 100.0) / 100.0;
}

}  // namespace

QVector<QString> generatePalette(const QString& color, bool darkTheme,
                                 const QString& backgroundColor) {
  FastColorLite primary(color);
  if (!primary.isValid()) {
    primary = FastColorLite("#1677ff");
  }

  const HsvColor hsv = primary.toHsv();

  QVector<FastColorLite> patterns;
  patterns.reserve(10);

  for (int i = kLightColorCount; i > 0; --i) {
    const HsvColor item{getHue(hsv, i, true), getSaturation(hsv, i, true), getValue(hsv, i, true),
                        hsv.a};
    patterns.push_back(FastColorLite::fromHsv(item));
  }

  patterns.push_back(primary);

  for (int i = 1; i <= kDarkColorCount; ++i) {
    const HsvColor item{getHue(hsv, i, false), getSaturation(hsv, i, false),
                        getValue(hsv, i, false), hsv.a};
    patterns.push_back(FastColorLite::fromHsv(item));
  }

  QVector<QString> results;
  results.reserve(10);

  if (darkTheme) {
    FastColorLite bg(backgroundColor.isEmpty() ? "#141414" : backgroundColor);
    if (!bg.isValid()) {
      bg = FastColorLite("#141414");
    }

    for (const DarkColorMapEntry& entry : kDarkColorMap) {
      results.push_back(bg.mix(patterns.at(entry.index), entry.amount).toHexString());
    }
    return results;
  }

  for (const FastColorLite& item : patterns) {
    results.push_back(item.toHexString());
  }

  return results;
}

}  // namespace adqt::theme
