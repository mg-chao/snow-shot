#include <QApplication>

#include "demo_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    DemoWindow window;
    window.resize(1280, 720);
    window.show();

    return app.exec();
}
