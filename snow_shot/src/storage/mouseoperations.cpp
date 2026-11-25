// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/mouseoperations.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace snow_shot::mouse {
namespace {
constexpr Definition actions[] = {
    {Action::ShotCopy,
     Category::Screenshot,
     "copy",
     QT_TRANSLATE_NOOP("MouseOperations", "Copy to clipboard"),
     Kind::Click,
     {Kind::DoubleClick, Qt::LeftButton, Qt::NoModifier}},
    {Action::ShotSave,
     Category::Screenshot,
     "save",
     QT_TRANSLATE_NOOP("MouseOperations", "Save as file"),
     Kind::Click,
     {}},
    {Action::ShotQuickSave,
     Category::Screenshot,
     "quick_save",
     QT_TRANSLATE_NOOP("MouseOperations", "Quick save"),
     Kind::Click,
     {}},
    {Action::ShotPin,
     Category::Screenshot,
     "pin",
     QT_TRANSLATE_NOOP("MouseOperations", "Pin to screen"),
     Kind::Click,
     {Kind::Click, Qt::MiddleButton, Qt::NoModifier}},
    {Action::ShotOcr,
     Category::Screenshot,
     "ocr",
     QT_TRANSLATE_NOOP("MouseOperations", "Text recognition"),
     Kind::Click,
     {}},
    {Action::ShotOcrCopy,
     Category::Screenshot,
     "ocr_copy",
     QT_TRANSLATE_NOOP("MouseOperations", "Copy after text recognition"),
     Kind::Click,
     {}},
    {Action::ShotCancel,
     Category::Screenshot,
     "cancel",
     QT_TRANSLATE_NOOP("MouseOperations", "Cancel screenshot"),
     Kind::Click,
     {}},
    {Action::Back,
     Category::Screenshot,
     "back",
     QT_TRANSLATE_NOOP("MouseOperations", "Go back one step"),
     Kind::Click,
     {Kind::Click, Qt::RightButton, Qt::NoModifier}},
    {Action::Zoom,
     Category::Pinned,
     "zoom",
     QT_TRANSLATE_NOOP("MouseOperations", "Zoom"),
     Kind::Wheel,
     {Kind::Wheel, Qt::NoButton, Qt::NoModifier}},
    {Action::FineZoom,
     Category::Pinned,
     "fine_zoom",
     QT_TRANSLATE_NOOP("MouseOperations", "Fine zoom"),
     Kind::Wheel,
     {Kind::Wheel, Qt::NoButton, Qt::AltModifier}},
    {Action::Opacity,
     Category::Pinned,
     "opacity",
     QT_TRANSLATE_NOOP("MouseOperations", "Adjust opacity"),
     Kind::Wheel,
     {Kind::Wheel, Qt::NoButton, Qt::ControlModifier}},
    {Action::FineOpacity,
     Category::Pinned,
     "fine_opacity",
     QT_TRANSLATE_NOOP("MouseOperations", "Fine opacity adjustment"),
     Kind::Wheel,
     {}},
    {Action::Copy,
     Category::Pinned,
     "copy",
     QT_TRANSLATE_NOOP("MouseOperations", "Copy to clipboard"),
     Kind::Click,
     {}},
    {Action::CopyClose,
     Category::Pinned,
     "copy_and_close",
     QT_TRANSLATE_NOOP("MouseOperations", "Copy to clipboard and close"),
     Kind::Click,
     {}},
    {Action::CopyText,
     Category::Pinned,
     "copy_text",
     QT_TRANSLATE_NOOP("MouseOperations", "Copy text to clipboard"),
     Kind::Click,
     {Kind::Click, Qt::RightButton, Qt::ShiftModifier}},
    {Action::Save,
     Category::Pinned,
     "save",
     QT_TRANSLATE_NOOP("MouseOperations", "Save as file"),
     Kind::Click,
     {}},
    {Action::ResetZoom,
     Category::Pinned,
     "reset_zoom",
     QT_TRANSLATE_NOOP("MouseOperations", "Reset zoom"),
     Kind::Click,
     {Kind::Click, Qt::MiddleButton, Qt::NoModifier}},
    {Action::Thumbnail,
     Category::Pinned,
     "thumbnail_mode",
     QT_TRANSLATE_NOOP("MouseOperations", "Thumbnail mode"),
     Kind::Click,
     {Kind::DoubleClick, Qt::LeftButton, Qt::NoModifier}},
    {Action::HideAtTop,
     Category::Pinned,
     "hide_to_top",
     QT_TRANSLATE_NOOP("MouseOperations", "Hide at top"),
     Kind::Click,
     {}},
    {Action::Recognition,
     Category::Pinned,
     "show_text_recognition_results",
     QT_TRANSLATE_NOOP("MouseOperations", "Show text recognition results"),
     Kind::Click,
     {}},
    {Action::Close,
     Category::Pinned,
     "close",
     QT_TRANSLATE_NOOP("MouseOperations", "Close"),
     Kind::Click,
     {}},
    {Action::Destroy,
     Category::Pinned,
     "destroy",
     QT_TRANSLATE_NOOP("MouseOperations", "Destroy"),
     Kind::Click,
     {}},
    {Action::Menu,
     Category::Pinned,
     "menu",
     QT_TRANSLATE_NOOP("MouseOperations", "Menu"),
     Kind::Click,
     {Kind::Click, Qt::RightButton, Qt::NoModifier}},
    {Action::Annotation,
     Category::Pinned,
     "annotation_mode",
     QT_TRANSLATE_NOOP("MouseOperations", "Annotation mode"),
     Kind::Click,
     {}},
    {Action::ClickThrough,
     Category::Pinned,
     "toggle_click_through",
     QT_TRANSLATE_NOOP("MouseOperations", "Click-through"),
     Kind::Click,
     {}},
    {Action::Lock,
     Category::Pinned,
     "toggle_lock",
     QT_TRANSLATE_NOOP("MouseOperations", "Lock mode"),
     Kind::Click,
     {}},
    {Action::ShowAllWindows,
     Category::Pinned,
     "show_all_windows",
     QT_TRANSLATE_NOOP("MouseOperations", "Show all windows"),
     Kind::Click,
     {}},
    {Action::HideOtherWindows,
     Category::Pinned,
     "hide_other_windows",
     QT_TRANSLATE_NOOP("MouseOperations", "Hide other windows"),
     Kind::Click,
     {}},
    {Action::CloseOtherWindows,
     Category::Pinned,
     "close_other_windows",
     QT_TRANSLATE_NOOP("MouseOperations", "Close other windows"),
     Kind::Click,
     {}},
    {Action::CloseAllWindows,
     Category::Pinned,
     "close_all_windows",
     QT_TRANSLATE_NOOP("MouseOperations", "Close all windows"),
     Kind::Click,
     {}},
    {Action::MultiSelect,
     Category::Pinned,
     "multi_select",
     QT_TRANSLATE_NOOP("MouseOperations", "Multi-select"),
     Kind::Click,
     {Kind::Click, Qt::LeftButton, Qt::ControlModifier}},
    {Action::Crop,
     Category::Pinned,
     "select_crop",
     QT_TRANSLATE_NOOP("MouseOperations", "Select crop region"),
     Kind::Drag,
     {Kind::Drag, Qt::RightButton, Qt::NoModifier}},
    {Action::PanCrop,
     Category::Pinned,
     "pan_crop",
     QT_TRANSLATE_NOOP("MouseOperations", "Pan within crop"),
     Kind::Drag,
     {Kind::Drag, Qt::LeftButton, Qt::ShiftModifier}},
    {Action::Move,
     Category::Pinned,
     "move_resize",
     QT_TRANSLATE_NOOP("MouseOperations", "Move or resize window"),
     Kind::Drag,
     {Kind::Drag, Qt::LeftButton, Qt::NoModifier}},
    {Action::ForceMove,
     Category::Pinned,
     "force_move",
     QT_TRANSLATE_NOOP("MouseOperations", "Force window move"),
     Kind::Drag,
     {Kind::Drag, Qt::LeftButton, Qt::AltModifier}},
    {Action::Export,
     Category::Pinned,
     "drag_image",
     QT_TRANSLATE_NOOP("MouseOperations", "Drag image"),
     Kind::Drag,
     {Kind::Drag, Qt::LeftButton, Qt::ControlModifier}},
};
static_assert(std::size(actions) == static_cast<size_t>(Action::Count));
struct Operation {
    Kind kind;
    Qt::MouseButton button;
    const char* id;
    const char* label;
};
constexpr Operation operations[] = {
    {Kind::None, Qt::NoButton, "none", QT_TRANSLATE_NOOP("MouseOperations", "None")},
    {Kind::Wheel, Qt::NoButton, "wheel", QT_TRANSLATE_NOOP("MouseOperations", "Mouse wheel")},
    {Kind::Click, Qt::LeftButton, "left_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Left-button single-click")},
    {Kind::DoubleClick, Qt::LeftButton, "left_double_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Left-button double-click")},
    {Kind::Drag, Qt::LeftButton, "left_drag",
     QT_TRANSLATE_NOOP("MouseOperations", "Left-button drag")},
    {Kind::Click, Qt::RightButton, "right_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Right-button single-click")},
    {Kind::DoubleClick, Qt::RightButton, "right_double_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Right-button double-click")},
    {Kind::Drag, Qt::RightButton, "right_drag",
     QT_TRANSLATE_NOOP("MouseOperations", "Right-button drag")},
    {Kind::Click, Qt::MiddleButton, "middle_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Middle-button single-click")},
    {Kind::DoubleClick, Qt::MiddleButton, "middle_double_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Middle-button double-click")},
    {Kind::Drag, Qt::MiddleButton, "middle_drag",
     QT_TRANSLATE_NOOP("MouseOperations", "Middle-button drag")},
    {Kind::Click, Qt::BackButton, "back_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 1 (back) single-click")},
    {Kind::DoubleClick, Qt::BackButton, "back_double_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 1 (back) double-click")},
    {Kind::Drag, Qt::BackButton, "back_drag",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 1 (back) drag")},
    {Kind::Click, Qt::ForwardButton, "forward_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 2 (forward) single-click")},
    {Kind::DoubleClick, Qt::ForwardButton, "forward_double_click",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 2 (forward) double-click")},
    {Kind::Drag, Qt::ForwardButton, "forward_drag",
     QT_TRANSLATE_NOOP("MouseOperations", "Side button 2 (forward) drag")},
};
struct Modifier {
    Qt::KeyboardModifier flag;
    const char* id;
    const char* label;
};
constexpr Modifier keys[] = {
#ifdef Q_OS_MACOS
    {Qt::ControlModifier, "command", QT_TRANSLATE_NOOP("MouseOperations", "Command")},
    {Qt::MetaModifier, "control", QT_TRANSLATE_NOOP("MouseOperations", "Control")},
    {Qt::AltModifier, "option", QT_TRANSLATE_NOOP("MouseOperations", "Option")},
#else
    {Qt::MetaModifier, "windows", QT_TRANSLATE_NOOP("MouseOperations", "Windows")},
    {Qt::ControlModifier, "ctrl", QT_TRANSLATE_NOOP("MouseOperations", "Ctrl")},
    {Qt::AltModifier, "alt", QT_TRANSLATE_NOOP("MouseOperations", "Alt")},
#endif
    {Qt::ShiftModifier, "shift", QT_TRANSLATE_NOOP("MouseOperations", "Shift")},
};
QString translated(const char* source) {
    return QCoreApplication::translate("MouseOperations", source);
}
const Operation& op(Binding b) {
    for (const auto& item : operations)
        if (b.kind == item.kind && b.button == item.button)
            return item;
    return operations[0];
}
Kind family(Kind kind) {
    return kind == Kind::DoubleClick ? Kind::Click : kind;
}
} // namespace
std::span<const Definition> definitions() {
    return actions;
}
const Definition& definition(Action action) {
    return actions[static_cast<size_t>(action)];
}
QString Definition::key() const {
    if (action == Action::Back)
        return {};
    return (category == Category::Screenshot ? QStringLiteral("screenshot_mouse/")
                                             : QStringLiteral("pin_to_screen_mouse/")) +
           QLatin1StringView(id);
}
bool isKey(QStringView key) {
    return key.startsWith(u"screenshot_mouse/") || key.startsWith(u"pin_to_screen_mouse/");
}
const Definition* forKey(const QString& key) {
    if (!isKey(key))
        return nullptr;
    for (const auto& action : actions)
        if (action.key() == key)
            return &action;
    return nullptr;
}
Qt::KeyboardModifiers modifiers(Qt::KeyboardModifiers mods) {
    return mods & (Qt::MetaModifier | Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
}
QString title(Action action) {
    return translated(definition(action).title);
}
QString operationId(Binding binding) {
    return QString::fromLatin1(op(binding).id);
}
QString operationLabel(Binding binding) {
    return translated(op(binding).label);
}
QString modifierLabel(Qt::KeyboardModifier modifier) {
    for (const auto& key : keys)
        if (key.flag == modifier)
            return translated(key.label);
    return {};
}
QString modifierLabel(const QString& id) {
    for (const auto& key : keys)
        if (id == QLatin1StringView(key.id))
            return translated(key.label);
    return id;
}
QString bindingLabel(Binding binding) {
    if (binding.kind == Kind::None)
        return QCoreApplication::translate("MouseOperations", "Unset");
    QStringList parts;
    for (const auto& key : keys)
        if (binding.modifiers.testFlag(key.flag))
            parts.append(translated(key.label));
    parts.append(operationLabel(binding));
    return parts.join(u'+');
}
QList<Binding> choices(Kind kind) {
    QList<Binding> result;
    for (const auto& item : operations)
        if (item.kind == Kind::None || family(item.kind) == kind)
            result.append(Binding{item.kind, item.button});
    return result;
}
QJsonObject encode(Binding binding) {
    if (binding == Binding{})
        return {};
    QJsonArray mods;
    for (const auto& key : keys)
        if (binding.modifiers.testFlag(key.flag))
            mods.append(QString::fromLatin1(key.id));
    return {{QStringLiteral("operation"), operationId(binding)},
            {QStringLiteral("modifiers"), mods}};
}
std::optional<Binding> decode(const QJsonValue& raw) {
    if (!raw.isObject())
        return {};
    const auto obj = raw.toObject();
    if (obj.isEmpty())
        return Binding{};
    const auto mods = obj.value(QStringLiteral("modifiers"));
    if (obj.size() != 2 || !mods.isArray())
        return {};
    Qt::KeyboardModifiers flags;
    for (const auto& entry : mods.toArray()) {
        const auto token = entry.toString();
        auto found = std::find_if(std::begin(keys), std::end(keys),
                                  [&](const auto& k) { return token == QLatin1StringView(k.id); });
        if (found == std::end(keys) || flags.testFlag(found->flag))
            return {};
        flags |= found->flag;
    }
    const auto id = obj.value(QStringLiteral("operation")).toString();
    for (const auto& item : operations)
        if (id == QLatin1StringView(item.id))
            return Binding{item.kind, item.button, flags};
    return {};
}
bool allowed(const Definition& action, Binding b) {
    if (action.action == Action::Back)
        return b == action.factory;
    if (modifiers(b.modifiers) != b.modifiers)
        return false;
    if (b.kind == Kind::None)
        return b.button == Qt::NoButton;
    if (family(b.kind) != action.kind || op(b).kind != b.kind)
        return false;
    if (b.modifiers == Qt::NoModifier && family(b.kind) == Kind::Click) {
        if (b.kind == Kind::Click && b.button == Qt::LeftButton)
            return false;
        if (action.category == Category::Screenshot && b.button == Qt::RightButton)
            return false;
    }
    return true;
}
bool conflict(Binding a, Binding b) {
    return a.kind != Kind::None && family(a.kind) == family(b.kind) && a.button == b.button &&
           a.modifiers == b.modifiers;
}
Bindings::Bindings(const Values& values) {
    for (const auto& a : actions)
        m_items[static_cast<size_t>(a.action)] = decode(values.value(a.key())).value_or(a.factory);
}
Binding Bindings::get(Action action) const {
    return m_items[static_cast<size_t>(action)];
}
std::optional<Action> Bindings::match(Category category, Binding binding) const {
    if (binding.kind != Kind::None)
        for (const auto& a : actions)
            if (a.category == category && get(a.action) == binding)
                return a.action;
    return {};
}
std::optional<Action> Bindings::owner(Action except, Binding binding) const {
    for (const auto& a : actions)
        if (a.action != except && a.category == definition(except).category &&
            conflict(get(a.action), binding))
            return a.action;
    return {};
}
Values factoryValues(Category category) {
    Values result;
    for (const auto& a : actions)
        if (a.category == category && a.action != Action::Back)
            result.insert(a.key(), encode(a.factory));
    return result;
}
void migrate(Values& values) {
    for (const auto category : {Category::Screenshot, Category::Pinned}) {
        const bool shot = category == Category::Screenshot;
        const QString prefix =
            shot ? QStringLiteral("screenshot/") : QStringLiteral("pin_to_screen/");
        const auto doubleKey = prefix + QStringLiteral("double_click_action");
        const auto middleKey = prefix + QStringLiteral("middle_mouse_button_action");
        const auto defaults = factoryValues(category);
        if (std::any_of(defaults.keyBegin(), defaults.keyEnd(),
                        [&](const auto& k) { return values.contains(k); }) ||
            (!values.contains(doubleKey) && !values.contains(middleKey)))
            continue;
        const auto effective = [&](const QString& key, Action fallback) {
            const auto id = values.value(key).toString();
            if (id == u"none")
                return QString();
            for (const auto& a : actions)
                if (a.category == category && a.action != Action::Back && a.kind == Kind::Click &&
                    id == QLatin1StringView(a.id))
                    return a.key();
            return definition(fallback).key();
        };
        const auto first = shot ? Action::ShotCopy : Action::Thumbnail;
        const auto middle = shot ? Action::ShotPin : Action::ResetZoom;
        const auto leftTarget = effective(doubleKey, first);
        const auto middleTarget = effective(middleKey, middle);
        values.insert(defaults);
        values[definition(first).key()] = QJsonObject{};
        values[definition(middle).key()] = QJsonObject{};
        if (!leftTarget.isEmpty())
            values[leftTarget] = encode(definition(first).factory);
        if (!middleTarget.isEmpty() && middleTarget != leftTarget)
            values[middleTarget] = encode(definition(middle).factory);
    }
}
void reconcile(Values& values) {
    // Explicit bindings get first claim. A missing default can only claim an unused gesture.
    for (const auto category : {Category::Screenshot, Category::Pinned}) {
        QList<Binding> assigned;
        QSet<QString> explicitKeys;
        for (const auto& a : actions) {
            if (a.category != category || a.action == Action::Back)
                continue;
            const auto b = decode(values.value(a.key()));
            if (b && allowed(a, *b))
                explicitKeys.insert(a.key());
        }
        for (bool explicitPass : {true, false})
            for (const auto& a : actions) {
                if (a.category != category || a.action == Action::Back ||
                    explicitKeys.contains(a.key()) != explicitPass)
                    continue;
                Binding b = explicitPass ? *decode(values.value(a.key())) : a.factory;
                if (std::any_of(assigned.cbegin(), assigned.cend(),
                                [&](Binding other) { return conflict(other, b); }))
                    b = {};
                if (b.kind != Kind::None)
                    assigned.append(b);
                values.insert(a.key(), encode(b));
            }
    }
}
bool changesValid(const Values& current, const Values& updates) {
    if (std::none_of(updates.keyBegin(), updates.keyEnd(),
                     [](const QString& k) { return isKey(k); }))
        return true;
    auto merged = current;
    merged.insert(updates);
    const Bindings bindings(merged);
    for (const auto& a : actions) {
        if (!updates.contains(a.key()))
            continue;
        const auto binding = decode(updates.value(a.key()));
        if (!binding || !allowed(a, *binding) || bindings.owner(a.action, *binding))
            return false;
    }
    return true;
}
} // namespace snow_shot::mouse
