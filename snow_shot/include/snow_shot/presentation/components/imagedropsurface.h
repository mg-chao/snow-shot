// SPDX-License-Identifier: GPL-3.0-only
#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_IMAGEDROPSURFACE_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_IMAGEDROPSURFACE_H

#include "antd_icons.h"
#include "icon_renderer.h"
#include "theme/theme_manager.h"
#include "widgets/button.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace snow_shot::presentation::components {

class ImageDropIcon final : public QWidget {
  public:
    explicit ImageDropIcon(QWidget* parent) : QWidget(parent) {
        setFixedSize(48, 48);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
        QPainter painter(this);
        adqt::icons::IconStatePalette palette;
        palette.set(QIcon::Normal, QIcon::Off,
                    adqt::icons::IconColors::primary(theme.colorPrimary));
        adqt::icons::paintIcon(&painter, adqt::icons::antd::outlined::Inbox(), rect(), {}, palette);
    }
};

// Ant Design Upload's drag surface, shared by image import previews.
class ImageDropSurface final : public QWidget {
  public:
    explicit ImageDropSurface(bool active, QWidget* parent) : QWidget(parent), m_active(active) {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(32, 32, 32, 32);
        column->setSpacing(8);
        column->addStretch();
        m_icon = new ImageDropIcon(this);
        column->addWidget(m_icon, 0, Qt::AlignHCenter);
        column->addSpacing(8);
        title = new QLabel(this);
        description = new QLabel(this);
        for (auto* label : {title, description}) {
            label->setAlignment(Qt::AlignCenter);
            label->setWordWrap(true);
            column->addWidget(label);
        }
        column->addStretch();
        if (active)
            setAttribute(Qt::WA_TransparentForMouseEvents);
        refreshTheme();
    }

    adqt::widgets::AdButton* addImageButton() {
        using adqt::widgets::AdButton;
        auto* button = new AdButton(this);
        button->setIconRef(adqt::icons::antd::outlined::Plus());
        button->setButtonStyle(AdButton::ButtonStyle::Solid);
        button->setAccentRole(AdButton::AccentRole::Primary);
        button->setFocusPolicy(Qt::StrongFocus);
        auto* column = static_cast<QVBoxLayout*>(layout());
        column->insertSpacing(column->count() - 1, 8);
        column->insertWidget(column->count() - 1, button, 0, Qt::AlignHCenter);
        return button;
    }

    void refreshTheme() {
        const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
        auto titleFont = font();
        titleFont.setPixelSize(qRound(theme.fontSizeLG));
        title->setFont(titleFont);
        auto descriptionFont = font();
        descriptionFont.setPixelSize(qRound(theme.fontSize));
        description->setFont(descriptionFont);
        auto palette = this->palette();
        palette.setColor(QPalette::WindowText, theme.colorText);
        title->setPalette(palette);
        palette.setColor(QPalette::WindowText, theme.colorTextSecondary);
        description->setPalette(palette);
        m_icon->update();
        update();
    }

    QLabel* title;
    QLabel* description;

  protected:
    void paintEvent(QPaintEvent*) override {
        const auto theme = adqt::theme::ThemeManager::instance().resolveTheme(this);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (m_active)
            painter.fillRect(rect(), theme.colorBgContainer);
        painter.setPen(QPen(m_active ? theme.colorPrimary : theme.colorBorder, 1, Qt::DashLine));
        painter.setBrush(m_active ? theme.colorPrimaryBg : theme.colorFillAlter);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), theme.borderRadiusLG,
                                theme.borderRadiusLG);
    }

  private:
    bool m_active;
    ImageDropIcon* m_icon;
};

} // namespace snow_shot::presentation::components

#endif // SNOW_SHOT_PRESENTATION_COMPONENTS_IMAGEDROPSURFACE_H
