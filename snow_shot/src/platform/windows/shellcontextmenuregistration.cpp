#include "snow_shot/platform/windows/shellcontextmenuregistration.h"

#include "snow_shot/app/edition.h"
#include "snow_shot/app/distribution.h"
#include "snow_shot/app/supportedpinfiles.h"
#include "snow_shot/shortcuts/menuaccesskey.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
#include <shlobj.h>
#include "snow_shot/platform/windows/packagedcontextmenupreference.h"
#endif

namespace snow_shot::platform::windows {
namespace {
constexpr quint32 kRegistryString = 1;

QString contextMenuText(const char* source) {
    return QCoreApplication::translate("ShellContextMenuRegistration", source);
}

QString menuLabel(size_t index, char key) {
    static constexpr const char* titles[] = {
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Pin to screen"),
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Pin to screen and annotate"),
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Stitch images"),
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Split images"),
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Edit images"),
        QT_TRANSLATE_NOOP("ShellContextMenuRegistration", "Compress images"),
    };
    QString label = contextMenuText(titles[index]);
    label.replace(QLatin1Char('&'), QStringLiteral("&&"));
    if (key != '.')
        label += QStringLiteral(" (&%1)").arg(QChar::fromLatin1(key));
    return label;
}

ShellContextMenuRegistryValue stringValue(const QString& value) {
    const auto native = value.toStdU16String();
    return {kRegistryString,
            QByteArray(reinterpret_cast<const char*>(native.c_str()),
                       static_cast<qsizetype>((native.size() + 1) * sizeof(char16_t)))};
}

QString readString(const ShellContextMenuRegistryValue& value) {
    if (value.type != kRegistryString || value.data.size() < 2 || value.data.size() % 2 != 0)
        return {};
    // QByteArray does not promise char16_t alignment.
    std::u16string native(static_cast<size_t>(value.data.size() / 2), u'\0');
    std::copy(value.data.cbegin(), value.data.cend(), reinterpret_cast<char*>(native.data()));
    const auto terminator = native.find(u'\0');
    if (terminator == std::u16string::npos)
        return {};
    return QString::fromStdU16String(native.substr(0, terminator));
}

QString verbPath(const QString& root, const QString& extension, const QString& verb) {
    return root + QStringLiteral("\\") + extension + QStringLiteral("\\shell\\") + verb;
}

#ifdef Q_OS_WIN
QString registryError(LSTATUS status) {
    return contextMenuText(
               QT_TRANSLATE_NOOP(
                   "ShellContextMenuRegistration",
                   "Could not access the Windows context menu registry. Windows error %1"))
        .arg(status);
}

bool listChildren(HKEY key, QStringList* children, QString* error) {
    for (DWORD index = 0;; ++index) {
        wchar_t name[256]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        const auto status =
            RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            return true;
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        children->append(QString::fromWCharArray(name, length));
    }
}

bool readNativeKey(HKEY key, ShellContextMenuRegistryKey* result, QString* error) {
    DWORD maxName = 0;
    DWORD maxData = 0;
    auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, &maxName, &maxData, nullptr, nullptr);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = registryError(status);
        return false;
    }
    std::vector<wchar_t> name(static_cast<size_t>(maxName) + 1);
    QByteArray data(static_cast<qsizetype>(maxData), '\0');
    result->exists = true;
    for (DWORD index = 0;; ++index) {
        DWORD nameSize = static_cast<DWORD>(name.size());
        DWORD dataSize = static_cast<DWORD>(data.size());
        DWORD type = 0;
        status = RegEnumValueW(key, index, name.data(), &nameSize, nullptr, &type,
                               reinterpret_cast<BYTE*>(data.data()), &dataSize);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        result->values.insert(QString::fromWCharArray(name.data(), nameSize),
                              {type, data.first(static_cast<qsizetype>(dataSize))});
    }
    QStringList children;
    if (!listChildren(key, &children, error))
        return false;
    for (const auto& nameValue : children) {
        HKEY child = nullptr;
        status = RegOpenKeyExW(key, nameValue.toStdWString().c_str(), 0, KEY_READ, &child);
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        ShellContextMenuRegistryKey snapshot;
        const bool success = readNativeKey(child, &snapshot, error);
        RegCloseKey(child);
        if (!success)
            return false;
        result->children.insert(nameValue, std::move(snapshot));
    }
    return true;
}

