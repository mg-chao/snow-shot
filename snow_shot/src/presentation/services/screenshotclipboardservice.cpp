#include "snow_shot/presentation/screenshotclipboardservice.h"
#include "snow_shot/runtime/runtimeactivitytracker.h"
#include "snow_shot/diagnostics/diagnostics.h"
#include <QUuid>

#include "screenshotclipboardperfinstrumentation.h"
#include "screenshotclipboarddibwriter.h"
#include "screenshotclipboardfilepayload.h"
#include "snowimageqtcodec.h"
#ifdef Q_OS_MACOS
#include "../../platform/macos/imageclipboard.h"
#endif

#include <QClipboard>
#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVarLengthArray>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

struct ScreenshotClipboardCommitState {
    std::atomic_bool cancelled{false};
    std::atomic_bool finished{false};
    std::atomic_bool completionEnabled{true};
    std::weak_ptr<ScreenshotClipboardCommitScopeState> scope;
};

struct ScreenshotClipboardCommitScopeState {
    QHash<ScreenshotClipboardCommitState*, std::shared_ptr<ScreenshotClipboardCommitState>> pending;
};

namespace {
#if !defined(Q_OS_WIN)
// Keep canonical PNG bytes for native consumers and lazily provide Qt's image
// representation when a reader requests it. Decoded pixels belong to that reader.
class PngClipboardMimeData final : public QMimeData {
  public:
    PngClipboardMimeData(QByteArray png, QByteArray placement, QByteArray appearance,
                         const QString& filePath, std::shared_ptr<std::atomic_bool> fileUrlRead)
        : m_fileUrlRead(std::move(fileUrlRead)) {
#ifdef Q_OS_MACOS
        snow_shot::platform::macos::initializeImageClipboardConverter();
#endif
        ensureScreenshotClipboardPlacementMimeSupport();
        ensureScreenshotClipboardAppearanceMimeSupport();
        if (!appearance.isEmpty())
            setData(screenshotClipboardAppearanceNativeMimeType(), std::move(appearance));
        if (!placement.isEmpty())
            setData(screenshotClipboardPlacementNativeMimeType(), std::move(placement));
        setData(QStringLiteral("image/png"), std::move(png));
        if (!filePath.isEmpty())
            setUrls({QUrl::fromLocalFile(filePath)});
    }
    QStringList formats() const override {
        auto result = QMimeData::formats();
        result.append(QStringLiteral("application/x-qt-image"));
        return result;
    }
    bool hasFormat(const QString& mime) const override {
        return mime == QStringLiteral("application/x-qt-image") || QMimeData::hasFormat(mime);
    }

  protected:
    QVariant retrieveData(const QString& mime, QMetaType type) const override {
        const auto activity = snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
        if (mime == QStringLiteral("application/x-qt-image")) {
            return snow_shot::image_codec::decode(data(QStringLiteral("image/png")),
                                                  snow::image::Format::png, "clipboard.png");
        }
        auto data = QMimeData::retrieveData(mime, type);
        if (m_fileUrlRead && mime == QStringLiteral("text/uri-list") && data.isValid())
            m_fileUrlRead->store(true, std::memory_order_relaxed);
        return data;
    }

  private:
    std::shared_ptr<std::atomic_bool> m_fileUrlRead;
};

#endif

constexpr int kMaximumCommitAttempts = 5;
constexpr qint64 kMaximumCommitDurationMs = 300;
constexpr std::array<int, kMaximumCommitAttempts - 1> kCommitRetryDelaysMs{10, 25, 60, 100};
struct ClipboardPublishAttempt final {
    ScreenshotClipboardCommitFailure failure = ScreenshotClipboardCommitFailure::None;
    quint32 nativeError = 0;
    bool superseded = false;
    quint64 revision = 0;
    bool transferred = false;

    [[nodiscard]] bool succeeded() const {
        return failure == ScreenshotClipboardCommitFailure::None && !superseded;
    }
};

// The latest guarded request also observes writes outside the publication service (for
// example QTextEdit::copy or a different application). Native revisions are synchronous,
// even when Qt is still waiting for an OS clipboard-change notification.
struct ClipboardObservation {
    using Id = ScreenshotClipboardService::PublicationId;
    QPointer<QClipboard> clipboard;
    bool native = false;
    quint64 qtRevision = 0;
    quint64 baseline = 0;
    Id request = 0;
    bool cancelled = false;

    quint64 revision() const {
        return native ? screenshotClipboardRevision() : qtRevision;
    }
    void begin(QClipboard* target, Id id) {
        if (target && clipboard != target) {
            clipboard = target;
            const auto platform = QGuiApplication::platformName();
            native = platform == QStringLiteral("windows") || platform == QStringLiteral("cocoa");
            if (!native)
                QObject::connect(target, &QClipboard::dataChanged, target,
                                 [this] { ++qtRevision; });
        }
        request = id;
        cancelled = false;
        baseline = revision();
    }
    bool changed(Id id) const {
        return id == request && (cancelled || revision() != baseline);
    }
    void didPublish(Id id, quint64 publishedRevision) {
        if (id == request)
            baseline = publishedRevision;
    }
};

ClipboardObservation& clipboardObservation() {
    static ClipboardObservation observation;
    return observation;
}

// Qt can synchronously notify listeners that replace the object during setMimeData.
ClipboardPublishAttempt publishQtMimeData(QPointer<QClipboard> clipboard, QMimeData* mime,
                                          ScreenshotClipboardCommitFailure rejected,
                                          const std::atomic_bool* fileUrlRead = nullptr) {
    const auto before = clipboardObservation().revision();
    QPointer<QMimeData> submitted(mime);
    clipboard->setMimeData(mime, QClipboard::Clipboard);
    const auto* current =
        clipboard && submitted ? clipboard->mimeData(QClipboard::Clipboard) : nullptr;
    const auto revision = clipboardObservation().revision();
    if (submitted && current == submitted)
        return {ScreenshotClipboardCommitFailure::None, 0, false, revision, true};
    // A listener can retrieve the file URL and replace the MIME data before setMimeData returns.
    const bool fileDelivered = fileUrlRead && fileUrlRead->load(std::memory_order_relaxed);
    if (!clipboard)
        return {ScreenshotClipboardCommitFailure::ClipboardUnavailable, 0, false, 0, fileDelivered};
    if (fileDelivered || revision != before)
        return {ScreenshotClipboardCommitFailure::None, 0, true, 0, fileDelivered};
    return {rejected};
}

#ifndef Q_OS_WIN
ClipboardPublishAttempt publishPngClipboard(QClipboard* clipboard, const QByteArray& png,
                                            const QByteArray& placement,
                                            const QByteArray& appearance, const QString& filePath) {
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        ensureScreenshotClipboardPlacementMimeSupport();
        ensureScreenshotClipboardAppearanceMimeSupport();
        const auto revision = snow_shot::platform::macos::publishImageClipboard(
            clipboard, png, placement, appearance, filePath);
        if (!revision)
            return {ScreenshotClipboardCommitFailure::PublishFailed};
        return {ScreenshotClipboardCommitFailure::None, 0,
                screenshotClipboardRevision() != *revision, *revision, true};
    }
#endif
    const auto fileUrlRead =
        filePath.isEmpty() ? nullptr : std::make_shared<std::atomic_bool>(false);
    return publishQtMimeData(
        clipboard, new PngClipboardMimeData(png, placement, appearance, filePath, fileUrlRead),
        ScreenshotClipboardCommitFailure::PublishFailed, fileUrlRead.get());
}
#endif

