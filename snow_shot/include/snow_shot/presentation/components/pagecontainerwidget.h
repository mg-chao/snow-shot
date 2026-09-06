#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_PAGECONTAINERWIDGET_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_PAGECONTAINERWIDGET_H

#include "snow_shot/presentation/styles/themecolorscheme.h"

#include <QWidget>

class QVBoxLayout;
namespace adqt::widgets {
class AdScrollArea;
}

class PageContainerWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit PageContainerWidget(
        const snow_shot::presentation::styles::ThemeAliasMetricToken& metric,
        QWidget* parent = nullptr);

    [[nodiscard]] adqt::widgets::AdScrollArea* scrollArea() const;
    [[nodiscard]] QWidget* contentWidget() const;
    [[nodiscard]] QVBoxLayout* contentLayout() const;

  private:
    adqt::widgets::AdScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
};

#endif // SNOW_SHOT_PRESENTATION_COMPONENTS_PAGECONTAINERWIDGET_H
