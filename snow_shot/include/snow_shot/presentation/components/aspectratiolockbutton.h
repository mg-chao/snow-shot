#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_ASPECTRATIOLOCKBUTTON_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_ASPECTRATIOLOCKBUTTON_H

#include "snow_shot/presentation/components/icons/snowshoticons.h"

#include "theme/theme_manager.h"
#include "widgets/button.h"

#include <QAbstractButton>
#include <QColor>
#include <QSize>

class AspectRatioLockButton final : public adqt::widgets::AdButton {
  public:
    explicit AspectRatioLockButton(QWidget* parent = nullptr) : adqt::widgets::AdButton(parent) {
        setButtonStyle(ButtonStyle::Text);
        setInteractionBackgroundVisible(false);
        setAccentRole(AccentRole::Neutral);
        setCheckable(true);
        setIconSize(QSize(20, 20));
        setFixedSize(32, 32);

        connect(this, &QAbstractButton::toggled, this, [this] { updateAspectRatioVisual(); });
        connect(&adqt::theme::ThemeManager::instance(), &adqt::theme::ThemeManager::themeChanged,
                this, [this] { updateAspectRatioVisual(); });
        updateAspectRatioVisual();
    }

  private:
    void updateAspectRatioVisual() {
        setAccentRole(isChecked() ? AccentRole::Primary : AccentRole::Neutral);
        adqt::icons::IconColors colors;
        if (isChecked()) {
            const QColor primaryActive =
                adqt::theme::ThemeManager::instance().resolveTheme(this).colorPrimaryActive;
            colors = adqt::icons::IconColors::primary(
                primaryActive.isValid() ? primaryActive : QColor(QStringLiteral("#0958d9")));
        }
        setIconRef(snow_shot::presentation::icons::custom::outlined::SelectionLockAspect(colors));
    }
};

#endif // SNOW_SHOT_PRESENTATION_COMPONENTS_ASPECTRATIOLOCKBUTTON_H
