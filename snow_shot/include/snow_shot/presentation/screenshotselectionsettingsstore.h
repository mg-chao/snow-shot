#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTSELECTIONSETTINGSSTORE_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTSELECTIONSETTINGSSTORE_H

#include "snow_shot/presentation/screenshotselectionexportworkflowports.h"
#include "snow_shot/presentation/screenshotselectionparams.h"

#include <QVector>

class ScreenshotSelectionSettingsStore final : public ScreenshotSelectionParamsStorePort {
  public:
    explicit ScreenshotSelectionSettingsStore();

    [[nodiscard]] bool hasPreviousSelectionParams() const;
    [[nodiscard]] ScreenshotSelectionParams previousSelectionParams() const;
    void setPreviousSelectionParams(const ScreenshotSelectionParams& params) override;

    [[nodiscard]] int cornerRadius() const;
    [[nodiscard]] int shadowWidth() const;
    void setSelectionEffects(int cornerRadius, int shadowWidth);

    [[nodiscard]] QVector<ScreenshotSelectionPreset> presets() const;
    void setPresets(const QVector<ScreenshotSelectionPreset>& presets);

    void clear();
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTSELECTIONSETTINGSSTORE_H
