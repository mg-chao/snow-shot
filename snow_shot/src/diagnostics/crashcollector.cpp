#include "snow_shot/diagnostics/diagnostics.h"
#include "diagnosticsbridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QTimeZone>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <DbgHelp.h>
#include "client/crash_report_database.h"
#include "client/settings.h"
#endif

namespace snow_shot::diagnostics {
namespace {
#ifdef Q_OS_WIN
QJsonObject exceptionContext(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < sizeof(MINIDUMP_HEADER))
        return {};
    MINIDUMP_HEADER header{};
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header) ||
        header.Signature != MINIDUMP_SIGNATURE || header.NumberOfStreams > 1024 ||
        static_cast<quint64>(header.StreamDirectoryRva) +
                header.NumberOfStreams * sizeof(MINIDUMP_DIRECTORY) >
            static_cast<quint64>(file.size()))
        return {};
    QJsonObject result;
    for (ULONG index = 0; index < header.NumberOfStreams; ++index) {
        MINIDUMP_DIRECTORY directory{};
        if (!file.seek(header.StreamDirectoryRva + index * sizeof(directory)) ||
            file.read(reinterpret_cast<char*>(&directory), sizeof(directory)) != sizeof(directory))
            return {};
        if (directory.StreamType != ExceptionStream)
            continue;
        MINIDUMP_EXCEPTION_STREAM exception{};
        if (directory.Location.DataSize < sizeof(exception) || !file.seek(directory.Location.Rva) ||
            file.read(reinterpret_cast<char*>(&exception), sizeof(exception)) != sizeof(exception))
            return {};
        result.insert(
            QStringLiteral("exception_code"),
            QStringLiteral("0x%1").arg(exception.ExceptionRecord.ExceptionCode, 8, 16, u'0'));
        result.insert(
            QStringLiteral("exception_address"),
            QStringLiteral("0x%1").arg(exception.ExceptionRecord.ExceptionAddress, 0, 16));
        result.insert(QStringLiteral("thread_id"), static_cast<qint64>(exception.ThreadId));
        break;
    }
    return result;
}
#endif
class LocalCrashCollector final : public CrashCollector {
  public:
    bool initialize(const QString& directory, const QString& handler, const QString& session,
                    QString* error) override {
#ifdef Q_OS_WIN
        m_database =
            crashpad::CrashReportDatabase::Initialize(base::FilePath(directory.toStdWString()));
        if (!m_database || !m_database->GetSettings()->SetUploadsEnabled(false)) {
            *error = QString::fromUtf8(QT_TRANSLATE_NOOP(
                "DiagnosticsService", "The local crash database could not be initialized."));
            return false;
        }
        if (handler.isEmpty() && session.isEmpty())
            return true;
        if (!QFileInfo(handler).isFile() ||
            !snow_diag_start(handler.toUtf8().constData(), directory.toUtf8().constData(),
                             session.toUtf8().constData(), SNOW_DIAGNOSTICS_VERSION,
                             SNOW_DIAGNOSTICS_REVISION)) {
            *error = QString::fromUtf8(QT_TRANSLATE_NOOP(
                "DiagnosticsService",
                "The crash collector could not be started. Check the application installation."));
            return false;
        }
        m_pipe = QString::fromUtf8(snow_diag_pipe());
        return true;
#else
        Q_UNUSED(directory);
        Q_UNUSED(handler);
        Q_UNUSED(session);
        *error = QString::fromUtf8(QT_TRANSLATE_NOOP(
            "DiagnosticsService", "Crash capture is unavailable on this platform."));
        return false;
#endif
    }
    QVector<CrashReport> reports() override {
        QVector<CrashReport> result;
#ifdef Q_OS_WIN
        if (!m_database)
            return result;
        std::vector<crashpad::CrashReportDatabase::Report> reports;
        std::vector<crashpad::CrashReportDatabase::Report> completed;
        m_database->GetPendingReports(&reports);
        m_database->GetCompletedReports(&completed);
        reports.insert(reports.end(), completed.begin(), completed.end());
        for (const auto& report : reports) {
            result.push_back({QString::fromStdString(report.uuid.ToString()),
                              QString::fromStdWString(report.file_path.value()),
                              QDateTime::fromSecsSinceEpoch(report.creation_time, QTimeZone::UTC),
                              static_cast<qint64>(report.total_size),
                              exceptionContext(QString::fromStdWString(report.file_path.value()))});
        }
#endif
        return result;
    }
    bool removeReport(const QString& id) override {
#ifdef Q_OS_WIN
        crashpad::UUID uuid;
        return m_database && uuid.InitializeFromString(id.toStdString()) &&
               m_database->DeleteReport(uuid) == crashpad::CrashReportDatabase::kNoError;
#else
        Q_UNUSED(id);
        return false;
#endif
    }
    QString pipeName() const override {
        return m_pipe;
    }
    bool healthy() const override {
#ifdef Q_OS_WIN
        if (m_pipe.isEmpty())
            return false;
        if (WaitNamedPipeW(reinterpret_cast<LPCWSTR>(m_pipe.utf16()), NMPWAIT_NOWAIT))
            return true;
        const DWORD error = GetLastError();
        return error == ERROR_SEM_TIMEOUT || error == ERROR_PIPE_BUSY;
#else
        return false;
#endif
    }

  private:
    QString m_pipe;
#ifdef Q_OS_WIN
    std::unique_ptr<crashpad::CrashReportDatabase> m_database;
#endif
};
} // namespace
std::shared_ptr<CrashCollector> makeCrashCollector() {
    return std::make_shared<LocalCrashCollector>();
}
} // namespace snow_shot::diagnostics
