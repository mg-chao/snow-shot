#ifndef SNOW_SHOT_PLATFORM_WINDOWS_SHELLCONTEXTMENUREGISTRATION_H
#define SNOW_SHOT_PLATFORM_WINDOWS_SHELLCONTEXTMENUREGISTRATION_H

#include "snow_shot/platform/windows/shellcontextmenumode.h"

#include <QByteArray>
#include <QMap>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QtTypes>
#include <functional>
#include <optional>

namespace snow_shot::platform::windows {

struct ShellContextMenuRegistryValue {
    quint32 type = 0;
    QByteArray data;

    friend bool operator==(const ShellContextMenuRegistryValue&,
                           const ShellContextMenuRegistryValue&) = default;
};

struct ShellContextMenuRegistryKey {
    bool exists = false;
    QMap<QString, ShellContextMenuRegistryValue> values;
    QMap<QString, ShellContextMenuRegistryKey> children;

    friend bool operator==(const ShellContextMenuRegistryKey&,
                           const ShellContextMenuRegistryKey&) = default;
};

// All paths are relative to HKCU. Injection keeps tests away from the user's registrations.
struct ShellContextMenuRegistryAccess {
    std::function<bool(const QString&, QStringList*, QString*)> childKeys;
    std::function<bool(const QString&, ShellContextMenuRegistryKey*, QString*)> read;
    std::function<bool(const QString&, const ShellContextMenuRegistryKey&, QString*)> replace;
    std::function<void()> notifyChanged;
};

struct ShellContextMenuRegistrationSnapshot {
    bool valid = false;
    QMap<QString, ShellContextMenuRegistryKey> keys;
    QString error;
};

struct ShellContextMenuResult {
    bool success = false;
    QString error;
};

class ShellContextMenuRegistration final {
  public:
    struct Options {
        QString classesRoot;
        QString executablePath;
        QString productId;
        QString verbName;
        QStringList fileExtensions;
        ShellContextMenuRegistryAccess registry;
        QString packagedPreferencePath;
        std::optional<bool> useLocalCommandServer;
    };

    ShellContextMenuRegistration();
    explicit ShellContextMenuRegistration(Options options);

    [[nodiscard]] static bool isSupported();
    [[nodiscard]] static ShellContextMenuMode modeFromValue(const QString& value);
    [[nodiscard]] static ShellMenuPreferences preferences(const QMap<QString, QJsonValue>& values);
    [[nodiscard]] ShellContextMenuResult
    refreshLabels(const ShellMenuPreferences& preferences) const;
    [[nodiscard]] ShellContextMenuRegistrationSnapshot snapshot() const;
    [[nodiscard]] bool restore(const ShellContextMenuRegistrationSnapshot& previous,
                               QString* error = nullptr) const;
    [[nodiscard]] ShellContextMenuResult
    change(const ShellMenuPreferences& preferences,
           const std::function<bool()>& commitPreference = {}) const;

  private:
    [[nodiscard]] ShellContextMenuResult
    changePackagedMode(const ShellMenuPreferences& preferences,
                       const std::function<bool()>& commitPreference) const;
    [[nodiscard]] bool ownedByProduct(const ShellContextMenuRegistryKey& key) const;
    [[nodiscard]] bool apply(const ShellMenuPreferences& preferences,
                             const ShellContextMenuRegistrationSnapshot& previous,
                             QString* error) const;
    QString classPath(size_t index) const;
    QString actionPath(size_t index, const QString& extension) const;
    QString classId(size_t index) const;
    QString m_classesRoot;
    QString m_associationsRoot;
    QString m_executable;
    QString m_productId;
    QString m_verbName;
    QStringList m_extensions;
    ShellContextMenuRegistryAccess m_registry;
    QString m_packagedPreferencePath;
    bool m_useLocalCommandServer;
};

} // namespace snow_shot::platform::windows

#endif // SNOW_SHOT_PLATFORM_WINDOWS_SHELLCONTEXTMENUREGISTRATION_H
