#include "editing/temporary_file_lease.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <utility>

namespace snow::image_viewer {

TemporaryFileLease::TemporaryFileLease(QString path, std::shared_ptr<QTemporaryDir> directory)
    : path_(std::move(path)), directory_(std::move(directory)) {}

TemporaryFileLease::~TemporaryFileLease() {
    QFile::remove(path_);
}

std::shared_ptr<const TemporaryFileLease>
TemporaryFileLease::adopt(const QString& path, std::shared_ptr<QTemporaryDir> directory,
                          QString* error) {
    const QFileInfo file(path);
    if (!directory || !directory->isValid() || !file.isFile() ||
        QDir::cleanPath(file.absolutePath()) !=
            QDir::cleanPath(QFileInfo(directory->path()).absoluteFilePath())) {
        if (error)
            *error = QStringLiteral("The temporary file lease is invalid.");
        return {};
    }
    return std::shared_ptr<const TemporaryFileLease>(
        new TemporaryFileLease(file.absoluteFilePath(), std::move(directory)));
}

} // namespace snow::image_viewer
