#pragma once

#include <QLabel>
#include <QPalette>
#include <QWidget>

#include <algorithm>
#include <utility>

#include "theme/theme_manager.h"

namespace demo {

inline QColor themeColorOr(const QColor& value, const QColor& fallback) {
  return value.isValid() ? value : fallback;
}

inline adqt::theme::ResolvedTheme resolve(const QWidget* widget) {
  return adqt::theme::ThemeManager::instance().resolve(widget);
}

inline adqt::theme::ThemeMapToken resolveTheme(const QWidget* widget,
                                               const QWidget* logicalOwner = nullptr) {
  return adqt::theme::ThemeManager::instance().resolveTheme(widget, logicalOwner);
}

class DemoHintLabel final : public QLabel {
 public:
  explicit DemoHintLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {
    setWordWrap(true);
    applyTheme();
    connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged, this,
            [this]() { applyTheme(); });
  }

 private:
  void applyTheme() {
    const adqt::theme::AdThemePalette& colors = resolve(this).theme.palette;
    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText,
                     themeColorOr(colors.colorTextTertiary,
                                  themeColorOr(colors.colorTextSecondary, QColor("#8c8c8c"))));
    setPalette(palette);
  }
};

inline QLabel* makeHintLabel(const QString& text, QWidget* parent = nullptr) {
  return new DemoHintLabel(text, parent);
}
template <typename Target, typename Fn>
inline void bindThemeRefresh(Target* target, Fn refresh) {
  if (!target) {
    return;
  }
  refresh();
  QObject::connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged,
                   target, [refresh = std::move(refresh)]() mutable { refresh(); });
}
}  // namespace demo

inline QLabel* makeHintLabel(const QString& text, QWidget* parent = nullptr) {
  return demo::makeHintLabel(text, parent);
}
