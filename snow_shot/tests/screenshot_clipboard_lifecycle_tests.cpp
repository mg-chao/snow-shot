#include "snow_shot/presentation/screenshotclipboardservice.h"
#include "snow_shot/storage/clipboardfilecache.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QEventLoop>
#include <QMimeData>
#include <QPointer>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QProcess>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void commitCompletionMarksFinishedAndReleasesInputs() {
    QObject receiver;
    for (bool cancel : {false, true}) {
        QEventLoop loop;
        auto* mime = new QMimeData;
        mime->setText(QStringLiteral("clipboard lifetime fixture"));
        const QPointer<QMimeData> input(mime);
        ScreenshotClipboardCommitHandle handle;
        bool completed = false;
        handle = ScreenshotClipboardService::commitMimeData(
            QApplication::clipboard(), &receiver, mime,
            [&](ScreenshotClipboardCommitResult result) {
                require(handle.isFinished(),
                        "a completion callback must observe a finished handle");
                require(cancel ? result.failure == ScreenshotClipboardCommitFailure::Cancelled
                               : result.succeeded(),
                        "commit must report its actual outcome");
                if (cancel)
                    require(input.isNull(), "cancelled input must be released before completion");
                completed = true;
                loop.quit();
            });
        require(handle.isValid() && !handle.isFinished(), "a queued commit must remain pending");
        if (cancel) {
            handle.cancel();
            require(handle.isCancellationRequested() && !handle.isFinished(),
                    "requesting cancellation must not prematurely report completion");
        }
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        require(completed, "clipboard commit completion timed out");
        QApplication::clipboard()->clear();
    }
}

void receiverDestroyedDuringMimePreparation() {
    class LazyMime final : public QMimeData {
      public:
        explicit LazyMime(QObject* receiver) : owner(receiver) {}
        QStringList formats() const override {
            delete owner.data();
            return QMimeData::formats();
        }

      private:
        QPointer<QObject> owner;
    };
    QPointer<QObject> receiver = new QObject;
    auto mime = std::make_unique<LazyMime>(receiver);
    bool completed = false;
    const auto handle = ScreenshotClipboardService::publishMimeData(
        QApplication::clipboard(), receiver, std::move(mime),
        ScreenshotClipboardService::reservePublication(), [&](auto) { completed = true; });
    require(!receiver && !handle.isValid() && !completed,
            "MIME preparation can retire the receiver before a clipboard attempt starts");
}

void abandonedReceiversReleaseInputsAndFinishHandles() {
    auto* receiver = new QObject;
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("abandoned commit"));
    const QPointer<QMimeData> input(mime);
    bool called = false;
    auto handle = ScreenshotClipboardService::commitMimeData(
        QApplication::clipboard(), receiver, mime,
        [&](ScreenshotClipboardCommitResult) { called = true; });
    delete receiver;
    require(handle.isFinished() && input.isNull(),
            "destroying the receiver must release the queued input and finish its handle");
    QCoreApplication::processEvents();
    require(!called, "destroyed receivers must not receive completion callbacks");
}

QString retainedClipboardFile(snow_shot::storage::ClipboardFileCache& cache) {
    QString error;
    auto directory = cache.createDirectory(&error);
    require(bool(directory), "clipboard export directory must be available");
    const QString path = QDir(directory->path()).filePath(QStringLiteral("image.png"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write("image") == 5,
            "clipboard file fixture must be writable");
    file.close();
    directory->retain();
    return path;
}

void clipboardFilesOutlivePublications() {
    QTemporaryDir root;
    require(root.isValid(), "clipboard cache fixture root must be available");
    snow_shot::storage::ClipboardFileCache cache(root.path());
    QString previousPath;
    for (bool cancel : {false, true}) {
        auto receiver = std::make_unique<QObject>();
        const QString path = retainedClipboardFile(cache);
        auto mime = std::make_unique<QMimeData>();
        mime->setUrls({QUrl::fromLocalFile(path)});
        QEventLoop loop;
        bool completed = false;
        const auto handle = ScreenshotClipboardService::commitMimeData(
            QApplication::clipboard(), receiver.get(), mime.release(),
            [&](ScreenshotClipboardCommitResult result) {
                require(cancel ? result.failure == ScreenshotClipboardCommitFailure::Cancelled
                               : result.succeeded(),
                        "temporary file publication must report its actual outcome");
                completed = true;
                loop.quit();
            });
        require(handle.isValid() && QFileInfo::exists(path),
                "queued publication must retain its temporary file");
        if (cancel)
            handle.cancel();
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        require(completed, "temporary file publication timed out");
        receiver.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!cancel) {
            require(QFileInfo::exists(path) && QApplication::clipboard()->mimeData()->urls() ==
                                                   QList<QUrl>{QUrl::fromLocalFile(path)},
                    "published files must outlive the export receiver and commit operation");
            QApplication::clipboard()->clear();
        }
        require(QFileInfo::exists(path),
                "clipboard replacement and cancellation must preserve completed file exports");
        if (!previousPath.isEmpty())
            require(QFileInfo::exists(previousPath),
                    "subsequent exports must preserve files pasted from earlier publications");
        previousPath = path;
    }
}