class ClipboardCommitOperation final : public QObject {
  public:
    using Attempt = std::function<ClipboardPublishAttempt()>;

    ClipboardCommitOperation(QObject* receiver,
                             std::shared_ptr<ScreenshotClipboardCommitState> state, Attempt attempt,
                             ScreenshotClipboardService::PublicationId id,
                             ScreenshotClipboardService::CommitCompletion completion)
        : m_receiver(receiver), m_id(id), m_state(std::move(state)), m_attempt(std::move(attempt)),
          m_completion(std::move(completion)) {
        if (receiver != nullptr) {
            connect(receiver, &QObject::destroyed, this, [this]() { finish({}, false); });
        }
    }

    void start(bool immediate = false) {
        m_elapsed.start();
        if (immediate)
            runAttempt();
        else
            QTimer::singleShot(0, this, [this]() { runAttempt(); });
    }

  private:
    void runAttempt() {
        const auto activity = m_activity;
        if (m_finished) {
            return;
        }
        if (m_state->cancelled.load(std::memory_order_acquire)) {
            ScreenshotClipboardCommitResult result;
            result.failure = ScreenshotClipboardCommitFailure::Cancelled;
            result.attempts = m_attempts;
            finish(result, true);
            return;
        }

        ++m_attempts;
        // Publication emits clipboard signals synchronously. A listener can
        // destroy the receiver and finish this operation during the call.
        // Keep the callable and its payload alive until publication returns.
        const QPointer<ClipboardCommitOperation> guardedOperation(this);
        Attempt publish = m_attempt;
        const auto id = m_id;
        ClipboardPublishAttempt attempt =
            clipboardObservation().changed(id)
                ? ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::None, 0, true}
                : publish();
        if (attempt.succeeded())
            clipboardObservation().didPublish(id, attempt.revision);
        if (ScreenshotClipboardService::publicationSuperseded(id) ||
            (attempt.succeeded() && clipboardObservation().changed(id)))
            attempt.superseded = true;
        publish = {};
        if (guardedOperation.isNull() || m_finished) {
            return;
        }
        if (attempt.superseded) {
            ScreenshotClipboardCommitResult result;
            result.attempts = m_attempts;
            result.superseded = true;
            finish(result, true);
            return;
        }
        if (attempt.succeeded()) {
            ScreenshotClipboardCommitResult result;
            result.attempts = m_attempts;
            finish(result, true);
            return;
        }

        const bool retryable = attempt.failure == ScreenshotClipboardCommitFailure::Busy;
        if (retryable && m_attempts < kMaximumCommitAttempts) {
            const int requestedDelay =
                kCommitRetryDelaysMs[static_cast<std::size_t>(m_attempts - 1)];
            const qint64 remaining = kMaximumCommitDurationMs - m_elapsed.elapsed();
            if (remaining > 0) {
                QTimer::singleShot(static_cast<int>((std::min)(remaining, qint64(requestedDelay))),
                                   this, [this]() { runAttempt(); });
                return;
            }
        }

        ScreenshotClipboardCommitResult result;
        result.failure = attempt.failure;
        result.nativeError = attempt.nativeError;
        result.attempts = m_attempts;
        finish(result, true);
    }

    void finish(ScreenshotClipboardCommitResult result, bool notify) {
        if (m_finished) {
            return;
        }
        m_finished = true;
        const auto activity = std::move(m_activity);
        m_attempt = {};
        if (const auto scope = m_state->scope.lock()) {
            scope->pending.remove(m_state.get());
        }
        m_state->scope.reset();
        m_state->finished.store(true, std::memory_order_release);
        const bool cancelled = !notify ||
                               !m_state->completionEnabled.load(std::memory_order_acquire) ||
                               result.failure == ScreenshotClipboardCommitFailure::Cancelled;
        const bool superseded = result.superseded;
        snow_shot::diagnostics::logEvent(
            QStringLiteral("snow_shot.clipboard"), QStringLiteral("clipboard.finished"),
            {{QStringLiteral("operation"), m_operation},
             {QStringLiteral("duration_ms"), m_elapsed.isValid() ? m_elapsed.elapsed() : 0},
             {QStringLiteral("count"), result.attempts},
             {QStringLiteral("code"), static_cast<qint64>(result.nativeError)},
             {QStringLiteral("outcome"), cancelled            ? QStringLiteral("cancelled")
                                         : superseded         ? QStringLiteral("superseded")
                                         : result.succeeded() ? QStringLiteral("succeeded")
                                                              : QStringLiteral("failed")}},
            cancelled || superseded || result.succeeded() ? QtInfoMsg : QtWarningMsg);
        auto completion = std::move(m_completion);
        if (notify && m_state->completionEnabled.load(std::memory_order_acquire) &&
            !m_receiver.isNull() && completion) {
            completion(result);
        }
        deleteLater();
    }

    QPointer<QObject> m_receiver;
    ScreenshotClipboardService::PublicationId m_id;
    std::shared_ptr<ScreenshotClipboardCommitState> m_state;
    Attempt m_attempt;
    ScreenshotClipboardService::CommitCompletion m_completion;
    QElapsedTimer m_elapsed;
    QString m_operation = QUuid::createUuid().toString(QUuid::Id128);
    int m_attempts = 0;
    bool m_finished = false;
    snow_shot::runtime::RuntimeActivityLease m_activity =
        snow_shot::runtime::RuntimeActivityTracker::shared().acquire();
};
} // namespace

#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

