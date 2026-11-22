#pragma once

#include "snow_shot/presentation/styles/thememanager.h"
#include "theme/theme_manager.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/scroll_area.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QScrollBar>

namespace history_sticky_test {

inline void settleLayout() {
    for (int index = 0; index < 4; ++index) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
    }
}

template <typename Require>
void verifySelection(QWidget& page, const QString& prefix, adqt::widgets::AdCheckbox* checkbox,
                     Require require) {
    auto* area = page.findChild<adqt::widgets::AdScrollArea*>();
    auto* bar = page.findChild<QWidget*>(prefix + QStringLiteral("SelectionBar"));
    auto* panel = page.findChild<QWidget*>(prefix + QStringLiteral("SelectionPanel"));
    auto* entries = page.findChild<QWidget*>(prefix + QStringLiteral("Entries"));
    auto* summary = page.findChild<QLabel*>(prefix + QStringLiteral("SelectionSummary"));
    auto* selectAll =
        page.findChild<adqt::widgets::AdButton*>(prefix + QStringLiteral("SelectAll"));
    auto* batchActions =
        page.findChild<adqt::widgets::AdButton*>(prefix + QStringLiteral("BatchActions"));
    auto* deleteSelected =
        page.findChild<adqt::widgets::AdButton*>(prefix + QStringLiteral("DeleteSelected"));
    auto* deselect =
        page.findChild<adqt::widgets::AdButton*>(prefix + QStringLiteral("DeselectAll"));
    require(area && bar && panel && entries && checkbox && summary && selectAll && batchActions &&
                deleteSelected && deselect,
            "sticky selection fixture exposes the page controls");
    const auto* originalRow = checkbox->parentWidget()->parentWidget();
    const QString originalName = originalRow->objectName();
    const QVariant originalId = originalRow->property("recordId");
    const QString checkboxName = checkbox->objectName();
    const auto currentCheckbox = [&]() -> adqt::widgets::AdCheckbox* {
        for (auto* candidate : entries->findChildren<adqt::widgets::AdCheckbox*>(checkboxName)) {
            const auto* row = candidate->parentWidget()->parentWidget();
            if (row->objectName() == originalName && row->property("recordId") == originalId)
                return candidate;
        }
        return nullptr;
    };
    auto* viewport = area->viewport();
    auto* scroll = area->verticalScrollBar();
    const auto scheme =
        snow_shot::presentation::styles::ThemeManager::instance().themeColorScheme();
    const auto& metric = scheme.metricAlias;
    const int topMargin = metric.marginSM;
    const auto saveReview = [&](const QString& suffix) {
        const QString directory = qEnvironmentVariable("SNOW_STICKY_SELECTION_REVIEW_DIR");
        if (!directory.isEmpty()) {
            require(QDir().mkpath(directory) &&
                        page.grab().save(QDir(directory).filePath(prefix + suffix + ".png")),
                    "save sticky selection review");
        }
    };
    require(bar->isVisible() && panel->isVisible() && scroll->maximum() > 500 &&
                summary->text() == QStringLiteral("Selected 0 items") && selectAll->isEnabled() &&
                batchActions->isVisible() && !batchActions->isEnabled() &&
                !deleteSelected->isEnabled() && !deselect->isEnabled(),
            "a scrollable page starts with an inline panel and only Select all enabled");
    require(panel->width() == entries->width() &&
                panel->height() == scheme.metricMap.font.fontHeight + 2 * metric.paddingSM &&
                bar->height() - panel->height() == metric.margin,
            "the wide selection panel spans the list and follows the alert spacing tokens");
    require(summary->mapTo(panel, QPoint()).x() == metric.paddingLG &&
                deselect->mapTo(panel, QPoint(deselect->width(), 0)).x() ==
                    panel->width() - metric.paddingLG &&
                batchActions->x() - deleteSelected->x() - deleteSelected->width() == metric.margin,
            "selection text and compact action links follow the horizontal spacing tokens");
    const int normalTop = bar->mapTo(viewport, QPoint()).y();
    const QRect entryGeometry = entries->geometry();
    const int maximum = scroll->maximum();
    const int wideHeight = bar->height();
    require(topMargin > 0 && normalTop > topMargin, "the panel starts below the page filters");
    scroll->setValue(normalTop + wideHeight + 20);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == normalTop - scroll->value() &&
                bar->mapTo(viewport, bar->rect().bottomLeft()).y() < 0 &&
                !bar->property("pageSticky").toBool(),
            "the empty panel scrolls fully out of view without overlaying the entries");
    scroll->setValue(0);
    settleLayout();
    checkbox = currentCheckbox();
    require(checkbox, "returning to the start materializes the first record");
    const QPoint rowPosition = checkbox->mapTo(viewport, QPoint());
    checkbox->setChecked(true);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == normalTop && bar->height() == wideHeight &&
                entries->geometry() == entryGeometry && scroll->value() == 0 &&
                scroll->maximum() == maximum && checkbox->mapTo(viewport, QPoint()) == rowPosition,
            "selecting the first record keeps the panel and list geometry stable");

    scroll->setValue(normalTop / 2);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == normalTop - scroll->value(),
            "the selection panel scrolls normally before reaching the top");
    scroll->setValue(normalTop + 80);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == topMargin &&
                entries->geometry() == entryGeometry && scroll->maximum() == maximum,
            "the panel preserves its top margin without changing content layout or scroll range");
    const QPoint hitPoint = deselect->mapTo(viewport, deselect->rect().center());
    QWidget* hit = viewport->childAt(hitPoint);
    require(hit && (hit == deselect || deselect->isAncestorOf(hit)),
            "sticky actions receive pointer input above the scrolling records");
    deselect->setFocus();
    scroll->setValue(normalTop + 180);
    settleLayout();
    require(deselect->hasFocus() && bar->mapTo(viewport, QPoint()).y() == topMargin,
            "scrolling preserves focus on the original selection controls");

    adqt::theme::ThemeOverride transparent;
    transparent.backgroundOpacity = 0.0;
    auto& themes = adqt::theme::ThemeManager::instance();
    themes.setScopeOverride(&page, transparent);
    settleLayout();
    QImage panelImage(panel->size(), QImage::Format_ARGB32_Premultiplied);
    panelImage.fill(Qt::transparent);
    panel->render(&panelImage, QPoint(), QRegion(), QWidget::DrawChildren);
    require(panelImage.pixelColor(panel->width() / 2, 3).alpha() == 255,
            "a sticky panel blocks underlying records even with a transparent skin");
    themes.clearScopeOverride(&page);
    settleLayout();
    saveReview(QStringLiteral("-wide"));

    page.resize(600, 640);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == topMargin && bar->width() == entries->width() &&
                bar->height() > wideHeight && deselect->mapTo(panel, QPoint()).y() >= 0 &&
                deselect->mapTo(panel, deselect->rect().bottomRight()).y() < panel->height(),
            "a sticky panel follows narrow layout width and reserves its wrapped height");
    saveReview(QStringLiteral("-narrow"));
    const QRect narrowEntries = entries->geometry();
    const int narrowHeight = bar->height();
    const int narrowMaximum = scroll->maximum();
    const int scrolled = scroll->value();
    deselect->click();
    settleLayout();
    require(bar->isVisible() && !bar->property("pageSticky").toBool() &&
                bar->mapTo(viewport, QPoint()).y() == normalTop - scroll->value(),
            "clearing a sticky selection restores the inline panel");
    require(bar->height() == narrowHeight && entries->geometry() == narrowEntries &&
                scroll->maximum() == narrowMaximum,
            "clearing a sticky selection preserves the narrow layout and scroll range");
    require(scroll->value() == scrolled,
            "clearing a focused sticky action preserves scroll position");

    scroll->setValue(0);
    settleLayout();
    checkbox = currentCheckbox();
    require(checkbox && !checkbox->isChecked(),
            "clearing a sticky selection also unchecks a rematerialized entry");
    scroll->setValue(scrolled);
    checkbox->setChecked(true);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == topMargin &&
                entries->geometry() == narrowEntries && scroll->value() == scrolled,
            "selecting while already scrolled immediately restores the sticky panel");

    page.resize(980, 640);
    settleLayout();
    require(bar->height() == wideHeight && bar->width() == entries->width(),
            "widening restores the original sticky panel size");
    page.hide();
    page.show();
    settleLayout();
    require(bar->isVisible() && bar->mapTo(viewport, QPoint()).y() == topMargin,
            "page revisits preserve the sticky selection");

    scroll->setValue(scroll->maximum());
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == topMargin,
            "selection actions remain available at the end of the page");
    scroll->setValue(0);
    settleLayout();
    require(bar->mapTo(viewport, QPoint()).y() == normalTop && entries->geometry() == entryGeometry,
            "scrolling back restores the normal panel position without layout drift");
}

} // namespace history_sticky_test