void clipboardFilesSurviveReplacementAndApplicationExit() {
    QTemporaryDir directory;
    require(directory.isValid(), "shutdown clipboard fixture must be available");
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--clipboard-file-shutdown"), directory.path(),
                 QStringLiteral("-platform"), QStringLiteral("offscreen")});
    require(child.waitForStarted(2000) && child.waitForFinished(5000) &&
                child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
            "temporary clipboard shutdown process must complete successfully");
    const QString root = QDir::fromNativeSeparators(directory.path());
    const auto paths = QString::fromUtf8(child.readAllStandardOutput()).trimmed().split(u'\n');
    require(paths.size() == 2, "shutdown fixture must publish two distinct clipboard files");
    for (const auto& line : paths) {
        const QString path = line.trimmed();
        require(QDir::fromNativeSeparators(path).startsWith(root + u'/') && QFileInfo::exists(path),
                "application exit must preserve current and replaced clipboard files");
    }
}

void failedCommitsFinishHandles() {
    QObject receiver;
    QEventLoop loop;
    ScreenshotClipboardCommitHandle handle;
    bool completed = false;
    handle = ScreenshotClipboardService::commit(
        QApplication::clipboard(), &receiver, {}, [&](ScreenshotClipboardCommitResult result) {
            require(handle.isFinished(), "failed commits must finish before their callback");
            require(result.failure == ScreenshotClipboardCommitFailure::InvalidPayload,
                    "an empty payload must be rejected");
            completed = true;
            loop.quit();
        });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    require(completed, "invalid payload completion timed out");
}

void scopedCommitsReleaseTrackingAndCallbacksOnEveryOutcome() {
    ScreenshotClipboardCommitScope scope;
    QObject receiver;
    for (int cycle = 0; cycle < 32; ++cycle) {
        for (int outcome = 0; outcome < 3; ++outcome) {
            QEventLoop loop;
            bool completed = false;
            ScreenshotClipboardCommitHandle handle;
            auto resource = std::make_shared<int>(cycle);
            const std::weak_ptr<int> lifetime(resource);
            auto completion = [&, resource](ScreenshotClipboardCommitResult result) {
                require(scope.pendingCount() == 0 && handle.isFinished(),
                        "every terminal commit must leave its scope before completion");
                const auto expected = outcome == 1
                                          ? ScreenshotClipboardCommitFailure::InvalidPayload
                                      : outcome == 2 ? ScreenshotClipboardCommitFailure::Cancelled
                                                     : ScreenshotClipboardCommitFailure::None;
                require(result.failure == expected, "scoped commits must preserve their outcome");
                require(*resource == cycle, "the callback must retain its inputs while running");
                completed = true;
                loop.quit();
            };
            if (outcome == 1) {
                handle =
                    scope.commit(QApplication::clipboard(), &receiver, {}, std::move(completion));
            } else {
                auto* mime = new QMimeData;
                mime->setText(QStringLiteral("scoped clipboard fixture"));
                handle = scope.commitMimeData(QApplication::clipboard(), &receiver, mime,
                                              std::move(completion));
            }
            resource.reset();
            require(scope.pendingCount() == 1 && handle.isValid(),
                    "a scope must own every pending publication");
            if (outcome == 2) {
                handle.cancel();
            }
            QTimer::singleShot(2000, &loop, &QEventLoop::quit);
            loop.exec();
            require(
                completed && lifetime.expired(),
                "finished commits must release callbacks without waiting for deferred deletion");
            QApplication::clipboard()->clear();
        }
    }
}

