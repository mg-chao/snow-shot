#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <functional>

namespace adqt::widgets::detail {

struct TimingConfig {
  int frameIntervalMs = 25;
  int spinnerCycleMs = 1000;
  int waveDurationMs = 560;
  int menuOpenDelayMs = 0;
  int menuCloseDelayMs = 100;
  int loadingDelayMs = 0;
};

using DeadlineCallback = std::function<void()>;
using FrameCallback = std::function<void(qint64 nowMs, qint64 deltaMs)>;

void scheduleTimingTask(QObject* context, const QString& key, int delayMs,
                        DeadlineCallback callback);
void deferTimingTask(QObject* context, const QString& key, DeadlineCallback callback);
void cancelTimingTask(QObject* context, const QString& key);

void setFrameSubscription(QObject* context, const QString& key, bool enabled,
                          FrameCallback callback);
void clearFrameSubscription(QObject* context, const QString& key);

qint64 timingNowMs();

TimingConfig currentTimingConfig();
int resolveLoadingDelayMs(int componentDelayMs);
int resolveMenuOpenDelayMs(int componentDelayMs);
int resolveMenuCloseDelayMs(int componentDelayMs);
int spinnerCycleDurationMs();
int waveDurationMs();

}  // namespace adqt::widgets::detail
