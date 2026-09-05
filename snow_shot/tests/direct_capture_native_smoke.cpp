#include "directcapturenative.h"
#include "snow_capture.h"

#include <QApplication>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include <windows.h>

#include <cstdlib>
#include <iostream>

using namespace snow_shot::presentation;

class CaptureFixture final : public QWidget {
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(30, 170, 80));
        painter.fillRect(QRect(0, 0, width() / 2, height()), QColor(210, 40, 70));
    }
};

bool hasFixturePixels(const QImage& image) {
    bool red = false;
    bool green = false;
    for (int y = 0; y < image.height(); y += 7) {
        for (int x = 0; x < image.width(); x += 7) {
            const QColor color = image.pixelColor(x, y);
            red |= color.red() > 150 && color.green() < 90 && color.blue() < 130;
            green |= color.green() > 120 && color.red() < 90 && color.blue() < 140;
        }
    }
    return red && green;
}

bool isConcreteBackend(quint8 backend) {
    return backend == SNOW_CAPTURE_BACKEND_DXGI || backend == SNOW_CAPTURE_BACKEND_WGC ||
           backend == SNOW_CAPTURE_BACKEND_GDI;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (!app.arguments().contains(QStringLiteral("--run-native"))) {
        std::cerr << "Pass --run-native to capture a visible fixture and its monitor.\n";
        return 2;
    }
    CaptureFixture fixture;
    fixture.setWindowTitle(QStringLiteral("Snow Shot direct capture smoke"));
    fixture.resize(360, 240);
    fixture.show();
    fixture.repaint();
    QTimer::singleShot(0, &app, [&]() {
        DirectCaptureRequest windowRequest;
        windowRequest.target = DirectCaptureTarget::FocusedWindow;
        windowRequest.window = fixture.winId();
        const auto window = captureDirectTarget(windowRequest);
        DirectCaptureRequest monitorRequest;
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const auto nativeMonitor =
            MonitorFromWindow(reinterpret_cast<HWND>(fixture.winId()), MONITOR_DEFAULTTONULL);
        if (!GetMonitorInfoW(nativeMonitor, reinterpret_cast<MONITORINFO*>(&monitorInfo))) {
            std::cerr << "Could not resolve the fixture's native monitor.\n";
            app.exit(1);
            return;
        }
        monitorRequest.monitorName = QString::fromWCharArray(monitorInfo.szDevice);
        const auto monitor = captureDirectTarget(monitorRequest);
        std::cout << "window: " << window.image.width() << 'x' << window.image.height()
                  << " backend=" << static_cast<int>(window.backend)
                  << " error=" << window.error.toStdString() << '\n';
        std::cout << "monitor: " << monitor.image.width() << 'x' << monitor.image.height()
                  << " backend=" << static_cast<int>(monitor.backend)
                  << " error=" << monitor.error.toStdString() << '\n';
        const bool pixelsValid = window.isValid() && monitor.isValid() &&
                                 hasFixturePixels(window.image) && hasFixturePixels(monitor.image);
        const bool preferredBackends = window.backend == SNOW_CAPTURE_BACKEND_WGC &&
                                       monitor.backend == SNOW_CAPTURE_BACKEND_DXGI;
        std::cout << "fixture_pixels=" << pixelsValid << " preferred_backends=" << preferredBackends
                  << '\n';
        const bool valid =
            pixelsValid && isConcreteBackend(window.backend) && isConcreteBackend(monitor.backend);
        const bool requirePreferred =
            app.arguments().contains(QStringLiteral("--require-preferred-backends"));
        app.exit(valid && (!requirePreferred || preferredBackends) ? 0 : 1);
    });
    return app.exec();
}
