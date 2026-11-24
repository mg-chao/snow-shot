#include "snow_shot/presentation/styles/thememanager.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/settingsadapters.h"

#include "theme/theme_manager.h"
#include "themeconfig_p.h"

#include <QApplication>
#include <QFontDatabase>
#include <QStyleHints>
#include <QtGlobal>

namespace snow_shot::presentation::styles {
namespace {
const QString kThemeMode = QStringLiteral("interface/theme_mode");
const QString kPrimaryColor = QStringLiteral("interface/theme_primary_color");
const QString kFontFamily = QStringLiteral("interface/app_font");
const QString kFontSize = QStringLiteral("interface/app_font_size");

QString installedFontFamily(const QString& family) {
    return !family.isEmpty() && QFontDatabase::hasFamily(family) ? family : QString();
}

ThemeMode decodeThemeMode(const QString& value) {
    const QString key = value.trimmed().toLower();
    if (key == QStringLiteral("light")) {
        return ThemeMode::Light;
    }
    if (key == QStringLiteral("dark")) {
        return ThemeMode::Dark;
    }
    return ThemeMode::FollowSystem;
}

QString encodeThemeMode(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    case ThemeMode::FollowSystem:
    default:
        return QStringLiteral("system");
    }
}

ThemeAppearance systemThemeAppearance() {
    const QStyleHints* styleHints = QApplication::styleHints();
    if (styleHints != nullptr && styleHints->colorScheme() == Qt::ColorScheme::Dark) {
        return ThemeAppearance::Dark;
    }
    return ThemeAppearance::Light;
}

ThemeAppearance resolvedAppearance(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark:
        return ThemeAppearance::Dark;
    case ThemeMode::Light:
        return ThemeAppearance::Light;
    case ThemeMode::FollowSystem:
    default:
        return systemThemeAppearance();
    }
}

ThemeAppearance toThemeAppearance(adqt::theme::ThemeScheme scheme) {
    return scheme == adqt::theme::ThemeScheme::Dark ? ThemeAppearance::Dark
                                                    : ThemeAppearance::Light;
}

ThemePreset toThemePreset(adqt::theme::ThemeDensity density) {
    return density == adqt::theme::ThemeDensity::Compact ? ThemePreset::Compact
                                                         : ThemePreset::Default;
}

ThemeStyleConfig toThemeStyleConfig(const adqt::theme::ResolvedTheme& resolvedTheme) {
    ThemeStyleConfig config;
    config.preset = toThemePreset(resolvedTheme.config.density);
    config.appearance = toThemeAppearance(resolvedTheme.config.scheme);
    config.colorPrimary = resolvedTheme.config.primary;
    config.colorSuccess = resolvedTheme.config.success;
    config.colorWarning = resolvedTheme.config.warning;
    config.colorError = resolvedTheme.config.error;
    config.colorInfo = resolvedTheme.config.info;
    config.colorLink = resolvedTheme.config.link;
    config.colorTextBase = resolvedTheme.values.colorTextBase;
    config.colorBgBase = resolvedTheme.values.colorBgBase;
    config.appFont = resolvedTheme.config.appFont;
    config.fontSize = qRound(resolvedTheme.config.fontSize);
    config.lineWidth = qRound(resolvedTheme.config.lineWidth);
    config.borderRadius = qRound(resolvedTheme.config.borderRadius);
    config.sizeUnit = qRound(resolvedTheme.config.sizeUnit);
    config.sizeStep = qRound(resolvedTheme.config.sizeStep);
    config.controlHeight = qRound(resolvedTheme.config.controlHeight);
    config.motionUnit = resolvedTheme.config.motion ? 0.1 : 0.0;
    return config;
}
} // namespace

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent), m_config(toThemeStyleConfig(adqt::theme::makeResolvedTheme(
                           adqt::theme::ThemeManager::instance().config()))),
      m_scheme(generateThemeColorScheme(m_config)),
      m_mode(decodeThemeMode(snow_shot::storage::InterfaceSettings().themeMode())),
      m_appFontFamily(storage::InterfaceSettings().appFontFamily()),
      m_effectiveFontFamily(installedFontFamily(m_appFontFamily)),
      m_appFontSize(storage::InterfaceSettings().appFontSize()) {
    m_config.colorPrimary = storage::InterfaceSettings().themePrimaryColor();
    if (!m_effectiveFontFamily.isEmpty()) {
        m_config.appFont.setFamily(m_effectiveFontFamily);
    }
    applyFontSize(m_config);
    auto& adqtThemeManager = adqt::theme::ThemeManager::instance();
    adqtThemeManager.setConfig(detail::toAdqtThemeConfig(m_config));
    m_config = toThemeStyleConfig(adqt::theme::makeResolvedTheme(adqtThemeManager.config()));
    m_scheme = generateThemeColorScheme(m_config);
    auto& configuration = storage::ApplicationStorage::instance().configuration();
    connect(&configuration, &storage::ConfigurationStore::valuesCommitted, this,
            [this](storage::ConfigurationWriteOrigin origin, const auto&, const auto& values) {
                if (origin != storage::ConfigurationWriteOrigin::Explicit &&
                    (values.contains(kThemeMode) || values.contains(kPrimaryColor) ||
                     values.contains(kFontFamily) || values.contains(kFontSize))) {
                    applyInterfaceConfiguration(values);
                }
            });
}

