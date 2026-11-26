// SPDX-License-Identifier: GPL-3.0-or-later

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QUuid>

#include <libproc.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
constexpr CGEventFlags shortcutModifiers =
    kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift;
constexpr CGKeyCode shortcutKey = kVK_ANSI_9;
constexpr size_t probeCount = 12;

using Color = std::array<int, 3>;
using Pixels = std::array<Color, probeCount>;
using Points = std::array<CGPoint, probeCount>;

struct Frame {
    uint64_t displayTime = 0;
    Pixels pixels{};
};

struct FrameQueue {
    std::mutex mutex;
    std::deque<Frame> frames;
    Points probes{};
    CGRect display{};
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void pump() {
    @autoreleasepool {
        QCoreApplication::processEvents();
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.004, false);
    }
}

template <typename Predicate> bool waitUntil(int milliseconds, Predicate predicate) {
    QElapsedTimer timer;
    timer.start();
    do {
        if (predicate())
            return true;
        pump();
    } while (timer.elapsed() < milliseconds);
    return false;
}

void settle(int milliseconds) {
    waitUntil(milliseconds, [] { return false; });
}

double millisecondsBetween(uint64_t first, uint64_t last) {
    mach_timebase_info_data_t scale{};
    mach_timebase_info(&scale);
    return static_cast<double>(last - first) * scale.numer / scale.denom / 1e6;
}

NSColor* nativeColor(const Color& color) {
    return [NSColor colorWithSRGBRed:color[0] / 255.0
                               green:color[1] / 255.0
                                blue:color[2] / 255.0
                               alpha:1];
}

NSWindow* fixtureWindow(NSRect frame) {
    auto* window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:NSWindowStyleMaskBorderless
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    window.releasedWhenClosed = NO;
    window.hasShadow = NO;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces;
    return window;
}

class Fixture {
  public:
    explicit Fixture(CGDirectDisplayID displayId) {
        NSScreen* screen = nil;
        for (NSScreen* candidate in NSScreen.screens) {
            NSNumber* number = candidate.deviceDescription[@"NSScreenNumber"];
            if (number.unsignedIntValue == displayId) {
                screen = candidate;
                break;
            }
        }
        require(screen != nil, "Display unavailable to the fixture");
        const auto display = CGDisplayBounds(displayId);
        const CGRect area = screen.frame;
        const double width = CGRectGetWidth(area);
        const double height = CGRectGetHeight(area);
        const double left = width * .28;
        const double top = height * .26;
        const double right = width * .73;
        const double bottom = height * .70;
        const auto screenPoint = [display](double x, double y) {
            return CGPointMake(CGRectGetMinX(display) + x, CGRectGetMinY(display) + y);
        };

        m_backdrop = fixtureWindow(area);
        m_target = fixtureWindow(
            NSMakeRect(NSMinX(area) + left, NSMaxY(area) - bottom, right - left, bottom - top));
        m_target.title = @"Snow Shot capture benchmark target";
        m_cursor = screenPoint(left + (right - left) / 2, top + (bottom - top) / 2);
        // Four probes just inside the selection and eight in the masked area. Avoid the
        // pointer, selection border, menu bar, and the shortcuts in the lower-left corner.
        constexpr double inset = 18;
        m_probes = {screenPoint(left + inset, top + inset),
                    screenPoint(right - inset, top + inset),
                    screenPoint(left + inset, bottom - inset),
                    screenPoint(right - inset, bottom - inset),
                    screenPoint(left - inset, top + inset),
                    screenPoint(right + inset, top + inset),
                    screenPoint((left + right) / 2, top - inset),
                    screenPoint(right - inset, bottom + inset),
                    screenPoint(width * .12, height * .14),
                    screenPoint(width * .88, height * .14),
                    screenPoint(width * .12, height * .30),
                    screenPoint(width * .88, height * .75)};
        recolor(0);
        raise();
    }

    ~Fixture() {
        [m_target close];
        [m_backdrop close];
    }

    void raise() const {
        [m_backdrop orderFrontRegardless];
        [m_target orderFrontRegardless];
    }

    void recolor(int index) {
        constexpr std::array foregrounds{Color{88, 224, 125}, Color{224, 105, 173},
                                         Color{148, 215, 225}, Color{229, 178, 70}};
        constexpr std::array backgrounds{Color{207, 141, 224}, Color{109, 222, 74},
                                         Color{225, 165, 86}, Color{116, 148, 234}};
        m_foreground = foregrounds.at(static_cast<size_t>(index % 4));
        m_background = backgrounds.at(static_cast<size_t>(index % 4));
        m_target.backgroundColor = nativeColor(m_foreground);
        m_backdrop.backgroundColor = nativeColor(m_background);
        [m_target displayIfNeeded];
        [m_backdrop displayIfNeeded];
    }

