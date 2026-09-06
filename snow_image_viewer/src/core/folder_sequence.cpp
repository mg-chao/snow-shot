#include "core/folder_sequence.h"

#include <snow/image/service.h>

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace snow::image_viewer {
namespace {

const QSet<QString>& supportedSuffixSet() {
    static const QSet<QString> suffixes = []() {
        QSet<QString> result;
        const snow::image::Service service;
        for (const snow::image::FormatCapability& capability : service.formats()) {
            for (const std::string_view extension : capability.extensions) {
                result.insert(
                    QString::fromUtf8(extension.data(), static_cast<qsizetype>(extension.size()))
                        .toLower());
            }
        }
        return result;
    }();
    return suffixes;
}

const QStringList& cachedSupportedSuffixes() {
    static const QStringList suffixes = []() {
        QStringList result(supportedSuffixSet().cbegin(), supportedSuffixSet().cend());
        result.sort(Qt::CaseInsensitive);
        return result;
    }();
    return suffixes;
}

} // namespace

QStringList FolderSequence::supportedSuffixes() {
    return cachedSupportedSuffixes();
}

bool FolderSequence::isSupportedFile(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return !suffix.isEmpty() && supportedSuffixSet().contains(suffix);
}

void FolderSequence::setCurrentFile(const QString& filePath) {
    const QFileInfo currentInfo(filePath);
    if (!currentInfo.exists() || !currentInfo.isFile()) {
        directoryPath_.clear();
        files_.clear();
        currentIndex_ = -1;
        return;
    }

    const QDir directory = currentInfo.absoluteDir();
    const QString absoluteCurrent = currentInfo.absoluteFilePath();
    const QString directoryPath = directory.absolutePath();
    if (directoryPath_ == directoryPath && setCurrentIndex(absoluteCurrent)) {
        return;
    }

    files_.clear();
    currentIndex_ = -1;
    directoryPath_ = directoryPath;
    const QFileInfoList entries =
        directory.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    for (const QFileInfo& entry : entries) {
        if (isSupportedFile(entry.absoluteFilePath())) {
            files_.append(entry.absoluteFilePath());
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(files_.begin(), files_.end(), [&collator](const QString& left, const QString& right) {
        return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
    });

    setCurrentIndex(absoluteCurrent);
}

bool FolderSequence::setCurrentIndex(const QString& absoluteFilePath) {
    currentIndex_ = -1;
    for (qsizetype index = 0; index < files_.size(); ++index) {
        if (QString::compare(files_.at(index), absoluteFilePath, Qt::CaseInsensitive) == 0) {
            currentIndex_ = index;
            return true;
        }
    }
    return false;
}

QString FolderSequence::currentFile() const {
    return currentIndex_ >= 0 && currentIndex_ < files_.size() ? files_.at(currentIndex_)
                                                               : QString();
}

QString FolderSequence::previousFile() const {
    return hasPrevious() ? files_.at(currentIndex_ - 1) : QString();
}

QString FolderSequence::nextFile() const {
    return hasNext() ? files_.at(currentIndex_ + 1) : QString();
}

bool FolderSequence::hasPrevious() const {
    return currentIndex_ > 0;
}

bool FolderSequence::hasNext() const {
    return currentIndex_ >= 0 && currentIndex_ + 1 < files_.size();
}

qsizetype FolderSequence::count() const {
    return files_.size();
}

qsizetype FolderSequence::currentIndex() const {
    return currentIndex_;
}

} // namespace snow::image_viewer
