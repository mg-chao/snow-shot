// SPDX-License-Identifier: GPL-3.0-or-later
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <libproc.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Milliseconds = std::chrono::milliseconds;
using Color = std::array<int, 3>;
constexpr std::array colors{Color{232, 48, 121}, Color{39, 212, 201}, Color{254, 194, 45},
                            Color{103, 76, 223}};
constexpr Color backdropColor{55, 65, 75};
constexpr CGEventFlags shortcutModifiers =
    kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
constexpr int streamWidth = 480;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate> bool waitFor(Milliseconds timeout, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        @autoreleasepool {
            if (predicate()) {
                return true;
            }
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

NSColor* nativeColor(const Color& color) {
    return [NSColor colorWithSRGBRed:color[0] / 255.0
                               green:color[1] / 255.0
                                blue:color[2] / 255.0
                               alpha:1];
}

struct Frame {
    uint64_t tick;
    std::vector<uint32_t> pixels;
    size_t width;
    size_t height;
    size_t stride;
};

bool near(uint32_t bgra, Color expected, int tolerance = 18) {
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto channel = (bgra >> (8 * (2 - i))) & 255;
        if (std::abs(static_cast<int>(channel) - expected[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

constexpr bool matchesImageAspect(CGSize size, double aspectRatio) {
    const double expectedWidth = size.height * aspectRatio;
    return size.width >= .75 * expectedWidth && size.width <= 1.25 * expectedWidth;
}

static_assert(matchesImageAspect({900, 900}, 1.0));
static_assert(!matchesImageAspect({900, 900}, 5.0 / 3));

struct NativeWindow {
    unsigned int id;
    CGRect bounds;
};

std::vector<NativeWindow> windows(int pid) {
    NSArray* info = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    std::vector<NativeWindow> result;
    for (NSDictionary* window in info) {
        if ([window[(__bridge NSString*)kCGWindowOwnerPID] intValue] != pid ||
            [window[(__bridge NSString*)kCGWindowAlpha] doubleValue] == 0) {
            continue;
        }
        CGRect bounds;
        if (CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)window[(__bridge NSString*)kCGWindowBounds], &bounds)) {
            result.push_back(
                {[window[(__bridge NSString*)kCGWindowNumber] unsignedIntValue], bounds});
        }
    }
    return result;
}

uint64_t postKey(CGKeyCode code, CGEventFlags flags) {
    auto down = CGEventCreateKeyboardEvent(nullptr, code, true);
    auto up = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);
    const auto start = mach_absolute_time();
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
    return start;
}

uint64_t postMouse(CGEventType type, CGPoint point) {
    auto event = CGEventCreateMouseEvent(nullptr, type, point, kCGMouseButtonLeft);
    CGEventSetFlags(event, 0);
    const auto start = mach_absolute_time();
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return start;
}
} // namespace

@interface PinBenchmarkPixels : NSObject <SCStreamOutput> {
  @public
    std::mutex mutex;
    std::deque<Frame> frames;
}
@end

@implementation PinBenchmarkPixels
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen) {
        return;
    }
    NSArray* attachments =
        (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSDictionary* metadata = attachments.firstObject;
    NSNumber* status = metadata[SCStreamFrameInfoStatus];
    NSNumber* displayTime = metadata[SCStreamFrameInfoDisplayTime];
    CVImageBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
    if (!status || status.integerValue != SCFrameStatusComplete || !displayTime || !buffer ||
        CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA ||
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return;
    }
    const auto* bytes = static_cast<const uint32_t*>(CVPixelBufferGetBaseAddress(buffer));
    const size_t width = CVPixelBufferGetWidth(buffer);
    const size_t height = CVPixelBufferGetHeight(buffer);
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer) / sizeof(uint32_t);
    if (bytes && width && height) {
        Frame frame{displayTime.unsignedLongLongValue,
                    {bytes, bytes + stride * height},
                    width,
                    height,
                    stride};
        const std::lock_guard lock(mutex);
        frames.push_back(std::move(frame));
        if (frames.size() > 60) {
            frames.pop_front();
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
}
@end

@interface PinBenchmarkTarget : NSView
@end

@implementation PinBenchmarkTarget
- (BOOL)isFlipped {
    return YES;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const auto halfWidth = NSWidth(self.bounds) / 2;
    const auto halfHeight = NSHeight(self.bounds) / 2;
    for (size_t i = 0; i < colors.size(); ++i) {
        [nativeColor(colors[i]) setFill];
        NSRectFill(NSMakeRect(static_cast<double>(i % 2) * halfWidth,
                              static_cast<double>(i / 2) * halfHeight, halfWidth, halfHeight));
    }
}
@end

namespace {
SCStream* startStream(CGDirectDisplayID id, PinBenchmarkPixels* observer) {
    __block SCShareableContent* content = nil;
    auto listed = dispatch_semaphore_create(0);
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent* value, NSError* error) {
                                   content = value;
                                   if (error) {
                                       std::cerr << error.localizedDescription.UTF8String << '\n';
                                   }
                                   dispatch_semaphore_signal(listed);
                                 }];
    require(waitFor(Milliseconds(5000),
                    [&] { return dispatch_semaphore_wait(listed, DISPATCH_TIME_NOW) == 0; }) &&
                content,
            "Could not list capture displays");
    SCDisplay* display = nil;
    for (SCDisplay* candidate in content.displays) {
        if (candidate.displayID == id) {
            display = candidate;
        }
    }
    require(display != nil, "Primary display unavailable");
    auto* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    auto* config = [SCStreamConfiguration new];
    config.width = streamWidth;
    config.height = static_cast<size_t>(std::lround(
        streamWidth * static_cast<double>(display.height) / static_cast<double>(display.width)));
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.colorSpaceName = kCGColorSpaceSRGB;
    config.showsCursor = NO;
    config.queueDepth = 5;
    config.minimumFrameInterval = CMTimeMake(1, 240);
    auto* stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
    NSError* error = nil;
    require([stream addStreamOutput:observer
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:dispatch_queue_create("pin-performance", DISPATCH_QUEUE_SERIAL)
                              error:&error],
            "Could not observe the compositor");
    auto started = dispatch_semaphore_create(0);
    __block NSError* failure = nil;
    [stream startCaptureWithCompletionHandler:^(NSError* value) {
      failure = value;
      dispatch_semaphore_signal(started);
    }];
    require(waitFor(Milliseconds(5000),
                    [&] { return dispatch_semaphore_wait(started, DISPATCH_TIME_NOW) == 0; }) &&
                !failure,
            "Could not start observing the compositor");
    return stream;
}

NSWindow* fixtureWindow(NSRect frame, NSWindowStyleMask style = NSWindowStyleMaskBorderless) {
    auto* window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    window.releasedWhenClosed = NO;
    window.hasShadow = NO;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces;
    return window;
}

class Pasteboard {
  public:
    void write(NSString* path, NSString* kind) {
        auto* clipboard = NSPasteboard.generalPasteboard;
        require(!m_original || clipboard.changeCount == m_change,
                "The clipboard changed outside the benchmark");
        if (!m_original) {
            auto* saved = [NSMutableArray new];
            for (NSPasteboardItem* item in clipboard.pasteboardItems) {
                auto* copy = [NSPasteboardItem new];
                for (NSPasteboardType type in item.types) {
                    if (NSData* data = [item dataForType:type]) {
                        [copy setData:data forType:type];
                    }
                }
                if (copy.types.count) {
                    [saved addObject:copy];
                }
            }
            m_original = saved;
        }
        [clipboard clearContents];
        if ([kind isEqualToString:@"file"]) {
            require([clipboard writeObjects:@[ [NSURL fileURLWithPath:path] ]],
                    "Could not set pasteboard file");
        } else {
            NSData* data = [NSData dataWithContentsOfFile:path];
            require(data != nil, "Could not read pasteboard fixture");
            require([clipboard setData:data forType:kind], "Could not set pasteboard data");
        }
        m_change = clipboard.changeCount;
    }

    ~Pasteboard() {
        auto* clipboard = NSPasteboard.generalPasteboard;
        if (m_original && clipboard.changeCount == m_change) {
            [clipboard clearContents];
            if (m_original.count) {
                [clipboard writeObjects:m_original];
            }
        }
    }

  private:
    NSArray<NSPasteboardItem*>* m_original = nil;
    NSInteger m_change = 0;
};

class Observer {
  public:
    Observer() : m_display(CGDisplayBounds(CGMainDisplayID())) {
        require(CGPreflightScreenCaptureAccess() && CGPreflightPostEventAccess(),
                "Screen Recording and keyboard-event permission are required");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSScreen* screen = NSScreen.screens.firstObject;
        require(screen != nil, "No main screen");
        const CGRect area = screen.frame;
        m_window = fixtureWindow(area);
        m_window.backgroundColor = nativeColor(backdropColor);
        const auto width = std::floor(area.size.width * .4);
        const auto height = std::floor(width * .6);
        m_targetBounds =
            CGRectMake(m_display.origin.x + 110, m_display.origin.y + 130, width, height);
        const CGRect target =
            CGRectMake(NSMinX(area) + 110, NSMaxY(area) - 130 - height, width, height);
        m_target = fixtureWindow(target, NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                             NSWindowStyleMaskMiniaturizable);
        m_target.title = @"Snow Shot pin benchmark target";
        m_target.contentView =
            [[PinBenchmarkTarget alloc] initWithFrame:CGRectMake(0, 0, width, height)];
        [m_window orderFrontRegardless];
        m_pixels = [PinBenchmarkPixels new];
        m_stream = startStream(CGMainDisplayID(), m_pixels);
        CGEventRef event = CGEventCreate(nullptr);
        require(event != nullptr, "Could not read cursor");
        m_cursor = CGEventGetLocation(event);
        CFRelease(event);
    }

    ~Observer() {
        [m_stream stopCaptureWithCompletionHandler:nil];
        [m_target close];
        [m_window close];
        CGWarpMouseCursorPosition(m_cursor);
    }

    NSDictionary* command(NSDictionary* command) {
        const int pid = [command[@"pid"] intValue];
        NSString* name = command[@"op"];
        if ([name isEqualToString:@"state"]) {
            proc_taskinfo info{};
            require(proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof(info)) == sizeof(info),
                    "Could not sample application RSS");
            auto* ids = [NSMutableArray new];
            for (const auto& window : windows(pid)) {
                [ids addObject:@(window.id)];
            }
            return @{@"rss" : @(info.pti_resident_size), @"windows" : ids};
        }
        if ([name isEqualToString:@"clipboard"]) {
            m_pasteboard.write(command[@"path"], command[@"type"]);
            return @{};
        }
        if ([name isEqualToString:@"fixture"]) {
            [m_window orderFrontRegardless];
            const auto start = mach_absolute_time();
            if ([command[@"show"] boolValue]) {
                [m_target orderFrontRegardless];
            } else {
                [m_target orderOut:nil];
            }
            waitFor(Milliseconds(80), [] { return false; });
            return @{
                @"tick" : @(start),
                @"selection" : @[
                    @(m_targetBounds.origin.x + 9), @(m_targetBounds.origin.y + 9),
                    @(m_targetBounds.size.width - 18), @(m_targetBounds.size.height - 18)
                ]
            };
        }
        if ([name isEqualToString:@"key"]) {
            return @{
                @"tick" : @(postKey([command[@"code"] unsignedShortValue],
                                    [command[@"plain"] boolValue] ? 0 : shortcutModifiers))
            };
        }
        if ([name isEqualToString:@"overlay"]) {
            require(waitFor(Milliseconds(4000),
                            [&] {
                                const auto appWindows = windows(pid);
                                return std::any_of(appWindows.begin(), appWindows.end(),
                                                   [&](const auto& window) {
                                                       return window.bounds.size.width >=
                                                                  m_display.size.width - 4 &&
                                                              window.bounds.size.height >=
                                                                  m_display.size.height - 4;
                                                   });
                            }),
                    "Selection overlay never appeared");
            return @{};
        }
        if ([name isEqualToString:@"select"]) {
            const auto inner = CGRectInset(m_targetBounds, 9, 9);
            const auto start = CGPointMake(CGRectGetMinX(inner), CGRectGetMinY(inner));
            const auto end = CGPointMake(CGRectGetMaxX(inner), CGRectGetMaxY(inner));
            postMouse(kCGEventMouseMoved, start);
            waitFor(Milliseconds(15), [] { return false; });
            postMouse(kCGEventLeftMouseDown, start);
            for (int i = 1; i <= 5; ++i) {
                waitFor(Milliseconds(10), [] { return false; });
                postMouse(kCGEventLeftMouseDragged,
                          CGPointMake(std::lerp(start.x, end.x, i / 5.0),
                                      std::lerp(start.y, end.y, i / 5.0)));
            }
            waitFor(Milliseconds(10), [] { return false; });
            if ([command[@"hide"] boolValue]) {
                [m_target orderOut:nil];
                waitFor(Milliseconds(25), [] { return false; });
            }
            return @{@"tick" : @(postMouse(kCGEventLeftMouseUp, end))};
        }
        if ([name isEqualToString:@"hover"]) {
            const auto target = m_targetBounds;
            require(CGWarpMouseCursorPosition(CGPointMake(
                        CGRectGetMidX(target), CGRectGetMidY(target))) == kCGErrorSuccess,
                    "Could not move the cursor to the observed display");
            return @{};
        }
        require([name isEqualToString:@"observe"], "Unknown command");
        const auto selection = CGRectInset(m_targetBounds, 9, 9);
        const double fixtureAspectRatio = [command[@"aspect_ratio"] doubleValue];
        const double expectedAspectRatio = fixtureAspectRatio > 0
                                               ? fixtureAspectRatio
                                               : selection.size.width / selection.size.height;
        return observe(pid, [command[@"tick"] unsignedLongLongValue], command[@"exclude"],
                       [command[@"clear_overlay"] boolValue], [command[@"document"] boolValue],
                       expectedAspectRatio);
    }

  private:
    uint32_t pixel(const Frame& frame, double x, double y) const {
        const auto column = std::clamp((x - m_display.origin.x) / m_display.size.width, 0.0, .9999);
        const auto row = std::clamp((y - m_display.origin.y) / m_display.size.height, 0.0, .9999);
        return frame
            .pixels[static_cast<size_t>(row * static_cast<double>(frame.height)) * frame.stride +
                    static_cast<size_t>(column * static_cast<double>(frame.width))];
    }

    bool matches(const Frame& frame, CGRect bounds, bool clearOverlay, bool document) const {
        if (clearOverlay && !near(pixel(frame, m_display.origin.x + 58, m_display.origin.y + 90),
                                  backdropColor, 10)) {
            return false;
        }
        if (document) {
            // The fixtures pair light paper with dark ink or the inverse.
            // Use a dense grid so line spacing cannot align with the probes.
            int light = 0;
            int dark = 0;
            for (int row = 7; row < 57; ++row) {
                for (int col = 6; col < 43; ++col) {
                    const auto color = pixel(frame, bounds.origin.x + bounds.size.width * col / 48,
                                             bounds.origin.y + bounds.size.height * row / 64);
                    light += near(color, Color{230, 223, 206}, 55);
                    dark += near(color, Color{24, 24, 24}, 28);
                }
            }
            return light >= 120 && dark >= 120;
        }
        for (const double offset : {.18, .31, .39}) {
            for (size_t i = 0; i < colors.size(); ++i) {
                const double x = i % 2 ? 1 - offset : offset;
                const double y = i / 2 ? 1 - offset : offset;
                if (!near(pixel(frame, bounds.origin.x + bounds.size.width * x,
                                bounds.origin.y + bounds.size.height * y),
                          colors[i])) {
                    return false;
                }
            }
        }
        return true;
    }

    NSDictionary* observe(int pid, uint64_t start, NSArray* exclude, bool clearOverlay,
                          bool document, double expectedAspectRatio) {
        Frame matched{};
        NativeWindow pin{};
        const bool found = waitFor(Milliseconds(7000), [&] {
            const auto appWindows = windows(pid);
            const std::lock_guard lock(m_pixels->mutex);
            for (const auto& frame : m_pixels->frames) {
                if (frame.tick <= start) {
                    continue;
                }
                for (const auto& window : appWindows) {
                    const auto size = window.bounds.size;
                    if (size.width < 45 || size.height < 30 ||
                        size.width >= m_display.size.width - 4 ||
                        !CGRectIntersectsRect(window.bounds, m_display) ||
                        [exclude containsObject:@(window.id)] ||
                        (!document && !matchesImageAspect(size, expectedAspectRatio))) {
                        continue;
                    }
                    if (matches(frame, window.bounds, clearOverlay, document)) {
                        matched.tick = frame.tick;
                        pin = window;
                        return true;
                    }
                }
            }
            return false;
        });
        if (!found) {
            for (const auto& candidate : windows(pid)) {
                const auto r = candidate.bounds;
                std::cerr << candidate.id << ": " << r.origin.x << ',' << r.origin.y << ' '
                          << r.size.width << 'x' << r.size.height << '\n';
                const std::lock_guard lock(m_pixels->mutex);
                if (document && !m_pixels->frames.empty()) {
                    const auto& last = m_pixels->frames.back();
                    for (int row = 1; row <= 5; ++row) {
                        for (int column = 1; column <= 8; ++column) {
                            std::cerr << ' ' << std::hex
                                      << (pixel(last, r.origin.x + r.size.width * column / 9,
                                                r.origin.y + r.size.height * row / 6) &
                                          0xffffff);
                        }
                        std::cerr << std::dec << '\n';
                    }
                }
            }
        }
        require(found, "No composited pin frame matched the source");
        mach_timebase_info_data_t scale{};
        mach_timebase_info(&scale);
        const double elapsed =
            static_cast<double>(matched.tick - start) * scale.numer / scale.denom / 1e6;
        return @{
            @"window" : @(pin.id),
            @"display_tick" : @(matched.tick),
            @"ms" : @(elapsed),
            @"bounds" : @[
                @(pin.bounds.origin.x), @(pin.bounds.origin.y), @(pin.bounds.size.width),
                @(pin.bounds.size.height)
            ]
        };
    }

    CGRect m_display{};
    CGRect m_targetBounds{};
    CGPoint m_cursor{};
    NSWindow* m_window = nil;
    NSWindow* m_target = nil;
    PinBenchmarkPixels* m_pixels = nil;
    SCStream* m_stream = nil;
    Pasteboard m_pasteboard;
};

void writeReply(NSDictionary* response) {
    auto* json = [NSJSONSerialization dataWithJSONObject:response options:0 error:nil];
    std::cout.write(static_cast<const char*>(json.bytes),
                    static_cast<std::streamsize>(json.length));
    std::cout << std::endl;
}
} // namespace

int main() {
    @autoreleasepool {
        try {
            Observer observer;
            writeReply(@{@"ready" : @YES});
            std::string line;
            while (std::getline(std::cin, line)) {
                @autoreleasepool {
                    auto* bytes = [NSData dataWithBytes:line.data() length:line.size()];
                    NSDictionary* message = [NSJSONSerialization JSONObjectWithData:bytes
                                                                            options:0
                                                                              error:nil];
                    if ([message[@"op"] isEqualToString:@"quit"]) {
                        break;
                    }
                    try {
                        writeReply(observer.command(message));
                    } catch (const std::exception& error) {
                        writeReply(@{@"error" : [NSString stringWithUTF8String:error.what()]});
                    }
                }
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