bool writeNativeKey(HKEY key, const ShellContextMenuRegistryKey& snapshot, QString* error) {
    for (auto value = snapshot.values.cbegin(); value != snapshot.values.cend(); ++value) {
        if (value->data.size() > static_cast<qsizetype>(std::numeric_limits<DWORD>::max())) {
            if (error)
                *error = registryError(ERROR_INVALID_DATA);
            return false;
        }
        const auto status = RegSetValueExW(key, value.key().toStdWString().c_str(), 0, value->type,
                                           reinterpret_cast<const BYTE*>(value->data.constData()),
                                           static_cast<DWORD>(value->data.size()));
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
    }
    for (auto item = snapshot.children.cbegin(); item != snapshot.children.cend(); ++item) {
        if (!item->exists)
            continue;
        HKEY child = nullptr;
        const auto status =
            RegCreateKeyExW(key, item.key().toStdWString().c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &child, nullptr);
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        const bool success = writeNativeKey(child, item.value(), error);
        RegCloseKey(child);
        if (!success)
            return false;
    }
    return true;
}
#endif

ShellContextMenuRegistryAccess nativeRegistryAccess() {
    ShellContextMenuRegistryAccess access;
#ifdef Q_OS_WIN
    access.childKeys = [](const QString& path, QStringList* children, QString* error) {
        HKEY key = nullptr;
        const auto status =
            RegOpenKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(), 0, KEY_READ, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            return true;
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        const bool success = listChildren(key, children, error);
        RegCloseKey(key);
        return success;
    };
    access.read = [](const QString& path, ShellContextMenuRegistryKey* result, QString* error) {
        HKEY key = nullptr;
        const auto status =
            RegOpenKeyExW(HKEY_CURRENT_USER, path.toStdWString().c_str(), 0, KEY_READ, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            return true;
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        const bool success = readNativeKey(key, result, error);
        RegCloseKey(key);
        return success;
    };
    access.replace = [](const QString& path, const ShellContextMenuRegistryKey& snapshot,
                        QString* error) {
        const auto nativePath = path.toStdWString();
        const auto deleteStatus = RegDeleteTreeW(HKEY_CURRENT_USER, nativePath.c_str());
        if (deleteStatus != ERROR_SUCCESS && deleteStatus != ERROR_FILE_NOT_FOUND &&
            deleteStatus != ERROR_PATH_NOT_FOUND) {
            if (error)
                *error = registryError(deleteStatus);
            return false;
        }
        if (!snapshot.exists)
            return true;
        HKEY key = nullptr;
        const auto status =
            RegCreateKeyExW(HKEY_CURRENT_USER, nativePath.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) {
            if (error)
                *error = registryError(status);
            return false;
        }
        const bool success = writeNativeKey(key, snapshot, error);
        RegCloseKey(key);
        return success;
    };
    access.notifyChanged = [] {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    };
#endif
    return access;
}

bool needsLocalCommandServer() {
#if defined(Q_OS_WIN) && defined(_M_X64)
    USHORT processMachine = 0;
    USHORT nativeMachine = 0;
    return IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) &&
           nativeMachine == IMAGE_FILE_MACHINE_ARM64;
#else
    return false;
#endif
}
} // namespace

ShellContextMenuRegistration::ShellContextMenuRegistration()
    : ShellContextMenuRegistration(Options{}) {}