void scopedAbandonedAndRejectedCommitsReleaseOwnership() {
    ScreenshotClipboardCommitScope scope;
    auto* receiver = new QObject;
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("scoped abandoned input"));
    const QPointer<QMimeData> input(mime);
    auto resource = std::make_shared<int>(1);
    const std::weak_ptr<int> lifetime(resource);
    bool called = false;
    const auto handle =
        scope.commitMimeData(QApplication::clipboard(), receiver, mime,
                             [&, resource](ScreenshotClipboardCommitResult) { called = true; });
    resource.reset();
    delete receiver;
    require(scope.pendingCount() == 0 && handle.isFinished() && input.isNull() &&
                lifetime.expired(),
            "abandoned receivers must retire all scoped input and callback ownership immediately");
    require(!called, "abandoned scoped commits must suppress callbacks");

    mime = new QMimeData;
    const QPointer<QMimeData> rejectedInput(mime);
    const auto rejected =
        scope.commitMimeData(QApplication::clipboard(), nullptr, mime,
                             [&](ScreenshotClipboardCommitResult) { called = true; });
    require(!rejected.isValid() && rejectedInput.isNull() && scope.pendingCount() == 0,
            "rejected publications must not enter the scope or retain their inputs");
}

void scopedCancellationRetiresOnlyTheCancelledBatch() {
    ScreenshotClipboardCommitScope scope;
    auto* oldReceiver = new QObject;
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("cancelled batch"));
    bool oldCalled = false;
    const auto old =
        scope.commitMimeData(QApplication::clipboard(), oldReceiver, mime,
                             [&](ScreenshotClipboardCommitResult) { oldCalled = true; });
    scope.cancelAll();
    scope.cancelAll();
    require(scope.pendingCount() == 0 && old.isCancellationRequested(),
            "batch cancellation must release tracking and request cancellation");

    QObject receiver;
    QEventLoop loop;
    bool completed = false;
    mime = new QMimeData;
    mime->setText(QStringLiteral("new batch"));
    const auto current = scope.commitMimeData(
        QApplication::clipboard(), &receiver, mime, [&](ScreenshotClipboardCommitResult result) {
            require(result.succeeded() && scope.pendingCount() == 0,
                    "a new batch must complete independently of its cancelled predecessor");
            completed = true;
            loop.quit();
        });
    delete oldReceiver;
    require(scope.pendingCount() == 1 && old.isFinished(),
            "finishing an old batch must not remove a current operation");
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    require(completed && current.isFinished() && !oldCalled &&
                QApplication::clipboard()->text() == QStringLiteral("new batch"),
            "scope reuse must publish only the new batch and suppress cancelled callbacks");
    QApplication::clipboard()->clear();
}

void scopeDestructionCancelsWithoutAffectingOtherScopes() {
    QObject receiver;
    auto scope = std::make_unique<ScreenshotClipboardCommitScope>();
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("destroyed owner"));
    const QPointer<QMimeData> input(mime);
    bool called = false;
    const auto abandoned =
        scope->commitMimeData(QApplication::clipboard(), &receiver, mime,
                              [&](ScreenshotClipboardCommitResult) { called = true; });
    scope.reset();
    require(abandoned.isCancellationRequested(), "scope destruction must cancel its pending work");

    ScreenshotClipboardCommitScope survivor;
    QEventLoop loop;
    bool completed = false;
    mime = new QMimeData;
    mime->setText(QStringLiteral("surviving owner"));
    const auto live = survivor.commitMimeData(
        QApplication::clipboard(), &receiver, mime, [&](ScreenshotClipboardCommitResult result) {
            require(result.succeeded(), "independent scopes must preserve their pending work");
            completed = true;
            loop.quit();
        });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    require(completed && live.isFinished() && abandoned.isFinished() && input.isNull() && !called,
            "destroyed scopes must release input ownership and suppress pending callbacks");
    QApplication::clipboard()->clear();
}

