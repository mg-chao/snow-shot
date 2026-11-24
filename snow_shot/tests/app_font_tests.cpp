#include "snow_shot/presentation/fontfamilies.h"
#include "snow_shot/presentation/globalshortcutmanager.h"
#include "snow_shot/presentation/languagemanager.h"
#include "snow_shot/presentation/settings/settingsbackend.h"
#include "snow_shot/presentation/settings/settingsregistry.h"
#include "snow_shot/presentation/styles/thememanager.h"
#include "snow_shot/storage/applicationstorage.h"
#include "snow_shot/storage/configurationschema.h"
#include "snow_shot/storage/settingsadapters.h"
#include "theme/theme_manager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void legacyFontSizesMigrate(const QString& directory) {
    namespace storage = snow_shot::storage;
    struct Case {
        QJsonValue percentage;
        int expected;
    };
    const Case cases[] = {
        {100, 14}, {101, 14}, {133, 19}, {150, 21},   {175, 25},
        {200, 28}, {99, 14},  {201, 14}, {150.5, 14}, {QStringLiteral("150"), 14}};
    const QString path = QDir(directory).filePath(QStringLiteral("legacy-font.json"));
    for (const auto& test : cases) {
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "create the legacy font configuration");
        const QByteArray document =
            QJsonDocument(
                QJsonObject{
                    {QStringLiteral("storage"),
                     QJsonObject{{QStringLiteral("schema_version"),
                                  storage::ConfigurationSchema::currentVersion()}}},
                    {QStringLiteral("interface"),
                     QJsonObject{{QStringLiteral("app_font_size_percentage"), test.percentage}}}})
                .toJson();
        require(file.write(document) == document.size(), "write the legacy font configuration");
        file.close();
        storage::ConfigurationStore configuration(path, true, true, 60000);
        require(configuration.value(QStringLiteral("interface/app_font_size")).toInt() ==
                    test.expected,
                "legacy percentages migrate to rounded pixels and invalid values use defaults");
        require(configuration.flushNow().success, "save the migrated font size");
        require(file.open(QIODevice::ReadOnly), "read the migrated font configuration");
        const auto interfaceValues = QJsonDocument::fromJson(file.readAll())
                                         .object()
                                         .value(QStringLiteral("interface"))
                                         .toObject();
        require(!interfaceValues.contains(QStringLiteral("app_font_size_percentage")) &&
                    interfaceValues.value(QStringLiteral("app_font_size")).toInt() == test.expected,
                "migration replaces the persisted percentage with a pixel size");
    }
    const auto values = storage::ConfigurationSchema::migrateLegacyValues(
        {{QStringLiteral("interface/app_font_size_percentage"), 200},
         {QStringLiteral("interface/app_font_size"), 12}});
    require(values.value(QStringLiteral("interface/app_font_size")).toInt() == 12,
            "an explicit pixel size takes precedence over the legacy percentage");
}
} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    const QDir fonts(qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts"));
    require(QFontDatabase::addApplicationFont(fonts.filePath(QStringLiteral("segoeui.ttf"))) >= 0 &&
                QFontDatabase::addApplicationFont(fonts.filePath(QStringLiteral("msyh.ttc"))) >= 0,
            "load deterministic UI fonts for offscreen checks");
#endif
    QTemporaryDir temporary;
    require(temporary.isValid(), "create isolated font storage");
    legacyFontSizesMigrate(temporary.path());
    namespace presentation = snow_shot::presentation;
    namespace storage = snow_shot::storage;
    namespace settings = presentation::settings;
    namespace styles = presentation::styles;
    auto& appStorage = storage::ApplicationStorage::instance();
    require(appStorage.initialize({temporary.path(), temporary.path(), 60000}).success,
            "initialize font storage");
    presentation::LanguageManager::instance().initialize();
    const storage::InterfaceSettings preferences;
    require(preferences.appFontFamily().isEmpty(), "system font is the default");
    require(preferences.appFontSize() == 14, "font size defaults to 14 pixels");
    const QFont originalFont = QApplication::font();
    const auto requireFontSize = [](const QFont& font, int pixelSize) {
        require(font.pixelSize() == pixelSize,
                "application font uses the requested pixel size without compounding");
    };
    const QStringList families = presentation::applicationFontFamilies();
    require(!families.isEmpty(), "font families are available");
    require(std::is_sorted(families.cbegin(), families.cend(),
                           [](const auto& a, const auto& b) {
                               return QString::compare(a, b, Qt::CaseInsensitive) < 0;
                           }),
            "font choices are sorted");
    const QString family = families.first();
    require(preferences.setAppFontFamily(family), "save startup font");
    require(preferences.setAppFontSize(21), "save startup font size");
    auto& theme = styles::ThemeManager::instance();
    theme.initialize(application);
    require(theme.appFontFamily() == family && QApplication::font().family() == family,
            "startup applies the saved family");
    require(theme.appFontSize() == 21, "startup applies the saved font size");
    requireFontSize(QApplication::font(), 21);
    presentation::GlobalShortcutManager shortcuts;
    settings::BuiltInSettingsBackend backend(shortcuts);
    const auto registry = settings::buildBuiltInSettingsRegistry();
    const auto* field = registry.field(QStringLiteral("interface.app-font"));
    require(field != nullptr && field->pageId == QStringLiteral("general-appearance") &&
                field->definition->configurationKey == QStringLiteral("interface/app_font"),
            "app font is registered in interface settings");
    const auto* page = registry.catalog().page(QStringLiteral("general-appearance"));
    const auto& general = page->sections.first();
    const auto* sizeField = registry.field(QStringLiteral("interface.app-font-size"));
    require(sizeField && general.items.last().id == sizeField->id &&
                general.items.at(general.items.size() - 2).id == field->id &&
                general.items.at(general.items.size() - 3).id ==
                    QStringLiteral("interface.theme-primary-color"),
            "font size immediately follows app font in Appearance");
    const auto* sizeSchema = storage::ConfigurationSchema::entry(sizeField->configurationKey);
    require(sizeField->kind == settings::SettingsFieldKind::Slider && sizeSchema &&
                sizeField->configurationKey == QStringLiteral("interface/app_font_size") &&
                sizeField->defaultValue.toInt() == 14 && sizeSchema->integerRange.has_value() &&
                sizeSchema->integerRange->minimum == 12 &&
                sizeSchema->integerRange->maximum == 32 && sizeSchema->integerRange->step == 1 &&
                std::get<settings::SettingsSliderDefinition>(sizeField->definition->payload)
                        .suffix.translated() == QStringLiteral("px"),
            "font size slider ranges from 12 to 32 pixels with a 14 pixel default");
    const auto sizeBinding = settings::SettingsSliderBinding::AppFontSize;
    require(backend.applySliderValue(sizeBinding, 14), "restore the default startup size");
    QLabel existing(QStringLiteral("Existing label"));
    QMenu menu;
    const auto binding = settings::SettingsSelectBinding::AppFont;
    const QString next = families.last();
    require(backend.applySelectValue(binding, next) && preferences.appFontFamily() == next &&
                backend.selectValue(binding).toString() == next,
            "editing font persists and updates runtime state");
    QCoreApplication::processEvents();
    QLabel created(QStringLiteral("New label"));
    require(existing.font().family() == next && created.font().family() == next,
            "existing and new widgets use the selected family");
    require(menu.font().family() == next, "native popup fonts follow the selected family");
    require(QApplication::font().pixelSize() == 14 &&
                QApplication::font().weight() == originalFont.weight() &&
                QApplication::font().hintingPreference() == QFont::PreferNoHinting,
            "changing family preserves size, weight and smooth rendering");
    for (const int pixelSize : {12, 13, 21, 32, 17, 14, 32}) {
        require(backend.applySliderValue(sizeBinding, pixelSize) &&
                    preferences.appFontSize() == pixelSize &&
                    backend.sliderValue(sizeBinding) == pixelSize,
                "font size edits persist and update runtime state");
        QCoreApplication::processEvents();
        const QLabel newLabel(QStringLiteral("New scaled label"));
        requireFontSize(QApplication::font(), pixelSize);
        requireFontSize(existing.font(), pixelSize);
        requireFontSize(newLabel.font(), pixelSize);
        requireFontSize(menu.font(), pixelSize);
        require(theme.themeColorScheme().metricAlias.fontSize == pixelSize &&
                    QApplication::font().family() == next,
                "theme typography and application fonts use pixels while preserving family");
    }
    require(!backend.applySliderValue(sizeBinding, 11) &&
                !backend.applySliderValue(sizeBinding, 33) && theme.appFontSize() == 32 &&
                preferences.appFontSize() == 32,
            "out-of-range font sizes leave runtime and stored settings unchanged");
    require(backend.applySelectValue(binding, QString()), "select system font at an enlarged size");
    requireFontSize(QApplication::font(), 32);
    require(backend.applySelectValue(binding, next), "restore selected family at an enlarged size");
    requireFontSize(QApplication::font(), 32);
    require(appStorage.flushNow().success, "flush font preferences");
    const storage::ConfigurationStore reloaded(
        QDir(appStorage.configurationDirectory()).filePath(QStringLiteral("config.json")), true,
        false);
    require(reloaded.value(QStringLiteral("interface/app_font")).toString() == next &&
                reloaded.value(QStringLiteral("interface/app_font_size")).toInt() == 32,
            "font family and size preferences survive reload");
    for (auto mode :
         {styles::ThemeMode::Dark, styles::ThemeMode::Light, styles::ThemeMode::FollowSystem}) {
        theme.setThemeMode(mode);
        theme.setThemePreset(styles::ThemePreset::Compact);
        theme.setThemePreset(styles::ThemePreset::Default);
        require(theme.setThemePrimaryColor(QColor("#13c2c2")), "change primary color");
        require(theme.appFontFamily() == next && QApplication::font().family() == next &&
                    adqt::theme::ThemeManager::instance().config().appFont.family() == next,
                "theme changes preserve the font family");
        requireFontSize(QApplication::font(), 32);
        require(theme.themeColorScheme().metricAlias.fontSize == 32,
                "theme and density changes preserve the font size");
    }
    const auto previous = appStorage.configuration().snapshot();
    auto imported = previous;
    const QString unavailable = QStringLiteral("SnowShot Missing Font Test Family");
    imported.insert(QStringLiteral("interface/app_font"), unavailable);
    imported.insert(QStringLiteral("interface/app_font_size"), 25);
    require(backend.importConfigurationSnapshot(imported, 3) &&
                theme.appFontFamily() == unavailable && theme.appFontSize() == 25,
            "configuration import applies font family and size using Qt fallback");
    requireFontSize(QApplication::font(), 25);
    require(QApplication::font().family() == originalFont.family(),
            "a missing preferred font uses the original application font until installed");
    const auto options = backend.dynamicSelectOptions(binding);
    require(std::any_of(options.cbegin(), options.cend(),
                        [&](const auto& option) { return option.value.toString() == unavailable; }),
            "unavailable saved font remains an option");
    require(backend.importConfigurationSnapshot(previous, 3) && theme.appFontFamily() == next &&
                theme.appFontSize() == 32,
            "restoring a snapshot restores the live font and size");
    auto legacy = previous;
    legacy.remove(QStringLiteral("interface/app_font_size"));
    legacy.insert(QStringLiteral("interface/app_font_size_percentage"), 150);
    require(backend.importConfigurationSnapshot(legacy, 3) && theme.appFontSize() == 21 &&
                preferences.appFontSize() == 21,
            "legacy configuration imports migrate the live and stored font size to pixels");
    requireFontSize(QApplication::font(), 21);
    imported.insert(QStringLiteral("interface/app_font"), 42);
    imported.insert(QStringLiteral("interface/app_font_size"), 33);
    require(backend.importConfigurationSnapshot(imported, 3) && theme.appFontFamily().isEmpty() &&
                preferences.appFontFamily().isEmpty() && theme.appFontSize() == 14 &&
                preferences.appFontSize() == 14,
            "invalid imported font values follow the schema salvage rule and restore default");
    requireFontSize(QApplication::font(), 14);
    require(backend.importConfigurationSnapshot(previous, 3), "restore the font after salvage");
    require(!backend.importConfigurationSnapshot(imported, 999) && theme.appFontFamily() == next &&
                preferences.appFontFamily() == next && theme.appFontSize() == 32 &&
                preferences.appFontSize() == 32,
            "rejected import leaves live and stored font unchanged");
    const bool reset = backend.resetSection(settings::SettingsSectionReset::GeneralSettings);
    require(reset && theme.appFontFamily().isEmpty() && preferences.appFontFamily().isEmpty() &&
                theme.appFontSize() == 14 && preferences.appFontSize() == 14,
            "General reset restores System default and 14 pixel font size");
    QCoreApplication::processEvents();
    require(QApplication::font().family() == originalFont.family() &&
                existing.font().family() == originalFont.family(),
            "System default restores the original platform family");
    requireFontSize(QApplication::font(), 14);
    requireFontSize(existing.font(), 14);
    appStorage.shutdown();
    return 0;
}
