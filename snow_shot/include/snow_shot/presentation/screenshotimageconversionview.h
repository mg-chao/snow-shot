#ifndef SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGECONVERSIONVIEW_H
#define SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGECONVERSIONVIEW_H

#include "snow_shot/presentation/screenshotimageconversion.h"
#include <QWidget>
#include <memory>
#include <functional>

class QMimeData;

class QLabel;
class QTextBrowser;
class QUrl;
namespace adqt::widgets {
class AdButton;
} // namespace adqt::widgets

class ScreenshotImageConversionView final : public QWidget {
    Q_OBJECT
  public:
    explicit ScreenshotImageConversionView(QWidget* parent = nullptr);
    void setContent(SnowShotImageConversionFormat format, const QString& source, bool busy,
                    const QString& error);
    [[nodiscard]] QString source() const {
        return m_source;
    }
    [[nodiscard]] std::unique_ptr<QMimeData> clipboardMimeData() const;
    [[nodiscard]] bool copyToClipboard() const;
    void selectAll();
    void setCopyRequestHandler(std::function<bool()> handler) {
        m_requestCopy = std::move(handler);
    }

  signals:
    void retryRequested();
    void linkActivated(const QUrl& url);

  protected:
    void changeEvent(QEvent* event) override;

  private:
    std::function<bool()> m_requestCopy;
    void render();
    void retranslate();
    void showContextMenu(const QPoint& globalPosition);
    QTextBrowser* m_browser = nullptr;
    QWidget* m_status = nullptr;
    QLabel* m_statusText = nullptr;
    adqt::widgets::AdButton* m_retry = nullptr;
    SnowShotImageConversionFormat m_format = SnowShotImageConversionFormat::Markdown;
    QString m_source;
    QString m_error;
    bool m_busy = false;
};

#endif // SNOW_SHOT_PRESENTATION_SCREENSHOTIMAGECONVERSIONVIEW_H
