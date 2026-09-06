#include "snow_shot/presentation/components/pagecontainerwidget.h"

#include "snow_shot/presentation/components/settingspageutils.h"

#include "widgets/scroll_area.h"

#include <QVBoxLayout>

PageContainerWidget::PageContainerWidget(
    const snow_shot::presentation::styles::ThemeAliasMetricToken& metric, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("pageContainer"));
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollArea = new adqt::widgets::AdScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("pageScrollArea"));
    snow_shot::presentation::components::configureSettingsScrollArea(m_scrollArea, metric);

    m_contentWidget = new QWidget(m_scrollArea);
    m_contentWidget->setObjectName(QStringLiteral("pageContent"));
    m_contentWidget->setAutoFillBackground(false);
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(metric.paddingLG, metric.paddingXXS, metric.paddingLG,
                                        metric.paddingLG);
    m_contentLayout->setSpacing(0);

    m_scrollArea->setContentWidget(m_contentWidget);
    rootLayout->addWidget(m_scrollArea, 1);
}

adqt::widgets::AdScrollArea* PageContainerWidget::scrollArea() const {
    return m_scrollArea;
}

QWidget* PageContainerWidget::contentWidget() const {
    return m_contentWidget;
}

QVBoxLayout* PageContainerWidget::contentLayout() const {
    return m_contentLayout;
}