    Pixels colors() const {
        Pixels pixels;
        std::fill_n(pixels.begin(), 4, m_foreground);
        std::fill(pixels.begin() + 4, pixels.end(), m_background);
        return pixels;
    }

    const Points& probes() const {
        return m_probes;
    }
    CGPoint cursor() const {
        return m_cursor;
    }

  private:
    NSWindow* m_target = nil;
    NSWindow* m_backdrop = nil;
    Color m_foreground{};
    Color m_background{};
    Points m_probes{};
    CGPoint m_cursor{};
};

bool near(const Color& actual, const Color& source, double factor, int tolerance) {
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - source[i] * factor) > tolerance)
            return false;
    }
    return true;
}

bool desktopMatches(const Pixels& actual, const Pixels& expected) {
    for (size_t i = 0; i < actual.size(); ++i) {
        if (!near(actual[i], expected[i], 1.0, 12))
            return false;
    }
    return true;
}

bool selectionMatches(const Pixels& actual, const Pixels& desktop, bool smartSelection) {
    for (size_t i = 0; i < actual.size(); ++i) {
        const bool highlighted = i < 4 && smartSelection;
        if (!near(actual[i], desktop[i], highlighted ? 1.0 : 0.5, highlighted ? 12 : 20))
            return false;
    }
    return true;
}

QJsonArray jsonPixels(const Pixels& pixels) {
    QJsonArray array;
    for (const auto& color : pixels)
        array.append(QJsonArray{color[0], color[1], color[2]});
    return array;
}

qint64 residentBytes(QProcess& process) {
    proc_taskinfo info{};
    const auto pid = static_cast<int>(process.processId());
    require(pid > 0 && proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof(info)) == sizeof(info),
            "Could not measure Snow Shot resident memory");
    return static_cast<qint64>(info.pti_resident_size);
}

uint64_t postKey(CGKeyCode key, CGEventFlags modifiers = 0) {
    auto source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    require(source != nullptr, "Could not create keyboard event source");
    const auto releaseSource = qScopeGuard([&] { CFRelease(source); });
    auto down = CGEventCreateKeyboardEvent(source, key, true);
    auto up = CGEventCreateKeyboardEvent(source, key, false);
    require(down && up, "Could not create keyboard events");
    const auto release = qScopeGuard([&] {
        CFRelease(down);
        CFRelease(up);
    });
    CGEventSetFlags(down, modifiers);
    CGEventSetFlags(up, modifiers);
    const auto triggerTime = mach_absolute_time();
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    return triggerTime;
}
} // namespace

@interface CaptureStartupFrameObserver : NSObject <SCStreamOutput> {
  @public
    FrameQueue queue;
}
@end

@implementation CaptureStartupFrameObserver
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
    Q_UNUSED(stream);
    if (type != SCStreamOutputTypeScreen)
        return;
    auto* attachments = (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSDictionary* metadata = attachments.firstObject;
    NSNumber* status = metadata[SCStreamFrameInfoStatus];
    NSNumber* displayTime = metadata[SCStreamFrameInfoDisplayTime];
    CVImageBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
    if (status == nil || status.integerValue != SCFrameStatusComplete || displayTime == nil ||
        buffer == nullptr || CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA ||
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
        return;
    const auto unlock =
        qScopeGuard([&] { CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly); });
    const auto* bytes = static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
    const auto stride = CVPixelBufferGetBytesPerRow(buffer);
    const auto width = CVPixelBufferGetWidth(buffer);
    const auto height = CVPixelBufferGetHeight(buffer);
    if (!bytes || width == 0 || height == 0)
        return;
    Frame frame;
    // WindowServer display time, in the same mach clock as the shortcut.
    frame.displayTime = displayTime.unsignedLongLongValue;
    for (size_t i = 0; i < queue.probes.size(); ++i) {
        const auto point = queue.probes[i];
        const double x = (point.x - queue.display.origin.x) / queue.display.size.width;
        const double y = (point.y - queue.display.origin.y) / queue.display.size.height;
        const auto column =
            std::min(static_cast<size_t>(x * static_cast<double>(width)), width - 1);
        const auto row = std::min(static_cast<size_t>(y * static_cast<double>(height)), height - 1);
        const auto* pixel = bytes + row * stride + 4 * column;
        frame.pixels[i] = {pixel[2], pixel[1], pixel[0]};
    }
    const std::lock_guard lock(queue.mutex);
    queue.frames.push_back(frame);
    if (queue.frames.size() > 1024)
        queue.frames.pop_front();
}
@end