ShellContextMenuRegistration::ShellContextMenuRegistration(Options options)
    : m_classesRoot(options.classesRoot.isEmpty() ? QStringLiteral("Software\\Classes")
                                                  : options.classesRoot),
      m_associationsRoot(m_classesRoot + QStringLiteral("\\SystemFileAssociations")),
      m_executable(QDir::toNativeSeparators(
          QDir::cleanPath(options.executablePath.isEmpty() ? QCoreApplication::applicationFilePath()
                                                           : options.executablePath))),
      m_productId(options.productId.isEmpty() ? app::edition::productId() : options.productId),
      m_verbName(options.verbName.isEmpty() ? app::edition::registryName() + QStringLiteral(".Edit")
                                            : options.verbName),
      m_registry(options.registry.read ? std::move(options.registry) : nativeRegistryAccess()),
      m_packagedPreferencePath(std::move(options.packagedPreferencePath)),
      m_useLocalCommandServer(options.useLocalCommandServer.value_or(needsLocalCommandServer())) {
#ifdef Q_OS_WIN
    if (m_packagedPreferencePath.isEmpty() && app::distribution::isStoreManaged())
        m_packagedPreferencePath = QString::fromStdWString(packaged_context_menu::preferencePath());
#endif
    const auto extensions = options.fileExtensions.isEmpty() ? app::supportedPinFileExtensions()
                                                             : std::move(options.fileExtensions);
    for (auto extension : extensions) {
        extension = extension.toLower();
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        // Extension paths must not introduce another registry namespace.
        if (extension.size() > 1 &&
            std::all_of(extension.cbegin() + 1, extension.cend(),
                        [](QChar character) { return character.isLetterOrNumber(); }) &&
            !m_extensions.contains(extension))
            m_extensions.append(extension);
    }
}

bool ShellContextMenuRegistration::isSupported() {
#ifdef Q_OS_WIN
    return !app::distribution::isStoreManaged();
#else
    return false;
#endif
}

ShellContextMenuMode ShellContextMenuRegistration::modeFromValue(const QString& value) {
    // The legacy image_files preference includes documents; do not reinterpret saved values.
    if (value == QStringLiteral("image_files"))
        return ShellContextMenuMode::ImageAndDocumentFiles;
    if (value == QStringLiteral("images_only"))
        return ShellContextMenuMode::ImageFiles;
    return ShellContextMenuMode::Disabled;
}

QString ShellContextMenuRegistration::classId(size_t index) const {
    return QStringLiteral("{3F1E90A2-7D4B-4F21-9D8A-6B0C5E2F70%1%2}")
        .arg(app::edition::isMini ? QStringLiteral("C") : QStringLiteral("B"))
        .arg(index);
}
QString ShellContextMenuRegistration::classPath(size_t index) const {
    return m_classesRoot + QStringLiteral("\\CLSID\\") + classId(index);
}
QString ShellContextMenuRegistration::actionPath(size_t index, const QString& extension) const {
    const auto action = QString::fromLatin1(shellActionIds[index]);
    return verbPath(m_associationsRoot, extension,
                    app::edition::registryName() + QStringLiteral(".File.") + action);
}

ShellMenuPreferences
ShellContextMenuRegistration::preferences(const QMap<QString, QJsonValue>& values) {
    ShellMenuPreferences result;
    result.mode =
        modeFromValue(values.value(QStringLiteral("system/context_menu_mode")).toString());
    const auto options = values.value(QStringLiteral("system/context_menu_options")).toArray();
    for (size_t i = 0; i < shellActionIds.size(); ++i) {
        const auto id = QString::fromLatin1(shellActionIds[i]);
        const auto bindings =
            values.value(QStringLiteral("context_menu_shortcuts/") + id).toArray();
        const auto key =
            bindings.isEmpty()
                ? QString{}
                : bindings.first().toObject().value(QStringLiteral("portable")).toString();
        result.actions[i] = !options.contains(id) ? '-'
                            : key.isEmpty()       ? '.'
                                                  : key.front().toLatin1();
    }
    return result;
}

bool ShellContextMenuRegistration::ownedByProduct(const ShellContextMenuRegistryKey& key) const {
    return key.exists && readString(key.values.value(QStringLiteral("ProductId"))) == m_productId;
}