void ThemeManager::initialize(QApplication& application) {
    if (m_initialized) {
        return;
    }

    m_initialized = true;
    applyThemeMode();
    adqt::theme::ThemeManager::instance().applyTo(application);
    connect(&application, &QGuiApplication::fontDatabaseChanged, this, [this] {
        if (!m_appFontFamily.isEmpty())
            applyInterfaceConfiguration({{kFontFamily, m_appFontFamily}});
    });

    if (QStyleHints* styleHints = QApplication::styleHints(); styleHints != nullptr) {
        m_systemColorSchemeConnection =
            connect(styleHints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
                if (m_mode == ThemeMode::FollowSystem) {
                    applyThemeMode();
                }
            });
    }
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeMode ThemeManager::themeMode() const {
    return m_mode;
}

ThemeColorScheme ThemeManager::themeColorScheme() const {
    return m_scheme;
}

QJsonObject ThemeManager::updateProgressAppearance() const {
    const auto& resolved = adqt::theme::ThemeManager::instance().globalResolvedTheme();
    const auto& colors = resolved.theme.palette;
    const auto& metrics = resolved.theme.metrics;
    const QColor background = resolved.semantic.surfaceElevated;
    // The native updater paints opaque RGB surfaces. Flatten Ant's translucent
    // text and fill tokens over the same elevated background used by Qt popups.
    const auto rgb = [&background](const QColor& color) {
        const float alpha = color.alphaF();
        QColor mixed;
        mixed.setRedF(color.redF() * alpha + background.redF() * (1.0F - alpha));
        mixed.setGreenF(color.greenF() * alpha + background.greenF() * (1.0F - alpha));
        mixed.setBlueF(color.blueF() * alpha + background.blueF() * (1.0F - alpha));
        return static_cast<int>(mixed.rgb() & 0xffffffU);
    };
    return {{QStringLiteral("background"), rgb(background)},
            {QStringLiteral("border"), rgb(colors.colorBorderSecondary)},
            {QStringLiteral("text"), rgb(colors.colorText)},
            {QStringLiteral("textSecondary"), rgb(colors.colorTextSecondary)},
            {QStringLiteral("textTertiary"), rgb(colors.colorTextTertiary)},
            {QStringLiteral("fillSecondary"), rgb(colors.colorFillSecondary)},
            {QStringLiteral("fillTertiary"), rgb(colors.colorFillTertiary)},
            {QStringLiteral("primary"), rgb(colors.colorPrimary)},
            {QStringLiteral("primaryBackground"), rgb(colors.colorPrimaryBg)},
            {QStringLiteral("success"), rgb(colors.colorSuccess)},
            {QStringLiteral("error"), rgb(colors.colorError)},
            {QStringLiteral("fontFamily"), QApplication::font().family().left(63)},
            {QStringLiteral("fontSize"), qRound(metrics.fontSize)},
            {QStringLiteral("smallFontSize"), qRound(metrics.fontSizeSM)},
            {QStringLiteral("borderRadius"), qRound(metrics.borderRadiusLG)},
            {QStringLiteral("motion"), resolved.config.motion}};
}

void ThemeManager::setThemeStyleConfig(const ThemeStyleConfig& config) {
    auto& adqtThemeManager = adqt::theme::ThemeManager::instance();
    adqtThemeManager.setConfig(detail::toAdqtThemeConfig(config));
    m_config = toThemeStyleConfig(adqt::theme::makeResolvedTheme(adqtThemeManager.config()));
    rebuildScheme();
}

QString ThemeManager::appFontFamily() const {
    return m_appFontFamily;
}

