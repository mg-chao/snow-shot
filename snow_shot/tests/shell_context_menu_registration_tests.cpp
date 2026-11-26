#include "shell_context_menu_test_registry.h"
#include "snow_shot/app/supportedpinfiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QUuid>

#include <cstdlib>
#include <iostream>

namespace {
using namespace shell_context_menu_tests;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void actionsAndRollback() {
    Registry registry;
    auto options = registry.options();
    options.useLocalCommandServer = false;
    options.fileExtensions.append(QStringLiteral("md"));
    windows::ShellContextMenuRegistration registration(options);
    windows::ShellMenuPreferences prefs;
    windows::ShellContextMenuRegistryKey legacy;
    legacy.exists = true;
    legacy.values.insert(QStringLiteral("ProductId"),
                         stringValue(snow_shot::app::edition::productId()));
    registry.keys.insert(Registry::path(QStringLiteral("obsolete")), legacy);
    const auto fileVerb = [](const QString& extension, const QString& action) {
        return Registry::path(extension, snow_shot::app::edition::registryName() +
                                             QStringLiteral(".File.") + action);
    };
    registry.keys.insert(fileVerb(QStringLiteral("obsolete"), QStringLiteral("pin_to_screen")),
                         legacy);
    const auto oldWildcard = QStringLiteral("software\\snowshottests\\*\\shell\\") +
                             snow_shot::app::edition::registryName().toLower() +
                             QStringLiteral(".pin_to_screen");
    registry.keys.insert(oldWildcard, legacy);
    prefs.actions = {'P', 'A', '.', '-', 'E', '-'};
    require(registration.change(prefs).success && registry.keys.size() == 14 &&
                !registry.keys.contains(oldWildcard),
            "four actions register on both images, two on documents, and stale wildcard verbs are "
            "removed");
    const auto broad = registry.keys;
    const auto writes = registry.writes;
    require(registration.change(prefs).success && registry.writes == writes,
            "startup reconciliation is idempotent");
    const auto pngPin = fileVerb(QStringLiteral("png"), QStringLiteral("pin_to_screen"));
    const auto mdPin = fileVerb(QStringLiteral("md"), QStringLiteral("pin_to_screen"));
    const auto verb = registry.keys.value(pngPin);
    require(stringText(verb.values.value({})) == QStringLiteral("Pin to screen (&P)") &&
                stringText(verb.values.value(QStringLiteral("MultiSelectModel"))) ==
                    QStringLiteral("Player"),
            "pin uses the assigned menu key and receives whole Explorer selections");
    require(registry.keys.value(mdPin) == verb &&
                !registry.keys.contains(
                    fileVerb(QStringLiteral("md"), QStringLiteral("edit_images"))) &&
                !registry.keys.contains(
                    fileVerb(QStringLiteral("obsolete"), QStringLiteral("pin_to_screen"))),
            "per-extension verbs use the same command and title, and only pinning is registered on "
            "documents");
    const auto clsid = stringText(verb.values.value(QStringLiteral("ExplorerCommandHandler")));
    const auto classPath = QStringLiteral("software\\snowshottests\\clsid\\") + clsid.toLower();
    require(registry.keys.value(classPath).exists &&
                stringText(registry.keys.value(classPath).values.value(QStringLiteral("Scope"))) ==
                    QStringLiteral("image_files"),
            "the verb resolves to its class and broad scope");
    prefs.mode = windows::ShellContextMenuMode::ImageFiles;
    registry.writesBeforeFailure = 1;
    require(!registration.change(prefs).success && registry.keys == broad,
            "partial native writes roll back");
    require(!registration.change(prefs, [] { return false; }).success && registry.keys == broad,
            "a failed preference commit restores every class and verb");
    require(registration.change(prefs).success && registry.keys.size() == 12 &&
                !registry.keys.contains(mdPin) && registry.keys.contains(pngPin) &&
                stringText(registry.keys.value(classPath).values.value(QStringLiteral("Scope"))) ==
                    QStringLiteral("images_only"),
            "scope updates reach Explorer");
    prefs.actions[0] = 'Q';
    require(registration.refreshLabels(prefs).success &&
                stringText(registry.keys.value(pngPin).values.value({})) ==
                    QStringLiteral("Pin to screen (&Q)") &&
                registry.keys.value(pngPin).values.value({}) ==
                    registry.keys.value(classPath).values.value({}),
            "label refresh updates the extension verb and its command class");
    auto foreign = verb;
    foreign.values.insert(QStringLiteral("ProductId"), stringValue(QStringLiteral("foreign")));
    registry.keys.insert(pngPin, foreign);
    require(!registration.change(prefs).success && registry.keys.value(pngPin) == foreign,
            "foreign ownership prevents overwriting");
    prefs.mode = windows::ShellContextMenuMode::Disabled;
    require(registration.change(prefs).success && registry.keys.size() == 1 &&
                registry.keys.value(pngPin) == foreign,
            "disable removes only owned registrations");
}

void nativeAndEmulatedServers() {
    Registry registry;
    auto options = registry.options();
    options.useLocalCommandServer = true;
    windows::ShellContextMenuRegistration emulated(options);
    const windows::ShellMenuPreferences prefs;
    require(emulated.change(prefs).success, "register an ARM64 host for the x64 installation");
    const auto verbPath =
        Registry::path(QStringLiteral("png"), snow_shot::app::edition::registryName() +
                                                  QStringLiteral(".File.pin_to_screen"));
    auto& verb = registry.keys[verbPath];
    const auto clsid = stringText(verb.children.value(QStringLiteral("command"))
                                      .values.value(QStringLiteral("DelegateExecute")));
    const auto classPath = QStringLiteral("software\\snowshottests\\clsid\\") + clsid.toLower();
    const auto server =
        registry.keys.value(classPath).children.value(QStringLiteral("LocalServer32"));
    const auto executable = QDir::toNativeSeparators(options.executablePath);
    require(
        !clsid.isEmpty() && !verb.values.contains(QStringLiteral("ExplorerCommandHandler")) &&
            stringText(verb.values.value(QStringLiteral("MultiSelectModel"))) ==
                QStringLiteral("Player") &&
            !registry.keys.value(classPath).children.contains(QStringLiteral("InprocServer32")) &&
            stringText(server.values.value(QStringLiteral("ServerExecutable"))) == executable &&
            stringText(server.values.value({})) ==
                QStringLiteral("\"%1\" --explorer-command-server 0").arg(executable),
        "ARM64 Explorer activates the x64 verb out of process with one multi-selection");
    verb.values.insert(QStringLiteral("Extended"), stringValue({}));
    auto& custom = verb.children[QStringLiteral("custom")];
    custom.exists = true;
    custom.values.insert(QStringLiteral("Opaque"), {3, QByteArray("\0\1\2", 3)});
    const auto originalCustom = custom;
    registry.keys[classPath].values.insert(QStringLiteral("OtherValue"),
                                           stringValue(QStringLiteral("keep")));
    require(emulated.change(prefs).success &&
                registry.keys.value(verbPath).children.value(QStringLiteral("custom")) ==
                    originalCustom &&
                registry.keys.value(verbPath).values.contains(QStringLiteral("Extended")) &&
                registry.keys.value(classPath).values.contains(QStringLiteral("OtherValue")),
            "startup reconciliation preserves custom registry values and subkeys");
    options.useLocalCommandServer = false;
    windows::ShellContextMenuRegistration native(options);
    require(
        native.change(prefs).success &&
            registry.keys.value(classPath).children.contains(QStringLiteral("InprocServer32")) &&
            !registry.keys.value(classPath).children.contains(QStringLiteral("LocalServer32")) &&
            stringText(registry.keys.value(verbPath).values.value(
                QStringLiteral("ExplorerCommandHandler"))) == clsid &&
            !registry.keys.value(verbPath).children.contains(QStringLiteral("command")) &&
            registry.keys.value(verbPath).children.value(QStringLiteral("custom")) ==
                originalCustom,
        "installing the native edition removes the obsolete delegate and retains custom data");
    require(
        emulated.change(prefs).success &&
            !registry.keys.value(classPath).children.contains(QStringLiteral("InprocServer32")) &&
            !registry.keys.value(verbPath).values.contains(
                QStringLiteral("ExplorerCommandHandler")),
        "reverting to emulation removes the old in-process activation path");
}

#ifdef Q_OS_WIN
void isolatedNativeRegistry() {
    const QString root = QStringLiteral("Software\\SnowShotTests\\") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSettings neighbors(QStringLiteral("HKEY_CURRENT_USER\\") + root, QSettings::NativeFormat);
    neighbors.setValue(QStringLiteral("SystemFileAssociations/.png/shell/ParentValue"), 123);
    neighbors.setValue(QStringLiteral("SystemFileAssociations/.png/shell/Other.Edit/Custom"),
                       QStringLiteral("keep neighbor"));
    neighbors.sync();
    require(neighbors.status() == QSettings::NoError, "create isolated native registry fixture");
    windows::ShellContextMenuRegistration::Options options;
    options.classesRoot = root;
    options.executablePath =
        QStringLiteral("C:/Native Snow Shot/雪/") + snow_shot::app::edition::executableName();
    options.fileExtensions = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp")};
    windows::ShellContextMenuRegistration registration(std::move(options));
    windows::ShellMenuPreferences prefs;
    prefs.actions[0] = 'E';
    require(registration.change(prefs).success, "enable an isolated native verb");
    const auto before = registration.snapshot();
    require(before.valid && std::count_if(before.keys.cbegin(), before.keys.cend(),
                                          [](const auto& key) { return key.exists; }) == 4,
            "only three extension verbs and their COM server are registered");
    prefs.mode = windows::ShellContextMenuMode::Disabled;
    require(!registration.change(prefs, [] { return false; }).success &&
                registration.snapshot().keys == before.keys,
            "a failed commit restores native keys");
    require(registration.change(prefs).success, "disable the isolated native verb");
    const auto disabled = registration.snapshot();
    require(disabled.valid && std::all_of(disabled.keys.cbegin(), disabled.keys.cend(),
                                          [](const auto& key) { return !key.exists; }),
            "native removal deletes BMP with the other owned image verbs");
    neighbors.sync();
    require(neighbors.value(QStringLiteral("SystemFileAssociations/.png/shell/ParentValue")) ==
                    123 &&
                neighbors.value(
                    QStringLiteral("SystemFileAssociations/.png/shell/Other.Edit/Custom")) ==
                    QStringLiteral("keep neighbor"),
            "native removal preserves adjacent registry values and unrelated verbs");
    neighbors.remove(QString());
    neighbors.sync();
    require(neighbors.status() == QSettings::NoError,
            "remove only the isolated test registry root");
}
#endif
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    actionsAndRollback();
    nativeAndEmulatedServers();
#ifdef Q_OS_WIN
    isolatedNativeRegistry();
#endif
    std::cout << "Shell context menu registration tests passed\n";
    return 0;
}
