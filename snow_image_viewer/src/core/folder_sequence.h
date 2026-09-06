#pragma once

#include <QString>
#include <QStringList>

namespace snow::image_viewer {

class FolderSequence final {
  public:
    void setCurrentFile(const QString& filePath);

    QString currentFile() const;
    QString previousFile() const;
    QString nextFile() const;
    bool hasPrevious() const;
    bool hasNext() const;
    qsizetype count() const;
    qsizetype currentIndex() const;

    static QStringList supportedSuffixes();
    static bool isSupportedFile(const QString& filePath);

  private:
    bool setCurrentIndex(const QString& absoluteFilePath);

    QString directoryPath_;
    QStringList files_;
    qsizetype currentIndex_ = -1;
};

} // namespace snow::image_viewer