bool ThemeManager::setAppFontFamily(const QString& family) {
    const QString normalized = family.trimmed();
    if (!storage::InterfaceSettings().setAppFontFamily(normalized)) {
        return false;
    }
    applyInterfaceConfiguration({{kFontFamily, normalized}});
    return true;
}

int ThemeManager::appFontSize() const {
    return m_appFontSize;
}

bool ThemeManager::setAppFontSize(int pixelSize) {
    if (!storage::InterfaceSettings().setAppFontSize(pixelSize)) {
        return false;
    }
    applyInterfaceConfiguration({{kFontSize, pixelSize}});
    return true;
}

void ThemeManager::applyFontSize(ThemeStyleConfig& config) const {
    config.fontSize = m_appFontSize;
    config.appFont.setPixelSize(m_appFontSize);
}

bool ThemeManager::setThemePrimaryColor(const QColor& color) {
    if (!color.isValid() || !storage::InterfaceSettings().setThemePrimaryColor(color)) {
        return false;
    }
    applyInterfaceConfiguration({{kPrimaryColor, storage::colorToRgbaString(color)}});
    return true;
}

void ThemeManager::setThemeMode(ThemeMode mode) {
    const QString preference = encodeThemeMode(mode);
    if (storage::InterfaceSettings().setThemeMode(preference))
        applyInterfaceConfiguration({{kThemeMode, preference}});
}

void ThemeManager::setThemeAppearance(ThemeAppearance appearance) {
    setThemeMode(appearance == ThemeAppearance::Dark ? ThemeMode::Dark : ThemeMode::Light);
}

void ThemeManager::setThemePreset(ThemePreset preset) {
    if (m_config.preset == preset) {
        return;
    }

    auto& adqtThemeManager = adqt::theme::ThemeManager::instance();
    adqtThemeManager.setDensity(detail::toAdqtDensity(preset));
    m_config = toThemeStyleConfig(adqt::theme::makeResolvedTheme(adqtThemeManager.config()));
    rebuildScheme();
}

void ThemeManager::rebuildScheme() {
    m_scheme = generateThemeColorScheme(m_config);
    emit themeChanged(m_scheme);
}

void ThemeManager::applyInterfaceConfiguration(const QMap<QString, QJsonValue>& values) {
    const auto mode =
        values.contains(kThemeMode) ? decodeThemeMode(values.value(kThemeMode).toString()) : m_mode;
    const bool hasFont = values.contains(kFontFamily);
    const QString family = hasFont ? values.value(kFontFamily).toString() : m_appFontFamily;
    const QString effectiveFont = hasFont ? installedFontFamily(family) : m_effectiveFontFamily;
    const int fontSize = values.value(kFontSize).toInt(m_appFontSize);
    const auto color = storage::colorFromRgbaString(values.value(kPrimaryColor).toString());
    const bool modeChanged = mode != m_mode;
    const bool familyChanged = family != m_appFontFamily;
    const bool effectiveFontChanged = effectiveFont != m_effectiveFontFamily;
    const bool sizeChanged = fontSize != m_appFontSize;
    const bool colorChanged = color.isValid() && color != m_config.colorPrimary;

    m_mode = mode;
    m_appFontFamily = family;
    m_effectiveFontFamily = effectiveFont;
    m_appFontSize = fontSize;
    const ThemeAppearance appearance = modeChanged ? resolvedAppearance(mode) : m_config.appearance;
    if (appearance != m_config.appearance || effectiveFontChanged || sizeChanged || colorChanged) {
        auto config = m_config;
        config.appearance = appearance;
        if (colorChanged)
            config.colorPrimary = color;
        if (effectiveFontChanged) {
            config.appFont = QFont();
            if (!effectiveFont.isEmpty())
                config.appFont.setFamily(effectiveFont);
        }
        if (effectiveFontChanged || sizeChanged)
            applyFontSize(config);
        setThemeStyleConfig(config);
    }
    if (modeChanged)
        emit themeModeChanged(mode);
    if (familyChanged)
        emit appFontFamilyChanged(family);
    if (sizeChanged)
        emit appFontSizeChanged(fontSize);
}

void ThemeManager::applyThemeMode() {
    const ThemeAppearance appearance = resolvedAppearance(m_mode);
    if (m_config.appearance == appearance) {
        return;
    }

    auto& adqtThemeManager = adqt::theme::ThemeManager::instance();
    adqtThemeManager.setColorScheme(detail::toAdqtScheme(appearance));
    m_config = toThemeStyleConfig(adqt::theme::makeResolvedTheme(adqtThemeManager.config()));
    rebuildScheme();
}
} // namespace snow_shot::presentation::styles