void completionCanCancelOtherCommitsAndStartANewBatch() {
    ScreenshotClipboardCommitScope scope;
    QObject receiver;
    QEventLoop loop;
    bool firstCompleted = false;
    bool cancelledCalled = false;
    bool newCompleted = false;
    const auto publicationId = ScreenshotClipboardService::reservePublication();
    const auto first = scope.commit(
        QApplication::clipboard(), &receiver, {},
        [&](ScreenshotClipboardCommitResult result) {
            require(result.failure == ScreenshotClipboardCommitFailure::InvalidPayload &&
                        scope.pendingCount() == 1,
                    "a completed operation must leave other pending publications owned");
            firstCompleted = true;
            scope.cancelAll();
            require(scope.pendingCount() == 0,
                    "a callback must be able to cancel its owner's remaining batch");
            const auto next = scope.commit(
                QApplication::clipboard(), &receiver, {},
                [&](ScreenshotClipboardCommitResult nextResult) {
                    require(nextResult.failure ==
                                    ScreenshotClipboardCommitFailure::InvalidPayload &&
                                scope.pendingCount() == 0,
                            "callback-created publications must retire the new batch correctly");
                    newCompleted = true;
                    loop.quit();
                });
            require(next.isValid() && scope.pendingCount() == 1,
                    "callbacks must be able to reuse a cancelled scope");
        },
        publicationId);
    const auto cancelled = scope.commit(
        QApplication::clipboard(), &receiver, {},
        [&](ScreenshotClipboardCommitResult) { cancelledCalled = true; }, publicationId);
    require(scope.pendingCount() == 2, "a scope must own multiple simultaneous publications");
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    require(firstCompleted && newCompleted && first.isFinished() && cancelled.isFinished() &&
                cancelled.isCancellationRequested() && !cancelledCalled,
            "reentrant batch replacement must preserve completion and cancellation boundaries");
}

#ifndef Q_OS_WIN
void replacementDuringImagePublicationSupersedesCopy() {
    auto* clipboard = QApplication::clipboard();
    const auto id = ScreenshotClipboardService::reservePublication(clipboard);
    QEventLoop loop;
    const QString replacement = QStringLiteral("newer copy");
    QObject::connect(
        clipboard, &QClipboard::dataChanged, &loop, [=] { clipboard->setText(replacement); },
        Qt::SingleShotConnection);
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    bool superseded = false;
    const auto handle = ScreenshotClipboardService::commit(
        clipboard, &loop, ScreenshotClipboardService::prepareImage(image), id, [&](auto result) {
            superseded = result.superseded;
            loop.quit();
        });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    require(handle.isFinished() && superseded &&
                !ScreenshotClipboardService::publicationCurrent(id) &&
                clipboard->text() == replacement,
            "a clipboard listener's replacement supersedes copy-and-close before completion");
}

void clipboardReadersOwnDecodedImages() {
    QImage source(32, 24, QImage::Format_ARGB32);
    source.fill(QColor(30, 60, 90, 128));
    require(ScreenshotClipboardService::publishImage(QApplication::clipboard(), source),
            "image publication must succeed");
    const QImage first = QApplication::clipboard()->image();
    require(!first.isNull() && first.isDetached(),
            "the clipboard must not retain a reader's decoded image");
    const QImage second = QApplication::clipboard()->image();
    require(first == second && second.isDetached(),
            "repeated reads must preserve pixels without caching decoded images");
    QApplication::clipboard()->setText(QStringLiteral("replacement"));
    require(first.pixelColor(0, 0) == source.pixelColor(0, 0),
            "replacing the clipboard must preserve images already owned by readers");
}

void scopeDestructionDuringPublicationSuppressesCompletion() {
    QObject receiver;
    auto scope = std::make_unique<ScreenshotClipboardCommitScope>();
    QEventLoop loop;
    const auto connection =
        QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, &loop, [&] {
            scope.reset();
            loop.quit();
        });
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("publication in progress"));
    bool called = false;
    const auto handle =
        scope->commitMimeData(QApplication::clipboard(), &receiver, mime,
                              [&](ScreenshotClipboardCommitResult) { called = true; });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    require(!scope && handle.isFinished() && handle.isCancellationRequested() && !called,
            "scope destruction during publication must safely suppress completion");
    require(QApplication::clipboard()->text() == QStringLiteral("publication in progress"),
            "a publication already in progress must retain valid input ownership");
    QApplication::clipboard()->clear();
}