ShellContextMenuRegistrationSnapshot ShellContextMenuRegistration::snapshot() const {
    ShellContextMenuRegistrationSnapshot result;
    if (app::distribution::isStoreManaged()) {
        result.valid = true;
        return result;
    }
    if (!m_registry.childKeys || !m_registry.read || !m_registry.replace) {
        result.error = contextMenuText(
            QT_TRANSLATE_NOOP("ShellContextMenuRegistration",
                              "The Windows context menu is only supported on Windows"));
        return result;
    }
    QStringList registeredExtensions;
    if (!m_registry.childKeys(m_associationsRoot, &registeredExtensions, &result.error))
        return result;
    for (const auto& extension : m_extensions)
        if (!registeredExtensions.contains(extension, Qt::CaseInsensitive))
            registeredExtensions.append(extension);
    const auto capture = [&](const QString& path, bool registered = true) {
        ShellContextMenuRegistryKey key;
        if (!m_registry.read(path, &key, &result.error))
            return false;
        if (registered || ownedByProduct(key))
            result.keys.insert(path, std::move(key));
        return true;
    };
    for (const auto& extension : registeredExtensions) {
        if (!extension.startsWith(QLatin1Char('.')) || extension.contains(QLatin1Char('\\')) ||
            extension.contains(QLatin1Char('/')))
            continue;
        const bool supported = m_extensions.contains(extension, Qt::CaseInsensitive);
        if (!capture(verbPath(m_associationsRoot, extension, m_verbName), supported))
            return result;
        for (size_t i = 0; i < shellActionIds.size(); ++i)
            if (!capture(actionPath(i, extension.toLower()), supported))
                return result;
    }
    for (size_t i = 0; i < shellActionIds.size(); ++i) {
        // Reconcile wildcard verbs left by earlier builds; new verbs are extension-specific.
        const QString oldVerb =
            app::edition::registryName() + u'.' + QString::fromLatin1(shellActionIds[i]);
        if (!capture(classPath(i)) ||
            !capture(verbPath(m_classesRoot, QStringLiteral("*"), oldVerb), false))
            return result;
    }
    result.valid = true;
    return result;
}

