#ifndef SNOW_SHOT_PRESENTATION_STYLES_THEMEMANAGER_H
#define SNOW_SHOT_PRESENTATION_STYLES_THEMEMANAGER_H

#include <QObject>
#include <QMetaObject>
#include <QJsonObject>
#include <QMap>

#include "snow_shot/presentation/styles/themecolorscheme.h"

class QApplication;

namespace snow_shot::presentation::styles {
class ThemeManager : public QObject {
    Q_OBJECT

  public:
    static ThemeManager& instance();

    void initialize(QApplication& application);

    [[nodiscard]] QString appFontFamily() const;
    [[nodiscard]] int appFontSize() const;
    [[nodiscard]] ThemeMode themeMode() const;
    [[nodiscard]] ThemeColorScheme themeColorScheme() const;
    [[nodiscard]] QJsonObject updateProgressAppearance() const;

  public slots:
    void setThemeStyleConfig(const ThemeStyleConfig& config);
    bool setAppFontFamily(const QString& family);
    bool setAppFontSize(int pixelSize);
    bool setThemePrimaryColor(const QColor& color);
    void setThemeMode(ThemeMode mode);
    void setThemeAppearance(ThemeAppearance appearance);
    void setThemePreset(ThemePreset preset);

  signals:
    void appFontFamilyChanged(const QString& family);
    void appFontSizeChanged(int pixelSize);
    void themeModeChanged(ThemeMode mode);
    void themeChanged(const ThemeColorScheme& scheme);

  private:
    explicit ThemeManager(QObject* parent = nullptr);

    void rebuildScheme();
    void applyThemeMode();
    void applyFontSize(ThemeStyleConfig& config) const;
    void applyInterfaceConfiguration(const QMap<QString, QJsonValue>& values);

    ThemeStyleConfig m_config;
    ThemeColorScheme m_scheme;
    ThemeMode m_mode = ThemeMode::FollowSystem;
    QString m_appFontFamily;
    QString m_effectiveFontFamily;
    int m_appFontSize = 14;
    QMetaObject::Connection m_systemColorSchemeConnection;
    bool m_initialized = false;
};
} // namespace snow_shot::presentation::styles

#endif // SNOW_SHOT_PRESENTATION_STYLES_THEMEMANAGER_H
