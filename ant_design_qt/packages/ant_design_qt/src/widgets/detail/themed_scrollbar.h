#pragma once

class QScrollBar;

namespace adqt::widgets::detail {

void applyThemedScrollBar(QScrollBar* bar, int extent = 8, int radius = 4, int inset = 0,
                          int marginStart = 0, int marginEnd = 0);

}  // namespace adqt::widgets::detail