void receiverDestructionDuringPublicationKeepsActiveInputsAlive(bool drainDeferredDeletes) {
    auto* receiver = new QObject;
    const QPointer<QObject> guardedReceiver(receiver);
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QEventLoop loop;
    const auto connection =
        QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, &loop, [&] {
            delete receiver;
            receiver = nullptr;
            if (drainDeferredDeletes) {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            loop.quit();
        });
    bool called = false;
    const auto handle = ScreenshotClipboardService::commit(
        QApplication::clipboard(), receiver, ScreenshotClipboardService::prepareImage(image),
        [&](ScreenshotClipboardCommitResult) { called = true; });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(connection);
    require(guardedReceiver.isNull() && handle.isFinished() && !called,
            "receiver destruction during publication must retire its commit without callbacks");
    require(QApplication::clipboard()->image().pixelColor(0, 0) == QColor(Qt::red),
            "publication already in progress must keep its input alive until the call returns");
    QApplication::clipboard()->clear();
}
#endif
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--clipboard-file-shutdown"))) {
        const auto index = app.arguments().indexOf(QStringLiteral("--clipboard-file-shutdown"));
        snow_shot::storage::ClipboardFileCache cache(app.arguments().at(index + 1));
        const QString firstPath = retainedClipboardFile(cache);
        const QString secondPath = retainedClipboardFile(cache);
        auto mime = std::make_unique<QMimeData>();
        mime->setUrls({QUrl::fromLocalFile(firstPath)});
        const auto handle = ScreenshotClipboardService::commitMimeData(
            QApplication::clipboard(), &app, mime.release(),
            [firstPath, secondPath, &app](ScreenshotClipboardCommitResult result) {
                require(result.succeeded(), "shutdown clipboard publication must succeed");
                auto* secondMime = new QMimeData();
                secondMime->setUrls({QUrl::fromLocalFile(secondPath)});
                const auto second = ScreenshotClipboardService::commitMimeData(
                    QApplication::clipboard(), &app, secondMime,
                    [firstPath, secondPath](ScreenshotClipboardCommitResult secondResult) {
                        require(secondResult.succeeded(), "second clipboard file must publish");
                        QApplication::clipboard()->setText(QStringLiteral("replacement"));
                        std::cout << firstPath.toStdString() << '\n'
                                  << secondPath.toStdString() << '\n';
                        QCoreApplication::quit();
                    });
                require(second.isValid(), "second clipboard publication must start");
            });
        require(handle.isValid(), "shutdown clipboard publication must start");
        QTimer::singleShot(2000, &app, [] { require(false, "shutdown publication timed out"); });
        return app.exec();
    }
    require(ScreenshotClipboardCommitHandle{}.isFinished(), "an empty handle has no pending work");
    commitCompletionMarksFinishedAndReleasesInputs();
    receiverDestroyedDuringMimePreparation();
    abandonedReceiversReleaseInputsAndFinishHandles();
    clipboardFilesOutlivePublications();
    clipboardFilesSurviveReplacementAndApplicationExit();
    failedCommitsFinishHandles();
    scopedCommitsReleaseTrackingAndCallbacksOnEveryOutcome();
    scopedAbandonedAndRejectedCommitsReleaseOwnership();
    scopedCancellationRetiresOnlyTheCancelledBatch();
    scopeDestructionCancelsWithoutAffectingOtherScopes();
    completionCanCancelOtherCommitsAndStartANewBatch();
#ifndef Q_OS_WIN
    replacementDuringImagePublicationSupersedesCopy();
    clipboardReadersOwnDecodedImages();
    scopeDestructionDuringPublicationSuppressesCompletion();
    receiverDestructionDuringPublicationKeepsActiveInputsAlive(false);
    receiverDestructionDuringPublicationKeepsActiveInputsAlive(true);
#endif
    return EXIT_SUCCESS;
}
