#pragma once

#include <QString>

#include <memory>

class QTemporaryDir;

namespace snow::image_viewer {

class TemporaryFileLease final {
  public:
    ~TemporaryFileLease();

    TemporaryFileLease(const TemporaryFileLease&) = delete;
    TemporaryFileLease& operator=(const TemporaryFileLease&) = delete;

    [[nodiscard]] static std::shared_ptr<const TemporaryFileLease>
    adopt(const QString& path, std::shared_ptr<QTemporaryDir> directory, QString* error = nullptr);

    [[nodiscard]] const QString& path() const noexcept {
        return path_;
    }

  private:
    TemporaryFileLease(QString path, std::shared_ptr<QTemporaryDir> directory);

    QString path_;
    std::shared_ptr<QTemporaryDir> directory_;
};

} // namespace snow::image_viewer
