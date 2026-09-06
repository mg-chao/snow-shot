#include "locale_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTranslator>

namespace adqt::locale {

namespace {

constexpr auto kCatalogPrefix = ":/ant_design_qt/i18n/";
constexpr auto kCatalogFilePrefix = "ant_design_qt_";
constexpr auto kCatalogFileSuffix = ".qm";

QString canonicalLocaleName(const QString& rawName) {
  QString value = rawName.trimmed();
  value.replace(u'-', u'_');
  const QLocale locale(value);
  if (locale.language() == QLocale::AnyLanguage || locale.name() == QStringLiteral("C")) {
    return {};
  }
  return locale.name();
}

bool sameScript(const QLocale& lhs, const QLocale& rhs) {
  return lhs.language() == rhs.language() && lhs.script() != QLocale::AnyScript &&
         lhs.script() == rhs.script();
}

bool sameLanguage(const QLocale& lhs, const QLocale& rhs) {
  return lhs.language() == rhs.language();
}

}  // namespace

LocaleManager& LocaleManager::instance() {
  static LocaleManager manager;
  return manager;
}

LocaleManager::LocaleManager(QObject* parent)
    : QObject(parent), config_(), locale_(config_.locale) {}

void LocaleManager::setConfig(const LocaleConfig& config) {
  if (config_ == config) {
    return;
  }

  if (!refreshTranslator(config.locale)) {
    return;
  }

  config_ = config;
  locale_ = config.locale;
  applyApplicationLocale(locale_);
  ++revision_;
  emit localeChanged(locale_);
}

const LocaleConfig& LocaleManager::config() const { return config_; }

void LocaleManager::setLocale(const QLocale& locale) {
  LocaleConfig next = config_;
  next.locale = locale;
  setConfig(next);
}

const QLocale& LocaleManager::locale() const { return locale_; }

quint64 LocaleManager::localeRevision() const { return revision_; }

void LocaleManager::applyTo(QApplication& app) {
  app_ = &app;
  refreshTranslator(locale_);
  applyApplicationLocale(locale_);
}

QString LocaleManager::catalogPathFor(const QLocale& locale) const {
  const QDir directory(QString::fromLatin1(kCatalogPrefix));
  const QStringList names =
      directory.entryList({QStringLiteral("ant_design_qt_*.qm")}, QDir::Files, QDir::Name);
  const QString filePrefix = QString::fromLatin1(kCatalogFilePrefix);
  const QString fileSuffix = QString::fromLatin1(kCatalogFileSuffix);

  QString exact;
  QString script;
  QString language;
  for (const QString& name : names) {
    const QString localeName = canonicalLocaleName(
        name.mid(filePrefix.size(), name.size() - filePrefix.size() - fileSuffix.size()));
    const QLocale catalogLocale(localeName);
    if (catalogLocale.name() == locale.name()) {
      exact = name;
      break;
    }
    if (script.isEmpty() && sameScript(locale, catalogLocale)) {
      script = name;
    }
    if (language.isEmpty() && sameLanguage(locale, catalogLocale)) {
      language = name;
    }
  }

  const QString selected = !exact.isEmpty() ? exact : (!script.isEmpty() ? script : language);
  return selected.isEmpty() ? QString() : QString::fromLatin1(kCatalogPrefix) + selected;
}

bool LocaleManager::refreshTranslator(const QLocale& locale) {
  std::unique_ptr<QTranslator> nextTranslator;
  QString nextTranslatorLocaleName;

  const QString catalogPath = catalogPathFor(locale);
  if (!catalogPath.isEmpty()) {
    nextTranslator = std::make_unique<QTranslator>();
    if (!nextTranslator->load(catalogPath)) {
      return false;
    }
    nextTranslatorLocaleName = nextTranslator->language();
  }

  if (QCoreApplication::instance()) {
    QTranslator* const previousTranslator = translator_.get();
    if (nextTranslator) {
      QCoreApplication::installTranslator(nextTranslator.get());
    }
    if (previousTranslator) {
      QCoreApplication::removeTranslator(previousTranslator);
    }
  }

  translator_ = std::move(nextTranslator);
  translatorLocaleName_ = std::move(nextTranslatorLocaleName);
  return true;
}

void LocaleManager::applyApplicationLocale(const QLocale& locale) {
  QLocale::setDefault(locale);
  if (app_) {
    app_->setLayoutDirection(locale.textDirection());
  }
}

}  // namespace adqt::locale
