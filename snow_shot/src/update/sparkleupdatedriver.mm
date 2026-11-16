#include "macosupdatedriver_p.h"

#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <limits>
#include <utility>

#import <Sparkle/Sparkle.h>

namespace snow_shot::update::detail {
class SparkleUpdateDriver;
}

@interface SnowSparkleDelegate : NSObject <SPUUpdaterDelegate, SPUUserDriver> {
  @public
    snow_shot::update::detail::SparkleUpdateDriver* owner;
}
@end

namespace snow_shot::update::detail {
namespace {
QString string(NSString* value) {
    return value ? QString::fromNSString(value) : QString();
}
qint64 byteCount(uint64_t value) {
    return static_cast<qint64>(
        std::min(value, static_cast<uint64_t>(std::numeric_limits<qint64>::max())));
}
bool transportError(NSError* error) {
    // Never retry validation, extraction or installation errors against another host.
    for (NSUInteger depth = 0; error && depth < 8; ++depth) {
        if ([error.domain isEqualToString:NSURLErrorDomain])
            return error.code != NSURLErrorCancelled;
        if ([error.domain isEqualToString:SUSparkleErrorDomain] && error.code != SUAppcastError &&
            error.code != SUDownloadError)
            return false;
        NSError* underlying = error.userInfo[NSUnderlyingErrorKey];
        if (!underlying && [error.domain isEqualToString:SUSparkleErrorDomain])
            return error.code == SUDownloadError;
        error = underlying;
    }
    return false;
}
} // namespace

class SparkleUpdateDriver final : public MacOSUpdateDriver {
  public:
    ~SparkleUpdateDriver() override {
        if (delegate)
            delegate->owner = nullptr;
        if (observing)
            [updater removeObserver:delegate forKeyPath:@"canCheckForUpdates"];
        cancelBlock = nil;
        choiceBlock = nil;
        installBlock = nil;
        updater = nil;
    }
    void publish() {
        if (changed)
            changed(status, started && updater.canCheckForUpdates && !retryPending,
                    cancelBlock != nil, onQuit);
    }
    void state(UpdateState value) {
        status.state = value;
        status.error.clear();
        errorText = nullptr;
        publish();
    }
    void complete(const QString& outcome) {
        if (operation.isEmpty())
            return;
        const QString completed = std::exchange(operation, {});
        if (finished)
            finished(completed, outcome);
    }
    void fail(NSError* error) {
        if (error)
            qWarning("Sparkle update failed (%s, %ld): %s", error.domain.UTF8String,
                     static_cast<long>(error.code), error.localizedDescription.UTF8String);
        cancelBlock = nil;
        choiceBlock = nil;
        installBlock = nil;
        onQuit = false;
        explicitInstall = false;
        retryDownload = false;
        errorText = QT_TRANSLATE_NOOP("UpdateService",
                                      "Could not update the application. Please try again");
        status.state = UpdateState::Failed;
        retranslate();
        complete(QStringLiteral("failed"));
    }
    void start(const QString& value) override {
        if (started)
            return;
        delegate = [SnowSparkleDelegate new];
        delegate->owner = this;
        NSBundle* bundle = NSBundle.mainBundle;
        primaryFeed = string([bundle objectForInfoDictionaryKey:@"SUFeedURL"]);
        mirrorFeed = string([bundle objectForInfoDictionaryKey:@"SnowSparkleMirrorFeedURL"]);
        const auto validFeed = [bundle](const QString& text) {
            const QUrl url(text);
            const bool fixture =
                string(bundle.bundleIdentifier).startsWith(u"com.snowshot.tests.") &&
                url.scheme() == u"http" && url.host() == u"127.0.0.1";
            return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() &&
                   (url.scheme() == u"https" || fixture);
        };
        if (!validFeed(primaryFeed) || (!mirrorFeed.isEmpty() && !validFeed(mirrorFeed)) ||
            string([bundle objectForInfoDictionaryKey:@"SUPublicEDKey"]).isEmpty()) {
            errorText = QT_TRANSLATE_NOOP("UpdateService",
                                          "Automatic updates are not configured for this copy");
            status.state = UpdateState::Unavailable;
            retranslate();
            return;
        }
        updater = [[SPUUpdater alloc] initWithHostBundle:bundle
                                       applicationBundle:bundle
                                              userDriver:delegate
                                                delegate:delegate];
        setMode(value);
        NSError* error = nil;
        if (![updater startUpdater:&error]) {
            fail(error);
            return;
        }
        started = true;
        [updater addObserver:delegate
                  forKeyPath:@"canCheckForUpdates"
                     options:NSKeyValueObservingOptionNew
                     context:nullptr];
        observing = true;
        state(UpdateState::Idle);
        // Sparkle owns subsequent scheduling. This is its documented optional launch check,
        // dispatched without a nested event loop or any startup installation gate.
        if (updater.automaticallyChecksForUpdates)
            [updater checkForUpdatesInBackground];
    }
    void setMode(const QString& value) override {
        mode = value;
        const BOOL checks = mode != u"manual";
        const BOOL downloads = mode == u"next_launch";
        if (updater.automaticallyDownloadsUpdates != downloads)
            updater.automaticallyDownloadsUpdates = downloads;
        if (updater.automaticallyChecksForUpdates != checks)
            updater.automaticallyChecksForUpdates = checks;
        if (downloads && choiceBlock && status.state == UpdateState::Available)
            download();
        // Deliberately retain an installer already waiting for application termination.
    }
    void check(bool user) override {
        if (!started || !updater.canCheckForUpdates || (!user && mode == u"manual"))
            return;
        usingMirror = false;
        retryPending = false;
        retryDownload = false;
        const bool resuming = updater.sessionInProgress;
        if (resuming)
            operation = QStringLiteral("check");
        if (user)
            [updater checkForUpdates];
        else
            [updater checkForUpdatesInBackground];
        // Sparkle focuses an existing offer instead of starting another cycle.
        // Complete the Qt request even when no new appcast callback is produced.
        if (resuming)
            complete(QStringLiteral("success"));
    }
    void download() override {
        if (!choiceBlock || status.state != UpdateState::Available || !status.downloadUrl.isEmpty())
            return;
        auto reply = choiceBlock;
        choiceBlock = nil;
        operation = QStringLiteral("download");
        reply(SPUUserUpdateChoiceInstall);
    }
    void cancel() override {
        if (!cancelBlock)
            return;
        auto cancel = cancelBlock;
        cancelBlock = nil;
        cancelled = true;
        cancel();
    }
    void install() override {
        if (!onQuit || !installBlock)
            return;
        explicitInstall = true;
        operation = QStringLiteral("apply");
        state(UpdateState::Applying);
        installBlock();
    }
    void retranslate() override {
        if (errorText)
            status.error = QCoreApplication::translate("UpdateService", errorText);
        publish();
    }
    void found(SUAppcastItem* item) {
        status.version = string(item.displayVersionString);
        status.downloadUrl =
            item.informationOnlyUpdate ? QUrl(string(item.infoURL.absoluteString)) : QUrl();
    }
    void ready(void (^install)(void)) {
        cancelBlock = nil;
        choiceBlock = nil;
        installBlock = [install copy];
        onQuit = true;
        retryDownload = false;
        state(UpdateState::Ready);
        complete(QStringLiteral("success"));
    }
    void finishCycle(NSError* error) {
        if (cancelled ||
            ([error.domain isEqualToString:NSURLErrorDomain] &&
             error.code == NSURLErrorCancelled) ||
            ([error.domain isEqualToString:SUSparkleErrorDomain] &&
             error.code == SUInstallationCanceledError)) {
            cancelled = false;
            cancelBlock = nil;
            status.version.clear();
            state(UpdateState::Idle);
            complete(QStringLiteral("cancelled"));
        } else if (transportError(error) && !usingMirror && !mirrorFeed.isEmpty() && !onQuit &&
                   (manual || mode != u"manual")) {
            usingMirror = true;
            retryDownload = operation == u"download";
            retryPending = true;
            const bool user = manual;
            // Sparkle has retired the previous driver before this delegate callback.
            // Start now so its completion handler does not schedule a competing check.
            if (user)
                [updater checkForUpdates];
            else
                [updater checkForUpdatesInBackground];
            retryPending = false;
        } else if (error && !([error.domain isEqualToString:SUSparkleErrorDomain] &&
                              error.code == SUNoUpdateError)) {
            usingMirror = false;
            fail(error);
        } else {
            usingMirror = false;
            complete(QStringLiteral("success"));
            publish();
        }
    }
    SnowSparkleDelegate* __strong delegate = nil;
    SPUUpdater* __strong updater = nil;
    void (^__strong cancelBlock)(void) = nil;
    void (^__strong choiceBlock)(SPUUserUpdateChoice) = nil;
    void (^__strong installBlock)(void) = nil;
    UpdateStatus status;
    QString mode = QStringLiteral("next_launch");
    QString primaryFeed;
    QString mirrorFeed;
    QString operation;
    QSet<QString> notified;
    const char* errorText = nullptr;
    bool started = false;
    bool observing = false;
    bool manual = false;
    bool cancelled = false;
    bool onQuit = false;
    bool explicitInstall = false;
    bool usingMirror = false;
    bool retryPending = false;
    bool retryDownload = false;
};
std::unique_ptr<MacOSUpdateDriver> makeSparkleUpdateDriver() {
    return std::make_unique<SparkleUpdateDriver>();
}
} // namespace snow_shot::update::detail

