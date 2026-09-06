#pragma once

#include <QtCore/qglobal.h>

QT_BEGIN_NAMESPACE
class QApplication;
QT_END_NAMESPACE

namespace snow::image_viewer {

void configureViewerApplicationIdentity(QApplication& app);
void configureViewerApplicationAppearance(QApplication& app);

} // namespace snow::image_viewer
