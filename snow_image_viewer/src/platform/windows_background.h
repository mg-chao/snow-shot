#pragma once

#include <QImage>
#include <QFuture>
#include <QString>

#include <memory>

namespace snow::image_viewer {

class WindowsBackgroundController final {
  public:
    enum class Target {
        LockScreen,
        Wallpaper,
    };

    struct Result {
        bool changed = false;
        QString errorMessage;
    };

    WindowsBackgroundController();
    ~WindowsBackgroundController();

    WindowsBackgroundController(const WindowsBackgroundController&) = delete;
    WindowsBackgroundController& operator=(const WindowsBackgroundController&) = delete;

    QFuture<Result> setImage(Target target, QImage image);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snow::image_viewer