namespace snow_shot::presentation::clipboard_detail {
WindowsClipboardFileData encodeWindowsClipboardFiles(const QList<QUrl>& urls) {
    if (urls.isEmpty())
        return {};
    QString paths;
    for (const auto& url : urls) {
        const QString path = QDir::toNativeSeparators(url.toLocalFile());
        if (!url.isLocalFile() || path.isEmpty() || path.contains(QChar(u'\0')))
            return {};
        paths += path;
        paths += QChar(u'\0');
    }
    paths += QChar(u'\0');
    constexpr qsizetype headerBytes = sizeof(DROPFILES);
    if (paths.size() > (std::numeric_limits<qsizetype>::max() - headerBytes) / 2)
        return {};
    WindowsClipboardFileData data;
    data.paths = QByteArray(headerBytes + paths.size() * 2, '\0');
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(data.paths.data(), &header, sizeof(header));
    std::memcpy(data.paths.data() + headerBytes, paths.utf16(), size_t(paths.size()) * 2);
    const QString firstUrl = urls.first().toString();
    data.wideUrl = QByteArray(reinterpret_cast<const char*>(firstUrl.utf16()), firstUrl.size() * 2);
    data.wideUrl.append(2, '\0');
    data.url = firstUrl.toLocal8Bit();
    data.url.append('\0');
    data.preferredDropEffect = QByteArray(sizeof(DWORD), '\0');
    qToLittleEndian(quint32(DROPEFFECT_COPY), data.preferredDropEffect.data());
    return data;
}
} // namespace snow_shot::presentation::clipboard_detail

namespace {
HWND clipboardOwnerWindow() {
    static const HWND owner =
        CreateWindowExW(0, L"STATIC", L"SnowShotClipboardOwner", 0, 0, 0, 0, 0, HWND_MESSAGE,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    return owner;
}

void clearGlobalPadding(HGLOBAL handle, void* memory, SIZE_T payloadBytes) {
    const SIZE_T allocatedBytes = GlobalSize(handle);
    if (allocatedBytes > payloadBytes)
        std::memset(static_cast<uchar*>(memory) + payloadBytes, 0, allocatedBytes - payloadBytes);
}

HGLOBAL copyToGlobal(const QByteArray& bytes) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(bytes.size()));
    if (handle == nullptr)
        return nullptr;
    void* memory = GlobalLock(handle);
    if (memory == nullptr) {
        GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(memory, bytes.constData(), static_cast<std::size_t>(bytes.size()));
    // Clipboard consumers can inspect the complete GlobalSize, including allocator rounding.
    clearGlobalPadding(handle, memory, static_cast<SIZE_T>(bytes.size()));
    GlobalUnlock(handle);
    return handle;
}

HGLOBAL cloneGlobal(HGLOBAL source) {
    const SIZE_T bytes = GlobalSize(source);
    const void* input = bytes ? GlobalLock(source) : nullptr;
    if (!input)
        return nullptr;
    HGLOBAL result = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void* output = result ? GlobalLock(result) : nullptr;
    if (output) {
        std::memcpy(output, input, bytes);
        GlobalUnlock(result);
    } else if (result) {
        GlobalFree(result);
        result = nullptr;
    }
    GlobalUnlock(source);
    return result;
}

class DibWriter final {
  public:
    explicit DibWriter(QSize size, bool opaque) : m_size(size), m_opaque(opaque) {
        if (size.isEmpty())
            return;
        m_stride = (quint64(size.width()) * 3 + 3) & ~quint64{3};
        const quint64 pixelBytes = m_stride * quint64(size.height());
        const quint64 totalBytes = sizeof(BITMAPINFOHEADER) + pixelBytes;
        if (pixelBytes > std::numeric_limits<DWORD>::max() ||
            totalBytes > std::numeric_limits<SIZE_T>::max())
            return;
        m_handle = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(totalBytes));
        if (!m_handle)
            return;
        auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(m_handle));
        if (!header) {
            GlobalFree(m_handle);
            m_handle = nullptr;
            return;
        }
        *header = {};
        clearGlobalPadding(m_handle, header, static_cast<SIZE_T>(totalBytes));
        header->biSize = sizeof(*header);
        header->biWidth = size.width();
        header->biHeight = size.height();
        header->biPlanes = 1;
        header->biBitCount = 24;
        header->biCompression = BI_RGB;
        header->biSizeImage = static_cast<DWORD>(pixelBytes);
        m_pixels = reinterpret_cast<uchar*>(header + 1);
    }
    ~DibWriter() {
        if (m_handle) {
            GlobalUnlock(m_handle);
            GlobalFree(m_handle);
        }
    }
    DibWriter(const DibWriter&) = delete;
    DibWriter& operator=(const DibWriter&) = delete;
    bool isValid() const {
        return m_pixels != nullptr;
    }
    bool write(int first, int count, qsizetype stride, const uchar* input, qsizetype capacity) {
        const qsizetype rowBytes = qsizetype(m_size.width()) * 4;
        if (!isValid() || !input || first < 0 || count <= 0 || count > m_size.height() ||
            first > m_size.height() - count || stride < rowBytes || capacity < rowBytes ||
            qsizetype(count - 1) > (capacity - rowBytes) / stride)
            return false;
        for (int row = 0; row < count; ++row) {
            const auto* source = input + qsizetype(row) * stride;
            auto* output = m_pixels + quint64(m_size.height() - 1 - first - row) * m_stride;
            snow_shot::presentation::clipboard_detail::writeBgr(source, output, m_size.width(),
                                                                m_opaque);
            std::memset(output + qsizetype(m_size.width()) * 3, 0,
                        size_t(m_stride - quint64(m_size.width()) * 3));
        }
        return true;
    }
    HGLOBAL finish() {
        if (!m_handle)
            return nullptr;
        GlobalUnlock(m_handle);
        m_pixels = nullptr;
        SNOW_SHOT_CLIPBOARD_PERF_COUNTER("clipboard.pixel_bytes",
                                         qint64(m_stride) * m_size.height());
        SNOW_SHOT_CLIPBOARD_PERF_COUNTER("clipboard.dib_bytes",
                                         qint64(sizeof(BITMAPINFOHEADER)) +
                                             qint64(m_stride) * m_size.height());
        SNOW_SHOT_CLIPBOARD_PERF_COUNTER("clipboard.dib_prepared", 1);
        return std::exchange(m_handle, nullptr);
    }

  private:
    QSize m_size;
    bool m_opaque = false;
    quint64 m_stride = 0;
    HGLOBAL m_handle = nullptr;
    uchar* m_pixels = nullptr;
};

