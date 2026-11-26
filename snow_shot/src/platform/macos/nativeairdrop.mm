// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../presentation/services/nativeairdropbackend.h"

#include <QCoreApplication>
#include <QPointer>
#include <QWidget>

#import <AppKit/AppKit.h>

#include <utility>

@interface SnowShotAirDropSession : NSObject <NSSharingServiceDelegate> {
  @public
    ScreenshotAirDropService::Completion completion;
    QPointer<QWidget> sourceWidget;
}
@property(nonatomic, strong) NSSharingService* service;
@property(nonatomic, strong) SnowShotAirDropSession* lifetime;
@property(nonatomic, weak) NSWindow* ownerWindow;
@property(nonatomic, strong) NSURL* sharedFile;
@property(nonatomic) BOOL cancellationRequested;
- (void)finish:(ScreenshotAirDropService::Result)result;
- (void)cancel;
@end

@implementation SnowShotAirDropSession
- (void)cancel {
    self.cancellationRequested = YES;
    NSWindow* owner = self.ownerWindow;
    if (!owner)
        return;
    NSWindow* chooser = owner.attachedSheet;
    if (!chooser) {
        NSWindow* key = NSApp.keyWindow;
        for (NSWindow* parent = key.parentWindow; parent; parent = parent.parentWindow) {
            if (parent == owner) {
                chooser = key;
                break;
            }
        }
    }
    // AirDrop can already be transferring without a chooser. Leave its file lease with the
    // native callback in that case, independently of the retired screenshot request.
    [chooser cancelOperation:nil];
}

- (void)finish:(ScreenshotAirDropService::Result)result {
    auto finished = std::move(completion);
    if (self.cancellationRequested && self.ownerWindow && QCoreApplication::instance()) {
        const QPointer<QWidget> widget = sourceWidget;
        NSWindow* const retired = self.ownerWindow;
        // The sharing UI can reorder its source while finishing the dismissal animation.
        // Recheck after AppKit returns and Qt has had a chance to replace the native surface.
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [widget, retired] {
                @autoreleasepool {
                    NSView* current =
                        widget ? (__bridge NSView*)reinterpret_cast<void*>(widget->internalWinId())
                               : nil;
                    if (current.window != retired)
                        [retired close];
                }
            },
            Qt::QueuedConnection);
    }
    self.service.delegate = nil;
    self.service = nil;
    if (finished)
        finished(std::move(result));
    self.lifetime = nil;
}
- (void)sharingService:(NSSharingService*)service didShareItems:(NSArray*)items {
    (void)items;
    // macOS 27's AirDrop extension calls didShareItems for Cancel as well, with
    // the original input items. Require the documented shared-attachment result;
    // an absent result is a handoff, never proof that the user sent the file.
    const bool confirmed = [service.attachmentFileURLs containsObject:self.sharedFile];
    [self finish:{confirmed ? ScreenshotAirDropService::Status::Shared
                            : ScreenshotAirDropService::Status::HandedOff,
                  {}}];
}
- (void)sharingService:(NSSharingService*)service
    didFailToShareItems:(NSArray*)items
                  error:(NSError*)error {
    (void)service;
    (void)items;
    const bool cancelled =
        ([error.domain isEqualToString:NSCocoaErrorDomain] && error.code == NSUserCancelledError) ||
        ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled) ||
        ([error.domain isEqualToString:NSOSStatusErrorDomain] && error.code == userCanceledErr);
    [self finish:{cancelled ? ScreenshotAirDropService::Status::Cancelled
                            : ScreenshotAirDropService::Status::Failed,
                  cancelled ? QString() : QString::fromNSString(error.localizedDescription)}];
}
- (NSWindow*)sharingService:(NSSharingService*)service
    sourceWindowForShareItems:(NSArray*)items
          sharingContentScope:(NSSharingContentScope*)scope {
    (void)service;
    (void)items;
    *scope = NSSharingContentScopeItem;
    return self.cancellationRequested ? nil : self.ownerWindow;
}
@end

ScreenshotAirDropService::NativeBackend screenshotNativeAirDropBackend() {
    return [](QWidget* owner, QString path, ScreenshotAirDropService::Completion completion,
              ScreenshotAirDropService::CancelNative& cancel) {
        @autoreleasepool {
            SnowShotAirDropSession* session = nil;
            @try {
                NSView* view = (__bridge NSView*)reinterpret_cast<void*>(owner->winId());
                NSSharingService* service =
                    [NSSharingService sharingServiceNamed:NSSharingServiceNameSendViaAirDrop];
                NSArray* items = @[ [NSURL fileURLWithPath:path.toNSString()] ];
                if (!view.window || !service || ![service canPerformWithItems:items]) {
                    completion({ScreenshotAirDropService::Status::Unavailable,
                                QCoreApplication::translate("ScreenshotAirDropService",
                                                            "AirDrop is unavailable on this Mac")});
                    return;
                }
                session = [SnowShotAirDropSession new];
                session->completion = std::move(completion);
                session->sourceWidget = owner;
                session.service = service;
                session.ownerWindow = view.window;
                session.sharedFile = items.firstObject;
                session.lifetime = session;
                service.delegate = session;
                __weak SnowShotAirDropSession* weakSession = session;
                cancel = [weakSession] {
                    @autoreleasepool {
                        [weakSession cancel];
                    }
                };
                [service performWithItems:items];
            } @catch (NSException* exception) {
                (void)exception;
                ScreenshotAirDropService::Result result{
                    ScreenshotAirDropService::Status::Failed,
                    QCoreApplication::translate("ScreenshotAirDropService",
                                                "The AirDrop sharing window could not be opened")};
                cancel = {};
                if (session)
                    [session finish:std::move(result)];
                else if (completion)
                    completion(std::move(result));
            }
        }
    };
}
