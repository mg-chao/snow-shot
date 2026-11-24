// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "snow_shot/presentation/components/actionrow.h"
#include "snow_shot/mouseoperations.h"
#include <functional>
class ShortcutConfigurationButton;
namespace snow_shot::presentation::settings {
class SettingsRuntimeSession;
}

class MouseOperationRow final : public ActionRow {
    Q_OBJECT
  public:
    MouseOperationRow(snow_shot::mouse::Action action,
                      snow_shot::presentation::settings::SettingsRuntimeSession& session,
                      const snow_shot::presentation::styles::ThemeColorScheme& scheme,
                      QWidget* parent);
    void refresh();
    void applyTheme(const snow_shot::presentation::styles::ThemeColorScheme& scheme) override;

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void openDialog();
    snow_shot::mouse::Action m_action;
    snow_shot::presentation::settings::SettingsRuntimeSession& m_session;
    ShortcutConfigurationButton* m_button = nullptr;
    std::function<void()> m_dialogRefresh;
};