HGLOBAL prepareDib(const ScreenshotImageRowSource& source) {
    SNOW_SHOT_CLIPBOARD_PERF_SCOPE("clipboard.prepare_dib");
    if (!source.isValid())
        return nullptr;
    DibWriter dib(source.size, source.hasVerifiedOpaquePixels());
    if (!dib.isValid())
        return nullptr;
    constexpr int batchRows = 64;
    const qsizetype rgbaStride = static_cast<qsizetype>(source.size.width()) * 4;
    QByteArray rows(rgbaStride * std::min(batchRows, source.size.height()), Qt::Uninitialized);
    // Bounded scratch space also supports scrolling sources without materializing a QImage.
    bool succeeded = true;
    for (int first = 0; first < source.size.height();) {
        const int count = std::min(batchRows, source.size.height() - first);
        if ((source.cancellationRequested && source.cancellationRequested()) ||
            !source.readRows(first, count, rgbaStride, reinterpret_cast<uchar*>(rows.data()),
                             rows.size()) ||
            !dib.write(first, count, rgbaStride, reinterpret_cast<const uchar*>(rows.constData()),
                       rows.size())) {
            succeeded = false;
            break;
        }
        first += count;
    }
    if (!succeeded || (source.cancellationRequested && source.cancellationRequested())) {
        return nullptr;
    }
    return dib.finish();
}

struct NativeClipboardFormat {
    UINT format;
    void** handle;
};

ClipboardPublishAttempt publishClipboardFormats(std::span<const NativeClipboardFormat> formats) {
    SNOW_SHOT_CLIPBOARD_PERF_SCOPE("clipboard.publish_total");
    const HWND owner = clipboardOwnerWindow();
    if (owner == nullptr || std::any_of(formats.begin(), formats.end(), [](const auto& entry) {
            return *entry.handle && entry.format == 0;
        })) {
        return {ScreenshotClipboardCommitFailure::ClipboardUnavailable, GetLastError()};
    }
    if (!OpenClipboard(owner)) {
        return {ScreenshotClipboardCommitFailure::Busy, GetLastError()};
    }
    if (!EmptyClipboard()) {
        const DWORD error = GetLastError();
        CloseClipboard();
        return {ScreenshotClipboardCommitFailure::ClearFailed, error};
    }
    // Ownership transfers separately for each successful SetClipboardData call.
    bool published = true;
    for (const auto& entry : formats) {
        if (*entry.handle) {
            if (SetClipboardData(entry.format, static_cast<HGLOBAL>(*entry.handle)) == nullptr) {
                published = false;
                break;
            }
            *entry.handle = nullptr;
        }
    }
    const DWORD error = published ? ERROR_SUCCESS : GetLastError();
    if (!published) {
        // Do not leave a partial multi-format publication behind.
        static_cast<void>(EmptyClipboard());
    }
    CloseClipboard();
    if (!published) {
        return {ScreenshotClipboardCommitFailure::PublishFailed, error};
    }
    const auto revision = clipboardObservation().revision();
    SNOW_SHOT_CLIPBOARD_PERF_COUNTER("clipboard.success", 1);
    return {ScreenshotClipboardCommitFailure::None, 0, GetClipboardOwner() != owner, revision,
            true};
}

ClipboardPublishAttempt publishClipboardPayload(void** pngHandle, void** dibHandle,
                                                void** placementHandle, void** appearanceHandle,
                                                void** fileHandle) {
    if (*pngHandle == nullptr || *dibHandle == nullptr)
        return {ScreenshotClipboardCommitFailure::InvalidPayload, ERROR_INVALID_DATA};
    const std::array formats{
        NativeClipboardFormat{RegisterClipboardFormatW(L"PNG"), pngHandle},
        NativeClipboardFormat{CF_DIB, dibHandle},
        NativeClipboardFormat{
            *placementHandle ? RegisterClipboardFormatW(L"SnowShotScreenshotPlacement") : 0,
            placementHandle},
        NativeClipboardFormat{
            *appearanceHandle ? RegisterClipboardFormatW(L"SnowShotScreenshotAppearance") : 0,
            appearanceHandle},
        NativeClipboardFormat{CF_HDROP, fileHandle}};
    return publishClipboardFormats(formats);
}

// Prepare all representations before opening the clipboard. Busy attempts retain their
// handles and a successful SetClipboardData transfers ownership of each handle to Windows.
class NativeClipboardData {
  public:
    NativeClipboardData() = default;
    NativeClipboardData(const NativeClipboardData&) = delete;
    NativeClipboardData& operator=(const NativeClipboardData&) = delete;
    ~NativeClipboardData() {
        for (const auto& entry : m_entries)
            if (entry.second)
                GlobalFree(static_cast<HGLOBAL>(entry.second));
    }
    void add(UINT format, const QByteArray& bytes) {
        auto& entry = m_entries.emplace_back(format, nullptr);
        entry.second = copyToGlobal(bytes);
        m_valid = m_valid && format && entry.second;
    }
    ClipboardPublishAttempt publish() {
        if (!m_valid || m_entries.empty())
            return {ScreenshotClipboardCommitFailure::InvalidPayload, ERROR_INVALID_DATA};
        QVarLengthArray<NativeClipboardFormat, 8> formats;
        for (auto& [format, handle] : m_entries)
            formats.append({format, &handle});
        return publishClipboardFormats({formats.constData(), size_t(formats.size())});
    }

  private:
    std::vector<std::pair<UINT, void*>> m_entries;
    bool m_valid = true;
};

std::shared_ptr<NativeClipboardData> prepareClipboardFiles(const QList<QUrl>& urls,
                                                           const QByteArray& placement,
                                                           const QByteArray& appearance) {
    auto payload = std::make_shared<NativeClipboardData>();
    const auto data = snow_shot::presentation::clipboard_detail::encodeWindowsClipboardFiles(urls);
    if (!data.isValid())
        return {};
    payload->add(CF_HDROP, data.paths);
    payload->add(RegisterClipboardFormatW(L"UniformResourceLocatorW"), data.wideUrl);
    payload->add(RegisterClipboardFormatW(L"UniformResourceLocator"), data.url);
    payload->add(RegisterClipboardFormatW(L"Preferred DropEffect"), data.preferredDropEffect);
    if (!placement.isEmpty())
        payload->add(RegisterClipboardFormatW(L"SnowShotScreenshotPlacement"), placement);
    if (!appearance.isEmpty())
        payload->add(RegisterClipboardFormatW(L"SnowShotScreenshotAppearance"), appearance);
    return payload;
}

