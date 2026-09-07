#include "snow_shot/diagnostics/diagnostics.h"
#include "diagnosticsbridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <Windows.h>
#include <malloc.h>

using namespace snow_shot::diagnostics;
extern "C" void snow_test_rust_panic(void (*callback)(const unsigned char*, size_t));
extern "C" void snow_test_ocr_initialize();
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
__declspec(noinline) void exhaustStack(unsigned depth) {
    volatile char page[4096]{};
    page[depth % sizeof(page)] = static_cast<char>(depth);
    void (*volatile recurse)(unsigned) = exhaustStack;
    recurse(depth + 1);
    page[0] = page[depth % sizeof(page)];
}
int fixture(const QStringList& arguments) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    DiagnosticsOptions options;
    options.directories = {arguments.at(2)};
    options.handlerPath = QStringLiteral(SNOW_TEST_CRASHPAD_HANDLER);
    options.mirrorToConsole = false;
    DiagnosticsService service;
    require(service.initialize(options), "fixture logger starts");
    require(service.status().crashCaptureAvailable, "fixture collector starts");
    service.record(QtWarningMsg, QStringLiteral("test"), QStringLiteral("crash.breadcrumb"));
    require(service.flush(), "pre-crash flush");
    const QString kind = arguments.at(3);
    if (kind == QStringLiteral("fatal"))
        qFatal("intentional diagnostic test");
    if (kind == QStringLiteral("abort"))
        std::abort();
    if (kind == QStringLiteral("terminate"))
        std::terminate();
    if (kind == QStringLiteral("panic")) {
        snow_test_rust_panic(snow_diag_panic);
    }
    if (kind.startsWith(QStringLiteral("ocr-"))) {
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("SNOW_SHOT_CRASHPAD_PIPE"), service.crashPipeName());
        environment.insert(QStringLiteral("SNOW_SHOT_DIAGNOSTICS_SESSION"),
                           service.status().sessionId);
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--ocr-fixture"), kind});
        require(child.waitForFinished(20000), "OCR child finishes");
        require(child.readAllStandardOutput() == QByteArrayLiteral("IPC"),
                "child diagnostics preserve stdout IPC");
        require(child.exitCode() != 0, "OCR crash terminates child");
        return 4;
    }
    if (kind == QStringLiteral("stack")) {
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);
        exhaustStack(0);
    }
    if (kind == QStringLiteral("access")) {
        void* allocation = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
        *static_cast<volatile char*>(allocation) = 1;
    }
    return 3;
}
void verifyCrash(const QString& kind) {
    std::fprintf(stderr, "Testing crash fixture: %s\n", qPrintable(kind));
    QTemporaryDir directory;
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--fixture"), directory.path(), kind});
    require(process.waitForStarted(5000), "fixture starts");
    if (!process.waitForFinished(25000)) {
        process.kill();
        process.waitForFinished(5000);
        throw std::runtime_error("crash fixture timed out");
    }
    require(process.exitCode() != 0 || process.exitStatus() == QProcess::CrashExit,
            "fixture terminates abnormally");
    auto collector = makeCrashCollector();
    QString error;
    require(collector->initialize(QDir(directory.path()).filePath(QStringLiteral("crashes")), {},
                                  {}, &error),
            "open crash database");
    const auto reports = collector->reports();
    if (reports.isEmpty()) {
        std::fprintf(stderr, "fixture %s: %s\n", qPrintable(kind),
                     process.readAllStandardError().constData());
    }
    require(reports.size() == 1, "exactly one dump per crash");
    QFile dump(reports.front().path);
    require(dump.open(QIODevice::ReadOnly), "dump readable");
    const QByteArray data = dump.readAll();
    dump.close();
    require(data.startsWith("MDMP") && data.size() > 32, "valid minidump header");
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
    const quint32 count = qFromLittleEndian<quint32>(bytes + 8);
    const quint32 offset = qFromLittleEndian<quint32>(bytes + 12);
    bool exception = false;
    for (quint32 i = 0; i < count; ++i) {
        const quint64 position = static_cast<quint64>(offset) + i * 12ULL;
        require(position + 12 <= static_cast<quint64>(data.size()), "valid stream directory");
        if (qFromLittleEndian<quint32>(bytes + position) == 6)
            exception = true;
    }
    require(exception, "dump contains an exception stream");
    require(reports.front().context.contains(QStringLiteral("exception_code")),
            "readable crash exception summary");
    require(data.contains("Snow Shot") && data.contains("crash.breadcrumb"),
            "build identity and recent event survive");
    require(collector->removeReport(reports.front().id), "database API deletes report");
    require(collector->reports().isEmpty(), "report metadata removed with dump");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().value(1) == QStringLiteral("--ocr-fixture")) {
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
            snow_test_ocr_initialize();
            constexpr char breadcrumb[] = "crash.breadcrumb OCR";
            snow_diag_breadcrumb(breadcrumb, sizeof(breadcrumb) - 1);
            std::fwrite("IPC", 1, 3, stdout);
            std::fflush(stdout);
            if (app.arguments().value(2) == QStringLiteral("ocr-panic"))
                snow_test_rust_panic(snow_diag_panic);
            void* allocation = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
            *static_cast<volatile char*>(allocation) = 1;
            return 5;
        }
        if (app.arguments().value(1) == QStringLiteral("--fixture"))
            return fixture(app.arguments());
        for (const QString& kind :
             {QStringLiteral("access"), QStringLiteral("stack"), QStringLiteral("fatal"),
              QStringLiteral("abort"), QStringLiteral("terminate"), QStringLiteral("panic"),
              QStringLiteral("ocr-access"), QStringLiteral("ocr-panic")}) {
            verifyCrash(kind);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
