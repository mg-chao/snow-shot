#include <QApplication>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <Windows.h>

#include <cstdio>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 4) {
        std::fprintf(stderr, "Usage: fixture BEFORE_EXE AFTER_EXE OUTPUT_DIRECTORY\n");
        return 2;
    }
    QWidget window;
    window.setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    window.setWindowTitle(QStringLiteral("Snow UIA Qt benchmark fixture"));
    window.setGeometry(60, 60, 500, 400);
    auto* layout = new QVBoxLayout(&window);
    QList<QPushButton*> buttons;
    for (int group = 0; group < 4; ++group) {
        auto* box = new QGroupBox(QStringLiteral("Group %1").arg(group), &window);
        auto* row = new QHBoxLayout(box);
        for (int item = 0; item < 2; ++item) {
            auto* button =
                new QPushButton(QStringLiteral("Control %1:%2").arg(group).arg(item), box);
            button->setMinimumHeight(45);
            row->addWidget(button);
            buttons.push_back(button);
        }
        layout->addWidget(box);
    }
    window.show();
    QProcess process;
    int run = 0;
    const auto start = [&]() {
        POINT origin{};
        ClientToScreen(reinterpret_cast<HWND>(window.winId()), &origin);
        const qreal scale = window.devicePixelRatioF();
        QStringList arguments{QStringLiteral("--backend"),     QStringLiteral("uia"),
                              QStringLiteral("--samples"),     QStringLiteral("15"),
                              QStringLiteral("--rounds"),      QStringLiteral("10"),
                              QStringLiteral("--interval-ms"), QStringLiteral("0"),
                              QStringLiteral("--csv")};
        for (int index : {0, 1, 6}) {
            const auto* button = buttons.at(index);
            const QPoint point = button->mapTo(&window, button->rect().center());
            arguments << QStringLiteral("--point")
                      << QString::number(origin.x + qRound(point.x() * scale))
                      << QString::number(origin.y + qRound(point.y() * scale));
        }
        process.setStandardOutputFile(QString::fromLocal8Bit(argv[3]) +
                                      (run == 0 ? QStringLiteral("/uia-before-qt.csv")
                                                : QStringLiteral("/uia-after-qt.csv")));
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process.start(QFileInfo(QString::fromLocal8Bit(argv[run + 1])).absoluteFilePath(),
                      arguments);
    };
    QObject::connect(&process, &QProcess::errorOccurred, &application,
                     [&](QProcess::ProcessError) { application.exit(1); });
    QObject::connect(&process, &QProcess::finished, &application,
                     [&](int code, QProcess::ExitStatus status) {
                         if (status != QProcess::NormalExit || code != 0) {
                             application.exit(1);
                         } else if (++run == 2) {
                             application.quit();
                         } else {
                             start();
                         }
                     });
    QTimer::singleShot(0, &application, start);
    const int result = application.exec();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
    }
    return result;
}
