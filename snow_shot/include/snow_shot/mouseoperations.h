// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QMap>
#include <Qt>
#include <array>
#include <optional>
#include <span>

namespace snow_shot::mouse {
enum class Category { Screenshot, Pinned };
enum class Kind { None, Click, DoubleClick, Wheel, Drag };
enum class Action {
    ShotCopy,
    ShotSave,
    ShotQuickSave,
    ShotPin,
    ShotOcr,
    ShotOcrCopy,
    ShotCancel,
    Back,
    Zoom,
    FineZoom,
    Opacity,
    FineOpacity,
    Copy,
    CopyClose,
    CopyText,
    Save,
    ResetZoom,
    Thumbnail,
    HideAtTop,
    Recognition,
    Close,
    Destroy,
    Menu,
    Annotation,
    ClickThrough,
    Lock,
    ShowAllWindows,
    HideOtherWindows,
    CloseOtherWindows,
    CloseAllWindows,
    MultiSelect,
    Crop,
    PanCrop,
    Move,
    ForceMove,
    Export,
    Count
};
struct Binding {
    Kind kind = Kind::None;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers{};
    friend bool operator==(const Binding&, const Binding&) = default;
};
struct Definition {
    Action action;
    Category category;
    const char* id;
    const char* title;
    Kind kind;
    Binding factory;
    [[nodiscard]] QString key() const;
};
using Values = QMap<QString, QJsonValue>;
[[nodiscard]] std::span<const Definition> definitions();
[[nodiscard]] const Definition& definition(Action action);
[[nodiscard]] const Definition* forKey(const QString& key);
[[nodiscard]] bool isKey(QStringView key);
[[nodiscard]] QJsonObject encode(Binding binding);
[[nodiscard]] std::optional<Binding> decode(const QJsonValue& raw);
[[nodiscard]] bool allowed(const Definition& action, Binding binding);
[[nodiscard]] bool conflict(Binding first, Binding second);
[[nodiscard]] Qt::KeyboardModifiers modifiers(Qt::KeyboardModifiers keys);
[[nodiscard]] QString operationId(Binding binding);
[[nodiscard]] QString operationLabel(Binding binding);
[[nodiscard]] QString modifierLabel(Qt::KeyboardModifier modifier);
[[nodiscard]] QString modifierLabel(const QString& id);
[[nodiscard]] QString bindingLabel(Binding binding);
[[nodiscard]] QString title(Action action);
[[nodiscard]] QList<Binding> choices(Kind kind);
[[nodiscard]] Values factoryValues(Category category);
void migrate(Values& values);
void reconcile(Values& values);
[[nodiscard]] bool changesValid(const Values& current, const Values& updates);
class Bindings {
  public:
    explicit Bindings(const Values& values = {});
    [[nodiscard]] Binding get(Action action) const;
    [[nodiscard]] std::optional<Action> match(Category category, Binding binding) const;
    [[nodiscard]] std::optional<Action> owner(Action except, Binding binding) const;

  private:
    std::array<Binding, static_cast<size_t>(Action::Count)> m_items{};
};
} // namespace snow_shot::mouse