bool ShellContextMenuRegistration::apply(const ShellMenuPreferences& prefs,
                                         const ShellContextMenuRegistrationSnapshot& previous,
                                         QString* error) const {
    QMap<QString, ShellContextMenuRegistryKey> desired;
    for (size_t i = 0; i < shellActionIds.size(); ++i) {
        if (prefs.mode == ShellContextMenuMode::Disabled || prefs.actions[i] == '-')
            continue;
        const auto label = menuLabel(i, prefs.actions[i]);
        const auto enabledKey = [&](const QString& path) {
            auto key = previous.keys.value(path);
            key.exists = true;
            key.values.insert(QString(), stringValue(label));
            key.values.insert(QStringLiteral("ProductId"), stringValue(m_productId));
            key.values.insert(QStringLiteral("InstallationExecutable"), stringValue(m_executable));
            return key;
        };
        const auto clsid = classId(i);
        const auto classKey = classPath(i);
        auto com = enabledKey(classKey);
        com.values.insert(QStringLiteral("Scope"),
                          stringValue(prefs.mode == ShellContextMenuMode::ImageFiles
                                          ? QStringLiteral("images_only")
                                          : QStringLiteral("image_files")));
        if (m_useLocalCommandServer) {
            // A native ARM64 Explorer cannot load the emulated application's x64 DLL.
            com.children.remove(QStringLiteral("InprocServer32"));
            auto& server = com.children[QStringLiteral("LocalServer32")];
            server.exists = true;
            server.values.insert(QString(),
                                 stringValue(QStringLiteral("\"%1\" --explorer-command-server %2")
                                                 .arg(m_executable)
                                                 .arg(i)));
            server.values.insert(QStringLiteral("ServerExecutable"), stringValue(m_executable));
        } else {
            com.children.remove(QStringLiteral("LocalServer32"));
            auto& server = com.children[QStringLiteral("InprocServer32")];
            server.exists = true;
            server.values.insert(
                QString(), stringValue(QDir::toNativeSeparators(
                               QFileInfo(m_executable)
                                   .dir()
                                   .filePath(QStringLiteral("SnowShotExplorerCommand.dll")))));
            server.values.insert(QStringLiteral("ThreadingModel"),
                                 stringValue(QStringLiteral("Apartment")));
        }
        desired.insert(classKey, com);
        for (const auto& extension : m_extensions) {
            if (app::pinDocumentFileFormat(extension.mid(1)) &&
                (prefs.mode == ShellContextMenuMode::ImageFiles ||
                 !acceptsDocuments(static_cast<ShellAction>(i))))
                continue;
            const auto path = actionPath(i, extension);
            auto verb = enabledKey(path);
            verb.values.insert(QStringLiteral("NeverDefault"), stringValue({}));
            verb.values.insert(QStringLiteral("Icon"),
                               stringValue(QStringLiteral("\"%1\",0").arg(m_executable)));
            verb.values.insert(QStringLiteral("MultiSelectModel"),
                               stringValue(QStringLiteral("Player")));
            if (m_useLocalCommandServer) {
                verb.values.remove(QStringLiteral("ExplorerCommandHandler"));
                auto& command = verb.children[QStringLiteral("command")];
                command.exists = true;
                command.values.insert(QStringLiteral("DelegateExecute"), stringValue(clsid));
            } else {
                auto command = verb.children.find(QStringLiteral("command"));
                if (command != verb.children.end()) {
                    command->values.remove(QStringLiteral("DelegateExecute"));
                    if (command->values.isEmpty() && command->children.isEmpty())
                        verb.children.erase(command);
                }
                verb.values.insert(QStringLiteral("ExplorerCommandHandler"), stringValue(clsid));
            }
            desired.insert(path, std::move(verb));
        }
    }
    bool changed = false;
    for (auto item = previous.keys.cbegin(); item != previous.keys.cend(); ++item) {
        const auto requested = desired.value(item.key());
        if (item->exists && !ownedByProduct(item.value())) {
            if (!requested.exists)
                continue;
            if (error)
                *error = contextMenuText(QT_TRANSLATE_NOOP(
                    "ShellContextMenuRegistration",
                    "The Windows context menu entry is owned by another application"));
            return false;
        }
        if (requested == item.value())
            continue;
        if (!m_registry.replace(item.key(), requested, error))
            return false;
        changed = true;
    }
    if (changed && m_registry.notifyChanged)
        m_registry.notifyChanged();
    return true;
}

ShellContextMenuResult
ShellContextMenuRegistration::refreshLabels(const ShellMenuPreferences& preferences) const {
    if (app::distribution::isStoreManaged())
        return {true, {}};
    const auto previous = snapshot();
    if (!previous.valid)
        return {false, previous.error};
    bool changed = false;
    for (size_t i = 0; i < shellActionIds.size(); ++i) {
        if (preferences.actions[i] == '-')
            continue;
        QStringList paths{classPath(i)};
        for (const auto& extension : m_extensions)
            paths.append(actionPath(i, extension));
        for (const auto& path : paths) {
            auto key = previous.keys.value(path);
            if (!ownedByProduct(key) ||
                readString(key.values.value(QStringLiteral("InstallationExecutable")))
                        .compare(m_executable, Qt::CaseInsensitive) != 0)
                continue;
            const auto label = stringValue(menuLabel(i, preferences.actions[i]));
            if (key.values.value({}) == label)
                continue;
            key.values.insert({}, label);
            QString error;
            if (!m_registry.replace(path, key, &error)) {
                QString recovery;
                if (!restore(previous, &recovery))
                    error += u' ' + recovery;
                return {false, error};
            }
            changed = true;
        }
    }
    if (changed && m_registry.notifyChanged)
        m_registry.notifyChanged();
    return {true, {}};
}