namespace {
std::optional<Frame> nextFrame(FrameQueue& queue, uint64_t after) {
    const std::lock_guard lock(queue.mutex);
    while (!queue.frames.empty() && queue.frames.front().displayTime <= after)
        queue.frames.pop_front();
    if (queue.frames.empty())
        return std::nullopt;
    auto result = queue.frames.front();
    queue.frames.pop_front();
    return result;
}

template <typename Predicate>
Frame waitForFrame(FrameQueue& queue, uint64_t after, Predicate matches, const char* failure) {
    Frame found;
    require(waitUntil(3500,
                      [&] {
                          while (auto next = nextFrame(queue, after)) {
                              if (matches(next->pixels)) {
                                  found = *next;
                                  return true;
                              }
                          }
                          return false;
                      }),
            failure);
    return found;
}

SCStream* startObserver(CGDirectDisplayID displayId, CaptureStartupFrameObserver* observer) {
    __block SCShareableContent* content = nil;
    __block NSError* shareFailure = nil;
    auto gate = dispatch_semaphore_create(0);
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent* value, NSError* error) {
                                   content = value;
                                   shareFailure = error;
                                   dispatch_semaphore_signal(gate);
                                 }];
    require(waitUntil(5000, [&] { return dispatch_semaphore_wait(gate, DISPATCH_TIME_NOW) == 0; }),
            "Timed out listing capture displays");
    if (!content) {
        if (shareFailure)
            std::cerr << shareFailure.localizedDescription.UTF8String << '\n';
        throw std::runtime_error("Could not list capture displays");
    }
    SCDisplay* display = nil;
    for (SCDisplay* candidate in content.displays) {
        if (candidate.displayID == displayId)
            display = candidate;
    }
    require(display != nil, "Display unavailable to compositor observer");

    auto* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    auto* config = [SCStreamConfiguration new];
    // Observe the whole desktop without reading full-resolution frames back to the CPU.
    config.width = 480;
    config.height = static_cast<size_t>(std::lround(480.0 * static_cast<double>(display.height) /
                                                    static_cast<double>(display.width)));
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.colorSpaceName = kCGColorSpaceSRGB;
    config.showsCursor = NO;
    config.queueDepth = 5;
    config.minimumFrameInterval = CMTimeMake(1, 240);
    auto* stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
    NSError* outputError = nil;
    require([stream addStreamOutput:observer
                               type:SCStreamOutputTypeScreen
                 sampleHandlerQueue:dispatch_queue_create("capture-startup-observer",
                                                          DISPATCH_QUEUE_SERIAL)
                              error:&outputError],
            "Could not register compositor observer");

    __block NSError* startFailure = nil;
    auto started = dispatch_semaphore_create(0);
    [stream startCaptureWithCompletionHandler:^(NSError* error) {
      startFailure = error;
      dispatch_semaphore_signal(started);
    }];
    require(
        waitUntil(5000, [&] { return dispatch_semaphore_wait(started, DISPATCH_TIME_NOW) == 0; }),
        "Timed out starting compositor observer");
    if (startFailure) {
        std::cerr << startFailure.localizedDescription.UTF8String << '\n';
        throw std::runtime_error("Could not start compositor observer");
    }
    return stream;
}

struct ObservedDisplay {
    explicit ObservedDisplay(CGDirectDisplayID displayId) : id(displayId), fixture(displayId) {
        observer = [CaptureStartupFrameObserver new];
        observer->queue.display = CGDisplayBounds(id);
        observer->queue.probes = fixture.probes();
        stream = startObserver(id, observer);
    }

    ~ObservedDisplay() {
        [stream stopCaptureWithCompletionHandler:nil];
    }

    Frame waitForDesktop(uint64_t after, const char* failure) {
        const auto colors = fixture.colors();
        return waitForFrame(
            observer->queue, after,
            [&](const Pixels& pixels) { return desktopMatches(pixels, colors); }, failure);
    }

    CGDirectDisplayID id;
    Fixture fixture;
    CaptureStartupFrameObserver* observer = nil;
    SCStream* stream = nil;
};