QByteArray windowsClipboardHtml(const QString& html) {
    constexpr QByteArrayView startMarker("<!--StartFragment-->");
    constexpr QByteArrayView endMarker("<!--EndFragment-->");
    auto body = html.toUtf8();
    if (!body.contains(startMarker))
        body.prepend(startMarker);
    if (!body.contains(endMarker))
        body.append(endMarker);
    // CF_HTML offsets are byte positions from the start of the entire UTF-8 payload.
    QByteArray header("Version:1.0\r\nStartHTML:0000000000\r\nEndHTML:0000000000\r\n"
                      "StartFragment:0000000000\r\nEndFragment:0000000000\r\n");
    const auto setOffset = [&](QByteArrayView field, qsizetype bodyOffset) {
        const auto digits = QByteArray::number(header.size() + bodyOffset).rightJustified(10, '0');
        header.replace(header.indexOf(field) + field.size(), 10, digits);
    };
    setOffset("StartHTML:", 0);
    setOffset("EndHTML:", body.size());
    setOffset("StartFragment:", body.indexOf(startMarker) + startMarker.size());
    setOffset("EndFragment:", body.indexOf(endMarker));
    return header.append(body).append('\0');
}

std::shared_ptr<NativeClipboardData> prepareNativeMimeData(const QMimeData& mime) {
    const auto placement = screenshotClipboardPlacementNativeMimeType();
    const auto appearance = screenshotClipboardAppearanceNativeMimeType();
    std::shared_ptr<NativeClipboardData> payload;
    if (mime.hasUrls()) {
        const auto urls = mime.urls();
        if (urls.isEmpty() || !std::all_of(urls.cbegin(), urls.cend(),
                                           [](const QUrl& url) { return url.isLocalFile(); }))
            return {};
        payload = prepareClipboardFiles(urls, mime.data(placement), mime.data(appearance));
        if (!payload)
            return std::make_shared<NativeClipboardData>(); // Invalid local-file payload.
    } else {
        if (!mime.hasText() && !mime.hasHtml())
            return {};
        payload = std::make_shared<NativeClipboardData>();
    }
    for (const auto& format : mime.formats()) {
        if (mime.hasUrls() && (format == QStringLiteral("text/uri-list") || format == placement ||
                               format == appearance))
            continue;
        if (format == QStringLiteral("text/plain")) {
            auto text = mime.text();
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            text.replace(u'\n', QStringLiteral("\r\n"));
            payload->add(CF_UNICODETEXT,
                         QByteArray(reinterpret_cast<const char*>(text.utf16()),
                                    (text.size() + 1) * qsizetype(sizeof(char16_t))));
        } else if (format == QStringLiteral("text/html")) {
            payload->add(RegisterClipboardFormatW(L"HTML Format"),
                         windowsClipboardHtml(mime.html()));
        } else if (format.startsWith(QStringLiteral("application/x-snow-shot-"))) {
            payload->add(RegisterClipboardFormatW(qUtf16Printable(format)), mime.data(format));
        } else {
            // Keep Qt's converters for images and other platform-specific representations.
            return {};
        }
    }
    return payload;
}
} // namespace
#endif

ScreenshotClipboardService::PublicationId
ScreenshotClipboardService::reservePublication(QClipboard* clipboard) {
    const auto id = reservePublication();
    clipboardObservation().begin(clipboard, id);
    return id;
}

void ScreenshotClipboardService::cancelPublication(PublicationId id) {
    auto& observation = clipboardObservation();
    if (id && id == observation.request)
        observation.cancelled = true;
}

bool ScreenshotClipboardService::publicationCurrent(PublicationId id) {
    return id != 0 && !publicationSuperseded(id) && !clipboardObservation().changed(id);
}

QString ScreenshotClipboardCommitResult::errorString() const {
    switch (failure) {
    case ScreenshotClipboardCommitFailure::None:
        return {};
    case ScreenshotClipboardCommitFailure::Cancelled:
        return QCoreApplication::translate("ScreenshotClipboardService",
                                           "The clipboard operation was cancelled");
    case ScreenshotClipboardCommitFailure::InvalidPayload:
        return QCoreApplication::translate("ScreenshotClipboardService",
                                           "The prepared clipboard image is invalid");
    case ScreenshotClipboardCommitFailure::ClipboardUnavailable:
        return QCoreApplication::translate("ScreenshotClipboardService",
                                           "The clipboard is unavailable");
    case ScreenshotClipboardCommitFailure::Busy:
        return QCoreApplication::translate("ScreenshotClipboardService", "The clipboard is busy");
    case ScreenshotClipboardCommitFailure::ClearFailed:
        return QCoreApplication::translate("ScreenshotClipboardService",
                                           "The clipboard could not be cleared");
    case ScreenshotClipboardCommitFailure::PublishFailed:
        return QCoreApplication::translate("ScreenshotClipboardService",
                                           "The clipboard did not accept the image");
    }
    return QCoreApplication::translate("ScreenshotClipboardService",
                                       "The clipboard operation failed");
}

ScreenshotClipboardCommitHandle::ScreenshotClipboardCommitHandle(
    std::shared_ptr<ScreenshotClipboardCommitState> state)
    : m_state(std::move(state)) {}

void ScreenshotClipboardCommitHandle::cancel() const {
    if (m_state != nullptr) {
        m_state->cancelled.store(true, std::memory_order_release);
    }
}

bool ScreenshotClipboardCommitHandle::isValid() const {
    return m_state != nullptr;
}

bool ScreenshotClipboardCommitHandle::isCancellationRequested() const {
    return !isValid() || m_state->cancelled.load(std::memory_order_acquire);
}

bool ScreenshotClipboardCommitHandle::isFinished() const {
    return !isValid() || m_state->finished.load(std::memory_order_acquire);
}

