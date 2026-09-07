#ifndef SNOW_SHOT_PRESENTATION_COMPONENTS_TOOLBAREDITSETTINGSWIDGET_H
#define SNOW_SHOT_PRESENTATION_COMPONENTS_TOOLBAREDITSETTINGSWIDGET_H

#include "snow_shot/presentation/components/settingscustomwidget.h"
#include "snow_shot/storage/settingsadapters.h"

#include <memory>

class QEvent;

namespace snow_shot::presentation::settings {
class SettingsRuntimeSession;
}

class ToolbarEditorSettingsWidget final : public SettingsCustomWidget {
  public:
    ToolbarEditorSettingsWidget(
        snow_shot::presentation::settings::SettingsCustomRenderer renderer,
        snow_shot::storage::ScreenshotToolbarLayoutKind layoutKind,
        snow_shot::presentation::settings::SettingsRuntimeSession& runtimeSession,
        QWidget* parent = nullptr);
    ~ToolbarEditorSettingsWidget() override;

    void applyTheme(const snow_shot::presentation::styles::ThemeColorScheme& scheme) override;
    void retranslateUi() override;

  protected:
    void changeEvent(QEvent* event) override;

  private:
    struct Private;
    std::unique_ptr<Private> m_private;
};

#endif // SNOW_SHOT_PRESENTATION_COMPONENTS_TOOLBAREDITSETTINGSWIDGET_H