bool ShellContextMenuRegistration::restore(const ShellContextMenuRegistrationSnapshot& previous,
                                           QString* error) const {
    if (app::distribution::isStoreManaged())
        return true;
    if (!previous.valid || !m_registry.read || !m_registry.replace) {
        if (error)
            *error = contextMenuText(
                QT_TRANSLATE_NOOP("ShellContextMenuRegistration",
                                  "Could not read the Windows context menu registration"));
        return false;
    }
    bool success = true;
    bool changed = false;
    for (auto item = previous.keys.cbegin(); item != previous.keys.cend(); ++item) {
        ShellContextMenuRegistryKey current;
        QString itemError;
        if (!m_registry.read(item.key(), &current, &itemError) ||
            (current != item.value() &&
             !m_registry.replace(item.key(), item.value(), &itemError))) {
            success = false;
            if (error)
                *error = itemError;
        } else if (current != item.value()) {
            changed = true;
        }
    }
    if (changed && m_registry.notifyChanged)
        m_registry.notifyChanged();
    return success;
}

ShellContextMenuResult ShellContextMenuRegistration::changePackagedMode(
    const ShellMenuPreferences& prefs, const std::function<bool()>& commitPreference) const {
    const auto mode = prefs.mode;
    const auto failure = [] {
        return ShellContextMenuResult{
            false, contextMenuText(
                       QT_TRANSLATE_NOOP("ShellContextMenuRegistration",
                                         "Could not save the Windows context menu preference"))};
    };
    if (m_packagedPreferencePath.isEmpty())
        return failure();
    QFile previous(m_packagedPreferencePath);
    const bool existed = previous.exists();
    QByteArray previousBytes;
    if (existed) {
        if (!previous.open(QIODevice::ReadOnly))
            return failure();
        previousBytes = previous.readAll();
        if (previous.error() != QFileDevice::NoError)
            return failure();
        previous.close();
    }
    const auto write = [&](const QByteArray& bytes) {
        if (!QDir().mkpath(QFileInfo(m_packagedPreferencePath).absolutePath()))
            return false;
        QSaveFile file(m_packagedPreferencePath);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
               file.commit();
    };
    const QByteArray modeByte = mode == ShellContextMenuMode::Disabled     ? QByteArray("0")
                                : mode == ShellContextMenuMode::ImageFiles ? QByteArray("2")
                                                                           : QByteArray("1");
    const auto bytes =
        modeByte + QByteArray(prefs.actions.data(), static_cast<qsizetype>(prefs.actions.size()));
    if (previousBytes == bytes && existed) {
        if (commitPreference && !commitPreference())
            return failure();
        return {true, {}};
    }
    if (!write(bytes))
        return failure();
    if (commitPreference && !commitPreference()) {
        auto result = failure();
        if (!(existed ? write(previousBytes) : QFile::remove(m_packagedPreferencePath)))
            result.error +=
                QLatin1Char(' ') +
                contextMenuText(QT_TRANSLATE_NOOP(
                                    "ShellContextMenuRegistration",
                                    "Could not restore the Windows context menu registration. %1"))
                    .arg(m_packagedPreferencePath);
        return result;
    }
    if (m_registry.notifyChanged)
        m_registry.notifyChanged();
    return {true, {}};
}

ShellContextMenuResult
ShellContextMenuRegistration::change(const ShellMenuPreferences& prefs,
                                     const std::function<bool()>& commitPreference) const {
    if (app::distribution::isStoreManaged())
        return changePackagedMode(prefs, commitPreference);
    const auto previous = snapshot();
    if (!previous.valid)
        return {false, previous.error};
    QString error;
    if (apply(prefs, previous, &error)) {
        if (!commitPreference || commitPreference())
            return {true, {}};
        error = contextMenuText(QT_TRANSLATE_NOOP(
            "ShellContextMenuRegistration", "Could not save the Windows context menu preference"));
    }
    QString recoveryError;
    if (!restore(previous, &recoveryError))
        error += QLatin1Char(' ') +
                 contextMenuText(QT_TRANSLATE_NOOP(
                                     "ShellContextMenuRegistration",
                                     "Could not restore the Windows context menu registration. %1"))
                     .arg(recoveryError);
    return {false, error};
}

} // namespace snow_shot::platform::windows
