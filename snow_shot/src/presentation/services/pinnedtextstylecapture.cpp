#include "snow_shot/presentation/screenshotclipboardcontent.h"
#include "snow_shot/storage/settingsadapters.h"
#include "snow_shot/storage/applicationstorage.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QThread>
#include "theme/theme_manager.h"

PinnedColorSwatchStyle capturePinnedColorSwatchStyle() {
    Q_ASSERT(QCoreApplication::instance() &&
             QThread::currentThread() == QCoreApplication::instance()->thread());
    PinnedColorSwatchStyle style;
    style.font = QGuiApplication::font();
    const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(nullptr);
    style.background = theme.colorBgContainer;
    style.foreground = theme.colorText;
    style.secondary = theme.colorTextSecondary;
    style.link = theme.colorLink;
    style.colorLabel = QCoreApplication::translate("PinnedColorSwatch", "Color:");
    style.labels = {QCoreApplication::translate("PinnedColorSwatch", "HEX:"),
                    QCoreApplication::translate("PinnedColorSwatch", "RGB:"),
                    QCoreApplication::translate("PinnedColorSwatch", "HSL:")};
    style.rgbaLabel = QCoreApplication::translate("PinnedColorSwatch", "RGBA:");
    style.hslaLabel = QCoreApplication::translate("PinnedColorSwatch", "HSLA:");
    style.copyLabels = {QCoreApplication::translate("PinnedColorSwatch", "Copy HEX color"),
                        QCoreApplication::translate("PinnedColorSwatch", "Copy RGB color"),
                        QCoreApplication::translate("PinnedColorSwatch", "Copy HSL color")};
    return style;
}

snow_shot::storage::PinnedTextStyle ScreenshotClipboardContentReader::capturePinnedTextStyle() {
    Q_ASSERT(QCoreApplication::instance() &&
             QThread::currentThread() == QCoreApplication::instance()->thread());
    const snow_shot::storage::PinToScreenSettings settings;
    snow_shot::storage::PinnedTextStyle style;
    const bool initialized = snow_shot::storage::ApplicationStorage::instance().isInitialized();
    if (initialized)
        style.maximumWidth = settings.textMaximumWidth();
    QFont font = QGuiApplication::font();
    const QString preferredFamily = initialized ? settings.textFontFamily() : QString();
    if (!preferredFamily.isEmpty()) {
        if (QFontDatabase::hasFamily(preferredFamily))
            font.setFamily(preferredFamily);
        else
            font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    }
    style.fontFamily = QFontInfo(font).family();
    if (style.fontFamily.isEmpty())
        style.fontFamily = font.family();
    if (initialized)
        style.fontSize = settings.textFontSize();
    style.textColor = initialized ? settings.textColor() : QColor();
    if (!style.textColor.isValid())
        style.textColor = QGuiApplication::palette().color(QPalette::Text);
    style.backgroundColor = initialized ? settings.textBackgroundColor() : QColor();
    if (!style.backgroundColor.isValid())
        style.backgroundColor = QGuiApplication::palette().color(QPalette::Base);
    return style;
}
