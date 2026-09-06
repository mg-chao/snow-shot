#ifndef SNOW_SHOT_PRESENTATION_LANGUAGEMANAGER_H
#define SNOW_SHOT_PRESENTATION_LANGUAGEMANAGER_H

#include <QLocale>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTranslator>
#include <QString>

#include <memory>

namespace snow_shot::presentation {
struct LanguageCatalog {
    QString localeName;
    QString nativeName;
};

class LanguageManager final : public QObject {
    Q_OBJECT

  public:
    static LanguageManager& instance();

    void initialize();

    [[nodiscard]] QList<LanguageCatalog> availableLanguages() const;
    [[nodiscard]] QString languagePreference() const;
    [[nodiscard]] QLocale currentLocale() const;
    [[nodiscard]] bool setLanguage(const QString& languagePreference);

  signals:
    void languageChanged(const QString& preference, const QLocale& effectiveLocale);
    void languageChangeFailed(const QString& preference);

  private:
    explicit LanguageManager(QObject* parent = nullptr);

    [[nodiscard]] bool applyPreference(const QString& preference, bool persist);
    void discoverCatalogs();

    QList<LanguageCatalog> m_availableLanguages;
    QHash<QString, QString> m_catalogResources;
    QHash<QString, QLocale> m_catalogLocales;
    QString m_languagePreference = QStringLiteral("system");
    QLocale m_currentLocale = QLocale(QLocale::English, QLocale::UnitedStates);
    std::unique_ptr<QTranslator> m_translator;
    bool m_initialized = false;
};
} // namespace snow_shot::presentation

#endif // SNOW_SHOT_PRESENTATION_LANGUAGEMANAGER_H