ScreenshotClipboardCommitScope::~ScreenshotClipboardCommitScope() {
    cancelAll();
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardCommitScope::commit(QClipboard* clipboard, QObject* receiver,
                                       ScreenshotClipboardPayload payload,
                                       ScreenshotClipboardService::CommitCompletion completion,
                                       ScreenshotClipboardService::PublicationId publicationId) {
    if (publicationId == 0) {
        publicationId = ScreenshotClipboardService::reservePublication();
    }
    auto handle = ScreenshotClipboardService::commit(clipboard, receiver, std::move(payload),
                                                     publicationId, std::move(completion));
    track(handle);
    return handle;
}

ScreenshotClipboardCommitHandle ScreenshotClipboardCommitScope::commitMimeData(
    QClipboard* clipboard, QObject* receiver, QMimeData* mimeData,
    ScreenshotClipboardService::CommitCompletion completion,
    ScreenshotClipboardService::PublicationId publicationId) {
    if (publicationId == 0) {
        publicationId = ScreenshotClipboardService::reservePublication();
    }
    auto handle = ScreenshotClipboardService::commitMimeData(clipboard, receiver, mimeData,
                                                             publicationId, std::move(completion));
    track(handle);
    return handle;
}

void ScreenshotClipboardCommitScope::track(const ScreenshotClipboardCommitHandle& handle) {
    if (handle.isFinished()) {
        return;
    }
    if (!m_state) {
        m_state = std::make_shared<ScreenshotClipboardCommitScopeState>();
    }
    handle.m_state->scope = m_state;
    m_state->pending.insert(handle.m_state.get(), handle.m_state);
}

void ScreenshotClipboardCommitScope::cancelAll() {
    // Detach the batch so a new publication cannot inherit cancellation or be
    // removed when an older cancelled operation eventually finishes.
    const auto state = std::exchange(m_state, {});
    if (!state) {
        return;
    }
    for (const auto& commit : state->pending) {
        commit->completionEnabled.store(false, std::memory_order_release);
        commit->cancelled.store(true, std::memory_order_release);
        commit->scope.reset();
    }
    state->pending.clear();
}

qsizetype ScreenshotClipboardCommitScope::pendingCount() const {
    return m_state ? m_state->pending.size() : 0;
}

ScreenshotClipboardPayload::~ScreenshotClipboardPayload() {
    reset();
}

void ScreenshotClipboardPayload::reset() noexcept {
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (m_dibHandle != nullptr)
        GlobalFree(static_cast<HGLOBAL>(m_dibHandle));
    if (m_pngHandle != nullptr)
        GlobalFree(static_cast<HGLOBAL>(m_pngHandle));
    if (m_placementHandle != nullptr)
        GlobalFree(static_cast<HGLOBAL>(m_placementHandle));
    m_dibHandle = nullptr;
    m_pngHandle = nullptr;
    if (m_appearanceHandle != nullptr)
        GlobalFree(static_cast<HGLOBAL>(m_appearanceHandle));
    m_placementHandle = nullptr;
    m_appearanceHandle = nullptr;
    if (m_fileHandle != nullptr)
        GlobalFree(static_cast<HGLOBAL>(m_fileHandle));
    m_fileHandle = nullptr;
#endif
    m_pngBytes.clear();
    m_placementBytes.clear();
    m_appearanceBytes.clear();
    m_filePath.clear();
    m_filePublished = {};
}

void ScreenshotClipboardPayload::confirmFilePublication() {
    if (auto published = std::exchange(m_filePublished, {}))
        published();
}

ScreenshotClipboardPayload::ScreenshotClipboardPayload(
    ScreenshotClipboardPayload&& other) noexcept {
    *this = std::move(other);
}

ScreenshotClipboardPayload&
ScreenshotClipboardPayload::operator=(ScreenshotClipboardPayload&& other) noexcept {
    if (this == &other)
        return *this;
    reset();
#if defined(Q_OS_WIN) || defined(_WIN32)
    m_dibHandle = std::exchange(other.m_dibHandle, nullptr);
    m_pngHandle = std::exchange(other.m_pngHandle, nullptr);
    m_placementHandle = std::exchange(other.m_placementHandle, nullptr);
    m_appearanceHandle = std::exchange(other.m_appearanceHandle, nullptr);
    m_fileHandle = std::exchange(other.m_fileHandle, nullptr);
#endif
    m_pngBytes = std::move(other.m_pngBytes);
    m_placementBytes = std::move(other.m_placementBytes);
    m_appearanceBytes = std::move(other.m_appearanceBytes);
    m_filePath = std::move(other.m_filePath);
    m_filePublished = std::exchange(other.m_filePublished, {});
    return *this;
}

bool ScreenshotClipboardPayload::isValid() const {
#if defined(Q_OS_WIN) || defined(_WIN32)
    return m_dibHandle != nullptr && m_pngHandle != nullptr && !m_pngBytes.isEmpty() &&
           (m_placementBytes.isEmpty() || m_placementHandle != nullptr) &&
           (m_appearanceBytes.isEmpty() || m_appearanceHandle != nullptr) &&
           (m_filePath.isEmpty() || m_fileHandle != nullptr);
#else
    return !m_pngBytes.isEmpty();
#endif
}

ScreenshotClipboardPayload
ScreenshotClipboardService::prepare(const ScreenshotImageRowSource& source,
                                    ScreenshotImageEncodingOptions encoding,
                                    std::optional<ScreenshotClipboardPlacement> placement,
                                    std::optional<ScreenshotClipboardAppearance> appearance) {
    SNOW_SHOT_CLIPBOARD_PERF_SCOPE("clipboard.prepare_total");
    if (!source.isValid() || (source.cancellationRequested && source.cancellationRequested()))
        return {};
    ScreenshotImageRowSource input = source;
    input.isKnownOpaque = source.hasVerifiedOpaquePixels();
#if defined(Q_OS_WIN) || defined(_WIN32)
    DibWriter dib(source.size, input.isKnownOpaque);
    if (dib.isValid()) {
        input.readRows = [&source, &dib](int first, int count, qsizetype stride, uchar* destination,
                                         qsizetype capacity) {
            return source.readRows(first, count, stride, destination, capacity) &&
                   dib.write(first, count, stride, destination, capacity);
        };
    }
#endif
    QByteArray png;
    {
        SNOW_SHOT_CLIPBOARD_PERF_SCOPE("clipboard.encode_png");
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) ||
            !snow_shot::image_codec::encodeToDevice(
                input, &buffer, snow::image::Format::png,
                ScreenshotImageFileService::encodeOptions(ScreenshotImageFileFormat::Png,
                                                          encoding))) {
            return {};
        }
    }
    SNOW_SHOT_CLIPBOARD_PERF_COUNTER("clipboard.png_encoded", 1);
    ScreenshotClipboardPayload payload;
    payload.m_pngBytes = std::move(png);
    if (placement) {
        placement->rasterSize = source.size;
        payload.m_placementBytes = encodeScreenshotClipboardPlacement(*placement);
    }
    if (appearance) {
        appearance->rasterSize = source.size;
        payload.m_appearanceBytes = encodeScreenshotClipboardAppearance(*appearance);
    }
