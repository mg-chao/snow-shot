// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "snow_shot/presentation/screenshotexportcoordinator.h"
#include <QSemaphore>
#include <utility>

class SaturatedExportQueue final {
  public:
    explicit SaturatedExportQueue(QObject& receiver) {
        auto& coordinator = ScreenshotExportCoordinator::shared();
        while (coordinator
                   .submit(
                       &receiver, ScreenshotExportCoordinator::Priority::Foreground,
                       [gate = m_gate](const auto&) {
                           gate->acquire();
                           return ScreenshotExportTaskResult{};
                       },
                       [](auto) {})
                   .isValid())
            ++m_count;
    }
    ~SaturatedExportQueue() {
        release();
    }
    void release() {
        m_gate->release(std::exchange(m_count, 0));
    }

  private:
    std::shared_ptr<QSemaphore> m_gate = std::make_shared<QSemaphore>();
    int m_count = 0;
};