using snow_shot::update::UpdateState;

@implementation SnowSparkleDelegate
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
    if ([keyPath isEqualToString:@"canCheckForUpdates"]) {
        if (owner)
            owner->publish();
    } else {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}
- (BOOL)updater:(SPUUpdater*)updater
    mayPerformUpdateCheck:(SPUUpdateCheck)check
                    error:(NSError**)error {
    (void)updater;
    (void)error;
    if (!owner)
        return NO;
    owner->manual = check == SPUUpdateCheckUpdates;
    if (!owner->retryDownload)
        owner->operation = QStringLiteral("check");
    owner->cancelled = false;
    if (!owner->onQuit)
        owner->state(UpdateState::Checking);
    return YES;
}
- (NSString*)feedURLStringForUpdater:(SPUUpdater*)updater {
    (void)updater;
    return owner ? (owner->usingMirror ? owner->mirrorFeed : owner->primaryFeed).toNSString() : nil;
}
- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater {
    (void)updater;
    return NO;
}
- (void)updater:(SPUUpdater*)updater didFindValidUpdate:(SUAppcastItem*)item {
    (void)updater;
    if (!owner)
        return;
    owner->found(item);
    if (!owner->retryDownload)
        owner->complete(QStringLiteral("success"));
}
- (void)updaterDidNotFindUpdate:(SPUUpdater*)updater error:(NSError*)error {
    (void)error;
    if (!owner || owner->onQuit)
        return;
    owner->status.version =
        QString::fromNSString(updater.hostBundle.infoDictionary[@"CFBundleShortVersionString"]);
    owner->state(UpdateState::Idle);
    if (owner->retryDownload) {
        owner->retryDownload = false;
        owner->complete(QStringLiteral("failed"));
    }
}
- (void)updater:(SPUUpdater*)updater
    willDownloadUpdate:(SUAppcastItem*)item
           withRequest:(NSMutableURLRequest*)request {
    (void)updater;
    (void)request;
    if (!owner)
        return;
    owner->found(item);
    owner->status.received = 0;
    owner->status.total = 0;
    owner->operation = QStringLiteral("download");
    owner->state(UpdateState::Downloading);
}
- (void)updater:(SPUUpdater*)updater didDownloadUpdate:(SUAppcastItem*)item {
    (void)updater;
    (void)item;
    if (owner)
        owner->state(UpdateState::Verifying);
}
- (BOOL)updater:(SPUUpdater*)updater
       willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))handler {
    (void)updater;
    if (!owner)
        return NO;
    owner->found(item);
    owner->ready(handler);
    // Hold this cycle and its explicit-restart handler. The installer independently
    // watches process termination; this does not ask the application to quit.
    return YES;
}
- (void)updater:(SPUUpdater*)updater willInstallUpdate:(SUAppcastItem*)item {
    (void)updater;
    (void)item;
    if (owner && owner->explicitInstall && owner->committed)
        owner->committed();
}
- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)check
                                 error:(NSError*)error {
    (void)updater;
    (void)check;
    if (owner)
        owner->finishCycle(error);
}
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:(void (^)(SUUpdatePermissionResponse*))reply {
    (void)request;
    reply([[SUUpdatePermissionResponse alloc]
        initWithAutomaticUpdateChecks:owner && owner->mode != u"manual"
           automaticUpdateDownloading:@(owner && owner->mode == u"next_launch")
                    sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation {
    if (!owner) {
        cancellation();
        return;
    }
    owner->cancelBlock = [cancellation copy];
    owner->state(UpdateState::Checking);
}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply {
    if (!owner) {
        reply(SPUUserUpdateChoiceDismiss);
        return;
    }
    owner->cancelBlock = nil;
    owner->found(item);
    if (state.stage == SPUUserUpdateStageInstalling) {
        owner->ready(^{
          reply(SPUUserUpdateChoiceInstall);
        });
    } else {
        owner->choiceBlock = [reply copy];
        owner->state(UpdateState::Available);
        if (owner->retryDownload) {
            if (owner->status.downloadUrl.isEmpty())
                owner->download();
            else {
                owner->retryDownload = false;
                owner->complete(QStringLiteral("failed"));
            }
            return;
        }
        if (!state.userInitiated && !owner->notified.contains(owner->status.version)) {
            owner->notified.insert(owner->status.version);
            if (owner->available)
                owner->available(owner->status.version);
        }
    }
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)data {
    (void)data;
}
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {
    (void)error;
}
- (void)showUpdateNotFoundWithError:(NSError*)error
                    acknowledgement:(void (^)(void))acknowledgement {
    (void)error;
    acknowledgement();
}
- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))acknowledgement {
    // The cycle completion reports the final error, after any bounded mirror retry.
    (void)error;
    acknowledgement();
}
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation {
    if (!owner) {
        cancellation();
        return;
    }
    owner->cancelBlock = [cancellation copy];
    owner->state(UpdateState::Downloading);
}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length {
    if (!owner)
        return;
    owner->status.total = snow_shot::update::detail::byteCount(length);
    owner->publish();
}
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {
    if (!owner)
        return;
    const auto count = snow_shot::update::detail::byteCount(length);
    owner->status.received +=
        std::min(count, std::numeric_limits<qint64>::max() - owner->status.received);
    owner->publish();
}
- (void)showDownloadDidStartExtractingUpdate {
    if (!owner)
        return;
    owner->cancelBlock = nil;
    owner->state(UpdateState::Verifying);
}
- (void)showExtractionReceivedProgress:(double)progress {
    (void)progress;
}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
    if (!owner) {
        reply(SPUUserUpdateChoiceDismiss);
        return;
    }
    owner->ready(^{
      reply(SPUUserUpdateChoiceInstall);
    });
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated
                          retryTerminatingApplication:(void (^)(void))retry {
    (void)terminated;
    (void)retry;
    if (owner)
        owner->state(UpdateState::Applying);
}
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched
                         acknowledgement:(void (^)(void))acknowledgement {
    (void)relaunched;
    if (owner)
        owner->complete(QStringLiteral("success"));
    acknowledgement();
}
- (void)dismissUpdateInstallation {
    if (!owner)
        return;
    owner->cancelBlock = nil;
    owner->choiceBlock = nil;
    owner->publish();
}
- (void)showUpdateInFocus {
    if (owner)
        owner->publish();
}
@end
