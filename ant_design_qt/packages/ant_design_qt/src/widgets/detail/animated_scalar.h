#pragma once

#include "timing_hub.h"

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace adqt::widgets::detail {

inline qreal clampAnimationProgress(qreal value) { return std::clamp(value, 0.0, 1.0); }

inline qreal cubicBezierCoordinate(qreal p1, qreal p2, qreal t) {
  const qreal oneMinusT = 1.0 - t;
  return 3.0 * oneMinusT * oneMinusT * t * p1 + 3.0 * oneMinusT * t * t * p2 + t * t * t;
}

inline qreal cubicBezierSlope(qreal p1, qreal p2, qreal t) {
  const qreal oneMinusT = 1.0 - t;
  return 3.0 * oneMinusT * oneMinusT * p1 + 6.0 * oneMinusT * t * (p2 - p1) +
         3.0 * t * t * (1.0 - p2);
}

inline qreal cubicBezierEase(qreal progress, qreal x1, qreal y1, qreal x2, qreal y2) {
  const qreal targetX = clampAnimationProgress(progress);
  qreal curveT = targetX;

  for (int iteration = 0; iteration < 6; ++iteration) {
    const qreal xEstimate = cubicBezierCoordinate(x1, x2, curveT) - targetX;
    const qreal slope = cubicBezierSlope(x1, x2, curveT);
    if (std::abs(slope) < 1e-6) {
      break;
    }
    curveT = clampAnimationProgress(curveT - xEstimate / slope);
  }

  qreal lower = 0.0;
  qreal upper = 1.0;
  for (int iteration = 0; iteration < 10; ++iteration) {
    const qreal xEstimate = cubicBezierCoordinate(x1, x2, curveT);
    if (std::abs(xEstimate - targetX) < 1e-5) {
      break;
    }
    if (xEstimate < targetX) {
      lower = curveT;
    } else {
      upper = curveT;
    }
    curveT = (lower + upper) / 2.0;
  }

  return cubicBezierCoordinate(y1, y2, curveT);
}

inline qreal standardEaseInOut(qreal progress) {
  return cubicBezierEase(progress, 0.42, 0.0, 0.58, 1.0);
}

class FrameLoop final {
 public:
  FrameLoop() = default;
  FrameLoop(const FrameLoop&) = delete;
  FrameLoop& operator=(const FrameLoop&) = delete;
  ~FrameLoop() { stop(); }

  void configure(QObject* context, QString key, FrameCallback callback) {
    stop();
    context_ = context;
    key_ = std::move(key);
    callback_ = std::move(callback);
  }

  void setRunning(bool running) {
    if (running == running_) {
      return;
    }
    if (!running) {
      stop();
      return;
    }
    if (!context_ || !callback_ || key_.isEmpty()) {
      return;
    }
    setFrameSubscription(context_, key_, true, [this](qint64 nowMs, qint64 deltaMs) {
      if (callback_) {
        callback_(nowMs, deltaMs);
      }
    });
    running_ = true;
  }

  void stop() {
    if (running_ && context_ && !key_.isEmpty()) {
      clearFrameSubscription(context_, key_);
    }
    running_ = false;
  }

  bool running() const { return running_; }

 private:
  QObject* context_ = nullptr;
  QString key_;
  FrameCallback callback_;
  bool running_ = false;
};

class AnimatedScalar final {
 public:
  using UpdateCallback = std::function<void()>;
  using Easing = std::function<qreal(qreal)>;

  AnimatedScalar() = default;
  AnimatedScalar(const AnimatedScalar&) = delete;
  AnimatedScalar& operator=(const AnimatedScalar&) = delete;
  ~AnimatedScalar() { stop(); }

  void configure(QObject* context, QString key, UpdateCallback onUpdate) {
    stop();
    context_ = context;
    key_ = std::move(key);
    onUpdate_ = std::move(onUpdate);
  }

  void snapTo(qreal value) {
    stop();
    value_ = value;
    startValue_ = value;
    targetValue_ = value;
    durationMs_ = 0;
    startMs_ = 0;
  }

  void animateTo(qreal target, int durationMs, Easing easing = standardEaseInOut) {
    targetValue_ = target;
    if (!context_ || !onUpdate_ || key_.isEmpty() || durationMs <= 0) {
      snapTo(target);
      return;
    }

    if (std::abs(value_ - target) < 0.0001) {
      value_ = target;
      startValue_ = target;
      stop();
      return;
    }

    startValue_ = value_;
    durationMs_ = durationMs;
    startMs_ = timingNowMs();
    easing_ = easing ? std::move(easing) : Easing(standardEaseInOut);

    if (running_) {
      return;
    }

    setFrameSubscription(context_, key_, true, [this](qint64 nowMs, qint64) { tick(nowMs); });
    running_ = true;
  }

  void stop() {
    if (running_ && context_ && !key_.isEmpty()) {
      clearFrameSubscription(context_, key_);
    }
    running_ = false;
  }

  qreal value() const { return value_; }
  qreal target() const { return targetValue_; }
  bool running() const { return running_; }

 private:
  void tick(qint64 nowMs) {
    if (!running_) {
      return;
    }

    if (durationMs_ <= 0) {
      value_ = targetValue_;
      stop();
      if (onUpdate_) {
        onUpdate_();
      }
      return;
    }

    const qint64 elapsed = std::max<qint64>(0, nowMs - startMs_);
    const qreal progress = clampAnimationProgress(static_cast<qreal>(elapsed) / durationMs_);
    const qreal eased = easing_ ? easing_(progress) : progress;
    value_ = startValue_ + (targetValue_ - startValue_) * eased;
    if (onUpdate_) {
      onUpdate_();
    }

    if (progress >= 1.0) {
      value_ = targetValue_;
      stop();
    }
  }

  QObject* context_ = nullptr;
  QString key_;
  UpdateCallback onUpdate_;
  Easing easing_ = standardEaseInOut;
  qreal value_ = 0.0;
  qreal startValue_ = 0.0;
  qreal targetValue_ = 0.0;
  qint64 startMs_ = 0;
  int durationMs_ = 0;
  bool running_ = false;
};

}  // namespace adqt::widgets::detail