#if defined(Q_OS_WIN) || defined(_WIN32)
    payload.m_dibHandle = dib.finish();
    // Retain successfully encoded bytes even if native allocation fails. Concurrent PNG
    // consumers can still use their encoding; the clipboard payload reports invalid ownership.
    if (payload.m_dibHandle) {
        payload.m_pngHandle = copyToGlobal(payload.m_pngBytes);
        if (!payload.m_placementBytes.isEmpty())
            payload.m_placementHandle = copyToGlobal(payload.m_placementBytes);
        if (!payload.m_appearanceBytes.isEmpty())
            payload.m_appearanceHandle = copyToGlobal(payload.m_appearanceBytes);
    }
#endif
    if (source.cancellationRequested && source.cancellationRequested())
        return {};
    return payload;
}

ScreenshotClipboardPayload
ScreenshotClipboardService::clonePrepared(const ScreenshotClipboardPayload& source) {
    if (!source.isValid())
        return {};
    ScreenshotClipboardPayload result;
    result.m_pngBytes = source.m_pngBytes;
    result.m_placementBytes = source.m_placementBytes;
    result.m_appearanceBytes = source.m_appearanceBytes;
    result.m_filePath = source.m_filePath;
    result.m_filePublished = source.m_filePublished;
#if defined(Q_OS_WIN) || defined(_WIN32)
    result.m_dibHandle = cloneGlobal(static_cast<HGLOBAL>(source.m_dibHandle));
    result.m_pngHandle = copyToGlobal(result.m_pngBytes);
    if (!result.m_placementBytes.isEmpty())
        result.m_placementHandle = copyToGlobal(result.m_placementBytes);
    if (!result.m_appearanceBytes.isEmpty())
        result.m_appearanceHandle = copyToGlobal(result.m_appearanceBytes);
    if (source.m_fileHandle)
        result.m_fileHandle = cloneGlobal(static_cast<HGLOBAL>(source.m_fileHandle));
#endif
    return result;
}

ScreenshotClipboardPayload ScreenshotClipboardService::withFile(ScreenshotClipboardPayload payload,
                                                                const QString& path,
                                                                std::function<void()> published) {
    if (!payload.isValid() || !QDir::isAbsolutePath(path) || path.contains(QChar::Null))
        return {};
    payload.m_filePath = QDir::fromNativeSeparators(path);
#if defined(Q_OS_WIN) || defined(_WIN32)
    const QString nativePath = QDir::toNativeSeparators(path);
    // CF_HDROP carries a double-NUL-terminated UTF-16 list after DROPFILES.
    QByteArray bytes(sizeof(DROPFILES) + (nativePath.size() + 2) * sizeof(wchar_t), '\0');
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), nativePath.utf16(),
                static_cast<size_t>(nativePath.size()) * sizeof(wchar_t));
    if (payload.m_fileHandle)
        GlobalFree(static_cast<HGLOBAL>(payload.m_fileHandle));
    payload.m_fileHandle = copyToGlobal(bytes);
    if (!payload.isValid())
        return {};
#endif
    payload.m_filePublished = std::move(published);
    return payload;
}

ScreenshotClipboardPayload ScreenshotClipboardService::prepareEncoded(
    const ScreenshotImageRowSource& source, const QByteArray& png,
    std::optional<ScreenshotClipboardPlacement> placement,
    std::optional<ScreenshotClipboardAppearance> appearance) {
    SNOW_SHOT_CLIPBOARD_PERF_SCOPE("clipboard.prepare_native");
    if (!source.isValid() || png.isEmpty() ||
        (source.cancellationRequested && source.cancellationRequested())) {
        return {};
    }
    ScreenshotClipboardPayload payload;
    if (placement) {
        placement->rasterSize = source.size;
        payload.m_placementBytes = encodeScreenshotClipboardPlacement(*placement);
    }
    if (appearance) {
        appearance->rasterSize = source.size;
        payload.m_appearanceBytes = encodeScreenshotClipboardAppearance(*appearance);
    }
    payload.m_pngBytes = png;
#if defined(Q_OS_WIN) || defined(_WIN32)
    payload.m_dibHandle = prepareDib(source);
    if (payload.m_dibHandle == nullptr)
        return {};
    payload.m_pngHandle = copyToGlobal(payload.m_pngBytes);
    if (!payload.m_placementBytes.isEmpty())
        payload.m_placementHandle = copyToGlobal(payload.m_placementBytes);
    if (!payload.m_appearanceBytes.isEmpty())
        payload.m_appearanceHandle = copyToGlobal(payload.m_appearanceBytes);
#endif
    if (source.cancellationRequested && source.cancellationRequested())
        return {};
    return payload;
}

ScreenshotClipboardPayload
ScreenshotClipboardService::prepareImage(const QImage& image,
                                         ScreenshotImageEncodingOptions encoding,
                                         std::optional<ScreenshotClipboardPlacement> placement,
                                         std::optional<ScreenshotClipboardAppearance> appearance) {
    return prepare(snow_shot::image_codec::srgbRowSource(image), encoding, std::move(placement),
                   std::move(appearance));
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardService::commit(QClipboard* clipboard, QObject* receiver,
                                   ScreenshotClipboardPayload payload,
                                   CommitCompletion completion) {
    return commit(clipboard, receiver, std::move(payload), reservePublication(),
                  std::move(completion));
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardService::commit(QClipboard* clipboard, QObject* receiver,
                                   ScreenshotClipboardPayload payload, PublicationId publicationId,
                                   CommitCompletion completion) {
    QCoreApplication* application = QCoreApplication::instance();
    if (receiver == nullptr || !completion || application == nullptr ||
        QThread::currentThread() != application->thread()) {
        return {};
    }

    auto state = std::make_shared<ScreenshotClipboardCommitState>();
    auto sharedPayload = std::make_shared<ScreenshotClipboardPayload>(std::move(payload));
#if defined(Q_OS_WIN) || defined(_WIN32)
    Q_UNUSED(clipboard);
    auto attempt = [sharedPayload, publicationId]() {
        if (!claimPublication(publicationId)) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::None, 0, true};
        }
        if (!sharedPayload->isValid())
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::InvalidPayload, 0};
        const auto result = publishClipboardPayload(
            &sharedPayload->m_pngHandle, &sharedPayload->m_dibHandle,
            &sharedPayload->m_placementHandle, &sharedPayload->m_appearanceHandle,
            &sharedPayload->m_fileHandle);
        if (result.transferred)
            sharedPayload->confirmFilePublication();
        return result;
    };
