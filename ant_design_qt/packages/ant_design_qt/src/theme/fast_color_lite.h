#pragma once

#include <QString>

namespace adqt::theme {

struct HsvColor {
  double h;
  double s;
  double v;
  double a;
};

class FastColorLite {
 public:
  FastColorLite();
  explicit FastColorLite(const QString& input);
  FastColorLite(int r, int g, int b, double a = 1.0);

  static FastColorLite fromHsv(const HsvColor& hsv);

  bool isValid() const;

  int red() const;
  int green() const;
  int blue() const;
  double alpha() const;

  FastColorLite setAlpha(double alpha) const;

  HsvColor toHsv() const;

  FastColorLite darken(double amountPercent) const;
  FastColorLite lighten(double amountPercent) const;
  FastColorLite mix(const FastColorLite& other, double amountPercent) const;

  QString toHexString() const;
  QString toRgbString() const;

 private:
  bool parseHex(const QString& input);
  bool parseRgb(const QString& input);

  static int clampChannel(int value);
  static double clampUnit(double value);
  static double clamp(double value, double minValue, double maxValue);
  static QString formatAlpha(double alpha);

  static FastColorLite fromHsl(double h, double s, double l, double a);

  int r_;
  int g_;
  int b_;
  double a_;
  bool valid_;
};

}  // namespace adqt::theme
