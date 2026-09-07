#ifndef SNOW_SHOT_PLATFORM_WINDOWS_PRINTSCREENSHORTCUTRECORDER_H
#define SNOW_SHOT_PLATFORM_WINDOWS_PRINTSCREENSHORTCUTRECORDER_H

#include <Qt>

#include <functional>
#include <memory>

class QKeyEvent;
class QWidget;

namespace snow_shot::platform::windows {

class PrintScreenShortcutRecorder final {
  public:
    using Handler = std::function<void(Qt::KeyboardModifiers)>;

    PrintScreenShortcutRecorder(QWidget& target, Handler handler);
    ~PrintScreenShortcutRecorder();

    bool handleKeyEvent(const QKeyEvent& event);
    void cancelPendingCapture();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace snow_shot::platform::windows

#endif // SNOW_SHOT_PLATFORM_WINDOWS_PRINTSCREENSHORTCUTRECORDER_H
