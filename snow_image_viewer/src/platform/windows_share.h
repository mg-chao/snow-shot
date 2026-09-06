#pragma once

#include <QString>
#include <QtGlobal>

#include <memory>

namespace snow::image_viewer {

class WindowsShareController final {
  public:
    WindowsShareController();
    ~WindowsShareController();

    WindowsShareController(const WindowsShareController&) = delete;
    WindowsShareController& operator=(const WindowsShareController&) = delete;

    bool showShareUI(quintptr ownerWindowId, const QString& filePath, QString* errorMessage);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace snow::image_viewer