#else
    const QPointer<QClipboard> guardedClipboard(clipboard);
    auto attempt = [guardedClipboard, sharedPayload, publicationId]() {
        if (!claimPublication(publicationId)) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::None, 0, true};
        }
        if (guardedClipboard.isNull()) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::ClipboardUnavailable,
                                           0};
        }
        if (!sharedPayload->isValid()) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::InvalidPayload, 0};
        }
        const auto result = publishPngClipboard(
            guardedClipboard, sharedPayload->m_pngBytes, sharedPayload->m_placementBytes,
            sharedPayload->m_appearanceBytes, sharedPayload->m_filePath);
        if (result.transferred)
            sharedPayload->confirmFilePublication();
        sharedPayload->reset();
        return result;
    };
#endif
    auto* operation = new ClipboardCommitOperation(receiver, state, std::move(attempt),
                                                   publicationId, std::move(completion));
    operation->start();
    return ScreenshotClipboardCommitHandle(std::move(state));
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardService::publishMimeData(QClipboard* clipboard, QObject* receiver,
                                            std::unique_ptr<QMimeData> mime, PublicationId id,
                                            CommitCompletion completion) {
    return startMimeCommit(clipboard, receiver, std::move(mime), id, std::move(completion), true);
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardService::commitMimeData(QClipboard* clipboard, QObject* receiver,
                                           QMimeData* mimeData, CommitCompletion completion) {
    return commitMimeData(clipboard, receiver, mimeData, reservePublication(),
                          std::move(completion));
}

ScreenshotClipboardCommitHandle
ScreenshotClipboardService::commitMimeData(QClipboard* clipboard, QObject* receiver,
                                           QMimeData* mimeData, PublicationId publicationId,
                                           CommitCompletion completion) {
    return startMimeCommit(clipboard, receiver, std::unique_ptr<QMimeData>(mimeData), publicationId,
                           std::move(completion), false);
}

ScreenshotClipboardCommitHandle ScreenshotClipboardService::startMimeCommit(
    QClipboard* clipboard, QObject* receiver, std::unique_ptr<QMimeData> mime,
    PublicationId publicationId, CommitCompletion completion, bool immediate) {
    QCoreApplication* application = QCoreApplication::instance();
    if (!receiver || !mime || !completion || !application ||
        QThread::currentThread() != application->thread())
        return {};

    const QPointer<QObject> guardedReceiver(receiver);
    const QPointer<QClipboard> guardedClipboard(clipboard);
    auto state = std::make_shared<ScreenshotClipboardCommitState>();
    const auto* mimeData = mime.get();
    auto holder = std::make_shared<std::unique_ptr<QMimeData>>(std::move(mime));
    const auto placementFormat = screenshotClipboardPlacementNativeMimeType();
    const auto appearanceFormat = screenshotClipboardAppearanceNativeMimeType();
    const auto formats = mimeData->formats();
    const bool fileOnly = mimeData->hasUrls() &&
                          std::all_of(formats.begin(), formats.end(),
                                      [&placementFormat, &appearanceFormat](const QString& format) {
                                          return format == QStringLiteral("text/uri-list") ||
                                                 format == placementFormat ||
                                                 format == appearanceFormat;
                                      });
    const QList<QUrl> fileUrls = fileOnly ? mimeData->urls() : QList<QUrl>{};
    const QByteArray placementBytes = fileOnly ? mimeData->data(placementFormat) : QByteArray{};
    const QByteArray appearanceBytes = fileOnly ? mimeData->data(appearanceFormat) : QByteArray{};
    ClipboardCommitOperation::Attempt nativePublication;
#if defined(Q_OS_WIN) || defined(_WIN32)
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        // Qt's OleSetClipboard sleeps on the GUI thread when another process holds the clipboard.
        if (auto native = prepareNativeMimeData(*mimeData)) {
            holder->reset();
            nativePublication = [native = std::move(native)] { return native->publish(); };
        }
    }
#endif
    // Reading a lazy MIME object or releasing it after native conversion can run user code.
    if (!guardedReceiver)
        return {};
    auto attempt = [guardedClipboard, holder, publicationId, fileUrls, placementFormat,
                    placementBytes, appearanceFormat, appearanceBytes,
                    nativePublication = std::move(nativePublication)]() {
        if (!claimPublication(publicationId)) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::None, 0, true};
        }
        if (guardedClipboard.isNull()) {
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::ClipboardUnavailable,
                                           0};
        }
        if (nativePublication)
            return nativePublication();
        if (!*holder)
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::InvalidPayload};
        if (!fileUrls.isEmpty()) {
            // Qt owns each attempted MIME object, including failed native publications.
            auto* attemptMime = new QMimeData();
            attemptMime->setUrls(fileUrls);
            if (!placementBytes.isEmpty())
                attemptMime->setData(placementFormat, placementBytes);
            if (!appearanceBytes.isEmpty())
                attemptMime->setData(appearanceFormat, appearanceBytes);
            return publishQtMimeData(guardedClipboard, attemptMime,
                                     ScreenshotClipboardCommitFailure::Busy);
        }
        if ((*holder)->formats().isEmpty())
            return ClipboardPublishAttempt{ScreenshotClipboardCommitFailure::InvalidPayload};
        return publishQtMimeData(guardedClipboard, holder->release(),
                                 ScreenshotClipboardCommitFailure::PublishFailed);
    };
    auto* operation = new ClipboardCommitOperation(guardedReceiver, state, std::move(attempt),
                                                   publicationId, std::move(completion));
    operation->start(immediate);
    return ScreenshotClipboardCommitHandle(std::move(state));
}

bool ScreenshotClipboardService::publish(QClipboard* clipboard,
                                         ScreenshotClipboardPayload payload) {
    static_cast<void>(reservePublication());
    if (!payload.isValid())
        return false;
#if defined(Q_OS_WIN) || defined(_WIN32)
    Q_UNUSED(clipboard);
    const auto result = publishClipboardPayload(&payload.m_pngHandle, &payload.m_dibHandle,
                                                &payload.m_placementHandle,
                                                &payload.m_appearanceHandle, &payload.m_fileHandle);
#else
    if (clipboard == nullptr || !payload.isValid()) {
        qWarning("Screenshot clipboard is unavailable");
        return false;
    }
    const auto result = publishPngClipboard(clipboard, payload.m_pngBytes, payload.m_placementBytes,
                                            payload.m_appearanceBytes, payload.m_filePath);
#endif
    if (result.transferred)
        payload.confirmFilePublication();
    return result.succeeded();
}

bool ScreenshotClipboardService::publishImage(QClipboard* clipboard, const QImage& image,
                                              ScreenshotImageEncodingOptions encoding) {
    return publish(clipboard, prepareImage(image, encoding));
}