struct DisplayObservation {
    Frame desktop;
    Frame first;
    std::optional<Pixels> changed;
    bool complete = false;
};

void writeConfig(const QString& directory, const QString& mode) {
    QFile config(QDir(directory).filePath(QStringLiteral("config.json")));
    require(config.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "Could not write fixture settings");
    const QJsonObject settings{
        {QStringLiteral("storage"), QJsonObject{{QStringLiteral("schema_version"), 11}}},
        {QStringLiteral("global_shortcuts"),
         QJsonObject{{QStringLiteral("screenshot"), QJsonArray{QStringLiteral("Meta+Alt+Shift+9")}},
                     {QStringLiteral("screenshot_copy"), QJsonArray{}},
                     {QStringLiteral("pin_clipboard_content"), QJsonArray{}},
                     {QStringLiteral("restore_last_closed_windows"), QJsonArray{}}}},
        {QStringLiteral("updates"),
         QJsonObject{{QStringLiteral("mode"), QStringLiteral("manual")}}},
        {QStringLiteral("screenshot_selection"),
         QJsonObject{{QStringLiteral("smart_selection"), mode}}}};
    const auto encoded = QJsonDocument(settings).toJson();
    require(config.write(encoded) == encoded.size(), "Could not write fixture settings");
}

int run(const QCommandLineParser& parser) {
    const auto validateApp = [](const QString& path) {
        const QFileInfo app(path);
        require(app.isFile() && app.isExecutable(), "Expected a deployed Snow Shot executable");
        require(!app.dir().exists(QStringLiteral("__data_directory")),
                "The benchmark needs a bundle without a portable data directory");
        return app.absoluteFilePath();
    };
    const QString appPath = validateApp(parser.value(QStringLiteral("app")));
    const QString comparison = parser.value(QStringLiteral("compare-app"));
    const QString candidatePath = comparison.isEmpty() ? QString{} : validateApp(comparison);
    const QString compareSetting = parser.value(QStringLiteral("compare-env"));
    const auto separator = compareSetting.indexOf(u'=');
    require(compareSetting.isEmpty() || separator > 0, "Expected --compare-env NAME=VALUE");
    const bool compare = !comparison.isEmpty() || !compareSetting.isEmpty();
    const auto mode = parser.value(QStringLiteral("mode"));
    const bool smartSelection = mode != QStringLiteral("disabled");
    require(mode == QStringLiteral("window_sub_element") || mode == QStringLiteral("window") ||
                !smartSelection,
            "Unknown smart selection mode");
    const auto launches = parser.value(QStringLiteral("launches")).toInt();
    const auto captures = parser.value(QStringLiteral("captures")).toInt();
    const auto startupMs = parser.value(QStringLiteral("startup-ms")).toInt();
    const auto settleMs = parser.value(QStringLiteral("settle-ms")).toInt();
    const auto idleMs = parser.value(QStringLiteral("idle-ms")).toInt();
    require(launches > 0 && captures > 0 && startupMs > 0 && settleMs >= 0 && idleMs > 0,
            "Invalid capture count or timing argument");
    require(CGPreflightScreenCaptureAccess() && CGPreflightPostEventAccess(),
            "The benchmark needs Screen Recording and keyboard-event permission");

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    uint32_t count = 0;
    require(CGGetActiveDisplayList(0, nullptr, &count) == kCGErrorSuccess && count > 0,
            "Could not count active displays");
    const bool multiDisplay = parser.isSet(QStringLiteral("multi-display"));
    require(count == (multiDisplay ? 2u : 1u),
            "Use one active display, or enable --multi-display for two active displays");
    std::vector<CGDirectDisplayID> displayIds(count);
    uint32_t listed = 0;
    require(CGGetActiveDisplayList(count, displayIds.data(), &listed) == kCGErrorSuccess &&
                listed == count,
            "Could not list active displays");
    const auto cursorEvent = CGEventCreate(nullptr);
    require(cursorEvent != nullptr, "Could not read the cursor position");
    const auto cursor = CGEventGetLocation(cursorEvent);
    CFRelease(cursorEvent);
    const auto restoreCursor = qScopeGuard([&] { CGWarpMouseCursorPosition(cursor); });

    const auto id = QUuid::createUuid().toString(QUuid::Id128);
    QCoreApplication::setOrganizationName(QStringLiteral("SnowShot"));
    QCoreApplication::setApplicationName(QStringLiteral("snow_shot-e2e-") + id);
    QDir profile(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    require(!profile.exists() && QDir().mkpath(profile.path()),
            "Could not create isolated profile");
    const auto removeProfile = qScopeGuard([&] { profile.removeRecursively(); });

    std::vector<std::unique_ptr<ObservedDisplay>> displays;
    displays.reserve(displayIds.size());
    for (const auto displayId : displayIds)
        displays.push_back(std::make_unique<ObservedDisplay>(displayId));
    // A stream can start with no complete frame until the desktop changes.
    const auto startTime = mach_absolute_time();
    for (const auto& display : displays)
        display->fixture.recolor(3);
    for (const auto& display : displays)
        display->waitForDesktop(startTime, "Fixture did not reach a display");

    auto writeRow = [](const QJsonObject& row) {
        std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << std::endl;
    };

    for (int launch = 0; launch < launches; ++launch) {
        writeConfig(profile.path(), mode);
        QProcess child;
        child.setStandardOutputFile(QProcess::nullDevice());
        child.setStandardErrorFile(parser.value(QStringLiteral("child-log")), QIODevice::Append);
        const bool candidate = compare && (launch % 4 == 1 || launch % 4 == 2);
        const auto arm = candidate ? QStringLiteral("candidate") : QStringLiteral("baseline");
        if (candidate && !compareSetting.isEmpty()) {
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(compareSetting.left(separator), compareSetting.mid(separator + 1));
            child.setProcessEnvironment(environment);
        }
        child.start(candidate && !candidatePath.isEmpty() ? candidatePath : appPath,
                    {QStringLiteral("--autostart"), QStringLiteral("--e2e-instance-id=") + id});
        const auto stop = qScopeGuard([&] {
            child.terminate();
            if (!child.waitForFinished(1000)) {
                child.kill();
                child.waitForFinished(1000);
            }
        });
        require(child.waitForStarted(3500), "Could not start Snow Shot");
        settle(startupMs);
        const auto idleBefore = residentBytes(child);
        for (int capture = 0; capture < captures; ++capture) {
            const auto preferred = static_cast<size_t>(capture) % displays.size();
            // Move the cursor only while the overlay is closed. Input during the capture
            // would initiate a separate smart-selection update.
            CGWarpMouseCursorPosition(displays[preferred]->fixture.cursor());
            const auto recolorTime = mach_absolute_time();
            const auto palette = (launch * captures + capture) % 4;
            std::vector<DisplayObservation> observations(displays.size());
            // Use a different palette on the second display to detect swapped sources.
            for (size_t index = 0; index < displays.size(); ++index) {
                displays[index]->fixture.raise();
                displays[index]->fixture.recolor(palette + static_cast<int>(index));
            }
            for (size_t index = 0; index < displays.size(); ++index) {
                observations[index].desktop = displays[index]->waitForDesktop(
                    recolorTime, "New capture fixture did not reach a display");
            }
            settle(25 + (launch * 13 + capture * 23) % 37);
            const auto trigger = postKey(shortcutKey, shortcutModifiers);
            for (size_t index = 0; index < displays.size(); ++index) {
                auto& observation = observations[index];
                observation.first = waitForFrame(
                    displays[index]->observer->queue, trigger,
                    [&](const Pixels& pixels) {
                        return !desktopMatches(pixels, observation.desktop.pixels);
                    },
                    "Screenshot overlay did not reach a display");
                observation.complete =
                    selectionMatches(observation.first.pixels, observation.desktop.pixels,
                                     index == preferred && smartSelection);
            }
            // Verify that each overlay remains frozen as its desktop changes underneath.
            for (size_t index = 0; index < displays.size(); ++index)
                displays[index]->fixture.recolor(palette + static_cast<int>(index) + 2);
            settle(70);
            for (size_t index = 0; index < displays.size(); ++index) {
                auto& observation = observations[index];
                auto& queue = displays[index]->observer->queue;
                while (auto later = nextFrame(queue, observation.first.displayTime)) {
                    if (!observation.changed &&
                        !desktopMatches(later->pixels, observation.first.pixels))
                        observation.changed = later->pixels;
                }
            }
            const bool complete = std::ranges::all_of(observations, &DisplayObservation::complete);
            const bool frozen = std::ranges::none_of(observations, [](const auto& observation) {
                return observation.changed.has_value();
            });
            const auto escape = postKey(kVK_Escape);
            for (const auto& display : displays)
                display->waitForDesktop(escape, "Escape did not close every overlay");

            const auto& selected = observations[preferred];
            QJsonObject row{
                {QStringLiteral("launch"), launch + 1},
                {QStringLiteral("capture"), capture + 1},
                {QStringLiteral("arm"), arm},
                {QStringLiteral("mode"), mode},
                {QStringLiteral("trigger_ticks"), static_cast<qint64>(trigger)},
                {QStringLiteral("first_frame_ticks"),
                 static_cast<qint64>(selected.first.displayTime)},
                {QStringLiteral("latency_ms"),
                 millisecondsBetween(trigger, selected.first.displayTime)},
                {QStringLiteral("first_frame_ok"), complete && frozen},
                {QStringLiteral("selection_and_capture_ok"), complete},
                {QStringLiteral("frozen_ok"), frozen},
                {QStringLiteral("changed_pixels"),
                 selected.changed ? jsonPixels(*selected.changed) : QJsonArray{}},
                {QStringLiteral("first_frame_pixels"), jsonPixels(selected.first.pixels)},
                {QStringLiteral("desktop_pixels"), jsonPixels(selected.desktop.pixels)}};
            if (multiDisplay) {
                QJsonArray displayResults;
                for (size_t index = 0; index < displays.size(); ++index) {
                    const auto& observation = observations[index];
                    displayResults.append(QJsonObject{
                        {QStringLiteral("id"), static_cast<qint64>(displays[index]->id)},
                        {QStringLiteral("preferred"), index == preferred},
                        {QStringLiteral("latency_ms"),
                         millisecondsBetween(trigger, observation.first.displayTime)},
                        {QStringLiteral("first_frame_ok"),
                         observation.complete && !observation.changed},
                        {QStringLiteral("changed_pixels"),
                         observation.changed ? jsonPixels(*observation.changed) : QJsonArray{}},
                        {QStringLiteral("first_frame_pixels"),
                         jsonPixels(observation.first.pixels)},
                        {QStringLiteral("desktop_pixels"), jsonPixels(observation.desktop.pixels)},
                    });
                }
                row.insert(QStringLiteral("displays"), displayResults);
            }
            writeRow(row);
            require(complete && frozen, "Incorrect first frame, see pixel probes");
            settle(settleMs);
        }
        settle(idleMs);
        const auto idleAfter = residentBytes(child);
        settle(150);
        writeRow({{QStringLiteral("launch"), launch + 1},
                  {QStringLiteral("arm"), arm},
                  {QStringLiteral("idle_before_bytes"), idleBefore},
                  {QStringLiteral("idle_after_bytes"), idleAfter},
                  {QStringLiteral("idle_after_confirmation_bytes"), residentBytes(child)}});
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        QCoreApplication app(argc, argv);
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral(
            "Native macOS screenshot startup benchmark using WindowServer display timestamps"));
        parser.addHelpOption();
        parser.addOption({QStringLiteral("app"), QStringLiteral("Deployed Snow Shot executable."),
                          QStringLiteral("path")});
        parser.addOption({QStringLiteral("compare-app"),
                          QStringLiteral("Alternate with a second executable in ABBA order."),
                          QStringLiteral("path")});
        parser.addOption({QStringLiteral("multi-display"),
                          QStringLiteral("Validate the first frame on every active display.")});
        parser.addOption({QStringLiteral("compare-env"),
                          QStringLiteral("Set a child environment variable in the candidate arm."),
                          QStringLiteral("name=value")});
        parser.addOption({QStringLiteral("child-log"), QStringLiteral("Snow Shot stderr."),
                          QStringLiteral("path"), QProcess::nullDevice()});
        parser.addOption({QStringLiteral("mode"), QStringLiteral("Smart-selection mode."),
                          QStringLiteral("mode"), QStringLiteral("window_sub_element")});
        parser.addOption({QStringLiteral("launches"), QStringLiteral("Fresh process count."),
                          QStringLiteral("count"), QStringLiteral("12")});
        parser.addOption({QStringLiteral("captures"), QStringLiteral("Captures per process."),
                          QStringLiteral("count"), QStringLiteral("4")});
        parser.addOption({QStringLiteral("startup-ms"), QStringLiteral("Startup settling time."),
                          QStringLiteral("ms"), QStringLiteral("1800")});
        parser.addOption({QStringLiteral("settle-ms"), QStringLiteral("Time between captures."),
                          QStringLiteral("ms"), QStringLiteral("400")});
        parser.addOption({QStringLiteral("idle-ms"),
                          QStringLiteral("Settling time before final RSS."), QStringLiteral("ms"),
                          QStringLiteral("2800")});
        parser.process(app);
        try {
            return run(parser);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
