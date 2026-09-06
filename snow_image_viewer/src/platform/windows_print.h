#pragma once

#include <QFuture>
#include <QImage>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace snow::image_viewer {

class WindowsPrintController final {
  public:
    struct Result {
        enum class Completion {
            Submitted,
            Canceled,
            Abandoned,
            Failed,
        };

        Completion completion = Completion::Failed;
        QString errorMessage;
    };

    WindowsPrintController();
    ~WindowsPrintController();

    WindowsPrintController(const WindowsPrintController&) = delete;
    WindowsPrintController& operator=(const WindowsPrintController&) = delete;

    QFuture<Result> showPrintUI(quintptr ownerWindowId, const QString& documentTitle,
                                const QImage& image);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snow::image_viewer
