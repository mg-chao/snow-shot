#include "fast_color_lite.h"

#include <QRegularExpression>

#include <cmath>

namespace adqt::theme {

namespace {

double roundToTwo(double value) { return std::round(value * 100.0) / 100.0; }

}  // namespace

FastColorLite::FastColorLite() : r_(0), g_(0), b_(0), a_(1.0), valid_(true) {}

FastColorLite::FastColorLite(const QString& input) : FastColorLite() {
  const QString trimmed = input.trimmed();
  if (trimmed.isEmpty()) {
    valid_ = false;
    return;
  }

  if (parseHex(trimmed) || parseRgb(trimmed)) {
    valid_ = true;
    return;
  }

  valid_ = false;
}

FastColorLite::FastColorLite(int r, int g, int b, double a)
    : r_(clampChannel(r)),
      g_(clampChannel(g)),
      b_(clampChannel(b)),
      a_(clampUnit(a)),
      valid_(true) {}

FastColorLite FastColorLite::fromHsv(const HsvColor& hsv) {
  double h = std::fmod(hsv.h, 360.0);
  if (h < 0.0) {
    h += 360.0;
  }

  const double s = clampUnit(hsv.s);
  const double v = clampUnit(hsv.v);

  int r = static_cast<int>(std::round(v * 255.0));
  int g = r;
  int b = r;

  if (s > 0.0) {
    const double hh = h / 60.0;
    const int i = static_cast<int>(std::floor(hh));
    const double ff = hh - i;
    const int p = static_cast<int>(std::round(v * (1.0 - s) * 255.0));
    const int q = static_cast<int>(std::round(v * (1.0 - (s * ff)) * 255.0));
    const int t = static_cast<int>(std::round(v * (1.0 - (s * (1.0 - ff))) * 255.0));

    switch (i) {
      case 0:
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        b = p;
        break;
      case 2:
        r = p;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        break;
      case 4:
        r = t;
        g = p;
        break;
      case 5:
      default:
        g = p;
        b = q;
        break;
    }
  }

  return FastColorLite(r, g, b, hsv.a);
}

bool FastColorLite::isValid() const { return valid_; }

int FastColorLite::red() const { return r_; }
int FastColorLite::green() const { return g_; }
int FastColorLite::blue() const { return b_; }
double FastColorLite::alpha() const { return a_; }

FastColorLite FastColorLite::setAlpha(double alpha) const {
  return FastColorLite(r_, g_, b_, clampUnit(alpha));
}

HsvColor FastColorLite::toHsv() const {
  const int maxChannel = std::max({r_, g_, b_});
  const int minChannel = std::min({r_, g_, b_});
  const int delta = maxChannel - minChannel;

  double h = 0.0;
  if (delta != 0) {
    if (r_ == maxChannel) {
      h = 60.0 * (((g_ - b_) / static_cast<double>(delta)) + (g_ < b_ ? 6.0 : 0.0));
    } else if (g_ == maxChannel) {
      h = 60.0 * (((b_ - r_) / static_cast<double>(delta)) + 2.0);
    } else {
      h = 60.0 * (((r_ - g_) / static_cast<double>(delta)) + 4.0);
    }
  }

  h = std::round(h);

  double s = 0.0;
  if (maxChannel != 0) {
    s = delta / static_cast<double>(maxChannel);
  }

  const double v = maxChannel / 255.0;

  return HsvColor{h, s, v, a_};
}

FastColorLite FastColorLite::darken(double amountPercent) const {
  const HsvColor hsv = toHsv();
  const int maxChannel = std::max({r_, g_, b_});
  const int minChannel = std::min({r_, g_, b_});

  double lightness = (maxChannel + minChannel) / 510.0;
  lightness -= amountPercent / 100.0;
  lightness = clampUnit(lightness);

  return fromHsl(hsv.h, hsv.s, lightness, a_);
}

FastColorLite FastColorLite::lighten(double amountPercent) const {
  const HsvColor hsv = toHsv();
  const int maxChannel = std::max({r_, g_, b_});
  const int minChannel = std::min({r_, g_, b_});

  double lightness = (maxChannel + minChannel) / 510.0;
  lightness += amountPercent / 100.0;
  lightness = clampUnit(lightness);

  return fromHsl(hsv.h, hsv.s, lightness, a_);
}

FastColorLite FastColorLite::mix(const FastColorLite& other, double amountPercent) const {
  const double p = clamp(amountPercent / 100.0, 0.0, 1.0);
  const int r = static_cast<int>(std::round((other.r_ - r_) * p + r_));
  const int g = static_cast<int>(std::round((other.g_ - g_) * p + g_));
  const int b = static_cast<int>(std::round((other.b_ - b_) * p + b_));
  const double a = roundToTwo((other.a_ - a_) * p + a_);
  return FastColorLite(r, g, b, a);
}

QString FastColorLite::toHexString() const {
  QString hex = QString("#%1%2%3")
                    .arg(r_, 2, 16, QChar('0'))
                    .arg(g_, 2, 16, QChar('0'))
                    .arg(b_, 2, 16, QChar('0'));

  if (a_ >= 0.0 && a_ < 1.0) {
    const int alpha = static_cast<int>(std::round(a_ * 255.0));
    hex += QString("%1").arg(alpha, 2, 16, QChar('0'));
  }

  return hex.toLower();
}

QString FastColorLite::toRgbString() const {
  if (a_ >= 1.0) {
    return QString("rgb(%1,%2,%3)").arg(r_).arg(g_).arg(b_);
  }

  return QString("rgba(%1,%2,%3,%4)").arg(r_).arg(g_).arg(b_).arg(formatAlpha(a_));
}

bool FastColorLite::parseHex(const QString& input) {
  QString hex = input;
  if (hex.startsWith('#')) {
    hex.remove(0, 1);
  }

  auto readSingle = [&hex](int index) { return QString(hex.at(index)) + hex.at(index); };

  auto readPair = [&hex](int start) { return hex.mid(start, 2); };

  bool ok = false;
  if (hex.size() == 3 || hex.size() == 4) {
    const int r = readSingle(0).toInt(&ok, 16);
    if (!ok) {
      return false;
    }
    const int g = readSingle(1).toInt(&ok, 16);
    if (!ok) {
      return false;
    }
    const int b = readSingle(2).toInt(&ok, 16);
    if (!ok) {
      return false;
    }

    double a = 1.0;
    if (hex.size() == 4) {
      const int alpha = readSingle(3).toInt(&ok, 16);
      if (!ok) {
        return false;
      }
      a = alpha / 255.0;
    }

    r_ = r;
    g_ = g;
    b_ = b;
    a_ = clampUnit(a);
    return true;
  }

  if (hex.size() == 6 || hex.size() == 8) {
    const int r = readPair(0).toInt(&ok, 16);
    if (!ok) {
      return false;
    }
    const int g = readPair(2).toInt(&ok, 16);
    if (!ok) {
      return false;
    }
    const int b = readPair(4).toInt(&ok, 16);
    if (!ok) {
      return false;
    }

    double a = 1.0;
    if (hex.size() == 8) {
      const int alpha = readPair(6).toInt(&ok, 16);
      if (!ok) {
        return false;
      }
      a = alpha / 255.0;
    }

    r_ = r;
    g_ = g;
    b_ = b;
    a_ = clampUnit(a);
    return true;
  }

  return false;
}

bool FastColorLite::parseRgb(const QString& input) {
  const QRegularExpression prefixRe("^\\s*rgba?\\((.*)\\)\\s*$",
                                    QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch prefixMatch = prefixRe.match(input);
  if (!prefixMatch.hasMatch()) {
    return false;
  }

  const QString inside = prefixMatch.captured(1);
  const QRegularExpression numberRe("\\d*\\.?\\d+%?");
  QRegularExpressionMatchIterator it = numberRe.globalMatch(inside);

  QStringList values;
  while (it.hasNext()) {
    values.append(it.next().captured(0));
  }

  if (values.size() < 3) {
    return false;
  }

  auto toChannel = [](const QString& value) {
    if (value.endsWith('%')) {
      const double pct = value.left(value.size() - 1).toDouble();
      return static_cast<int>(std::round(pct / 100.0 * 255.0));
    }
    return static_cast<int>(std::round(value.toDouble()));
  };

  auto toAlpha = [](const QString& value) {
    if (value.endsWith('%')) {
      return value.left(value.size() - 1).toDouble() / 100.0;
    }
    return value.toDouble();
  };

  r_ = clampChannel(toChannel(values.at(0)));
  g_ = clampChannel(toChannel(values.at(1)));
  b_ = clampChannel(toChannel(values.at(2)));

  if (values.size() >= 4) {
    a_ = clampUnit(toAlpha(values.at(3)));
  } else {
    a_ = 1.0;
  }

  return true;
}

int FastColorLite::clampChannel(int value) { return static_cast<int>(clamp(value, 0.0, 255.0)); }

double FastColorLite::clampUnit(double value) { return clamp(value, 0.0, 1.0); }

double FastColorLite::clamp(double value, double minValue, double maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

QString FastColorLite::formatAlpha(double alpha) {
  QString result = QString::number(roundToTwo(alpha), 'f', 2);
  while (result.endsWith('0')) {
    result.chop(1);
  }
  if (result.endsWith('.')) {
    result.chop(1);
  }
  return result;
}

FastColorLite FastColorLite::fromHsl(double h, double s, double l, double a) {
  h = std::fmod(h, 360.0);
  if (h < 0.0) {
    h += 360.0;
  }

  s = clampUnit(s);
  l = clampUnit(l);

  if (s <= 0.0) {
    const int rgb = static_cast<int>(std::round(l * 255.0));
    return FastColorLite(rgb, rgb, rgb, a);
  }

  double r = 0.0;
  double g = 0.0;
  double b = 0.0;

  const double huePrime = h / 60.0;
  const double chroma = (1.0 - std::abs((2.0 * l) - 1.0)) * s;
  const double second = chroma * (1.0 - std::abs(std::fmod(huePrime, 2.0) - 1.0));

  if (huePrime >= 0.0 && huePrime < 1.0) {
    r = chroma;
    g = second;
  } else if (huePrime >= 1.0 && huePrime < 2.0) {
    r = second;
    g = chroma;
  } else if (huePrime >= 2.0 && huePrime < 3.0) {
    g = chroma;
    b = second;
  } else if (huePrime >= 3.0 && huePrime < 4.0) {
    g = second;
    b = chroma;
  } else if (huePrime >= 4.0 && huePrime < 5.0) {
    r = second;
    b = chroma;
  } else {
    r = chroma;
    b = second;
  }

  const double mod = l - chroma / 2.0;

  return FastColorLite(static_cast<int>(std::round((r + mod) * 255.0)),
                       static_cast<int>(std::round((g + mod) * 255.0)),
                       static_cast<int>(std::round((b + mod) * 255.0)), a);
}

}  // namespace adqt::theme
