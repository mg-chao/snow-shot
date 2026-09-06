#pragma once

#include "locale_types.h"

#include <QApplication>
#include <QObject>
#include <QPointer>
#include <QTranslator>

#include <memory>

namespace adqt::locale {

class LocaleManager final : public QObject {
  Q_OBJECT

 public:
  static LocaleManager& instance();

  void setConfig(const LocaleConfig& config);
  const LocaleConfig& config() const;

  void setLocale(const QLocale& locale);
  const QLocale& locale() const;

  quint64 localeRevision() const;

  void applyTo(QApplication& app);

 signals:
  void localeChanged(const QLocale& locale);

 private:
  explicit LocaleManager(QObject* parent = nullptr);

  bool refreshTranslator(const QLocale& locale);
  QString catalogPathFor(const QLocale& locale) const;
  void applyApplicationLocale(const QLocale& locale);

  LocaleConfig config_;
  QLocale locale_;
  QPointer<QApplication> app_;
  std::unique_ptr<QTranslator> translator_;
  QString translatorLocaleName_;
  quint64 revision_ = 1;
};

}  // namespace adqt::locale
