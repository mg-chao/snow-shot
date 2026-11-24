#pragma once

#include "snow_shot/app/edition.h"
#include "snow_shot/presentation/settings/settingsruntimesession.h"
#include "snow_shot/storage/configurationschema.h"
#include <QJsonArray>
#include <cmath>
#include <optional>

namespace snow_shot::app::mcp {
namespace settings = presentation::settings;

struct AuxiliaryIntegerSetting {
    QString id;
    QString configurationKey;
    settings::SettingsIntegerBinding binding;
};

inline std::optional<AuxiliaryIntegerSetting>
auxiliaryIntegerSetting(const settings::SettingsFieldDescriptor& field) {
    const auto* shortcut =
        field.definition
            ? std::get_if<settings::SettingsShortcutActionDefinition>(&field.definition->payload)
            : nullptr;
    if (shortcut &&
        shortcut->adjustment == settings::SettingsShortcutAdjustment::ScreenshotDelaySeconds)
        return AuxiliaryIntegerSetting{field.id + QStringLiteral(".seconds"),
                                       QStringLiteral("screenshot/delay_seconds"),
                                       settings::SettingsIntegerBinding::ScreenshotDelaySeconds};
    return std::nullopt;
}

#if SNOW_SHOT_ENABLE_API_CONFIGURATION
inline QJsonArray publicModels(const CustomAiModels& models) {
    QJsonArray result;
    for (const auto& model : models) {
        auto object = customAiModelsToJson({model}).first().toObject();
        object.remove(QStringLiteral("api_key"));
        object.insert(QStringLiteral("credential_set"), !model.apiKey.isEmpty());
        result.append(object);
    }
    return result;
}
#endif

inline QJsonValue settingsJson(const QVariant& value) {
    if (value.metaType() == QMetaType::fromType<CloudUploadSettings>()) {
        auto settings = value.value<CloudUploadSettings>();
        for (auto& profile : settings.configurations) {
            profile.accessKeyId.clear();
            profile.secretAccessKey.clear();
            profile.sessionToken.clear();
        }
        return cloudUploadSettingsToJson(settings);
    }
    if (value.metaType() == QMetaType::fromType<QColor>())
        return storage::colorToRgbaString(value.value<QColor>());
    if (value.metaType() == QMetaType::fromType<QVector<QColor>>()) {
        QJsonArray colors;
        for (const auto& color : value.value<QVector<QColor>>())
            colors.append(storage::colorToRgbaString(color));
        return colors;
    }
#if SNOW_SHOT_ENABLE_API_CONFIGURATION
    if (value.metaType() == QMetaType::fromType<CustomAiModels>())
        return publicModels(value.value<CustomAiModels>());
#endif
    if (value.metaType() == QMetaType::fromType<shortcuts::ShortcutBindingList>())
        return shortcuts::shortcutBindingsToJson(value.value<shortcuts::ShortcutBindingList>());
    if (value.metaType() == QMetaType::fromType<settings::SettingsGlobalMouseCombination>()) {
        const auto chord = value.value<settings::SettingsGlobalMouseCombination>();
        return QJsonObject{
            {QStringLiteral("activation_keys"), QJsonArray::fromStringList(chord.activationKeys)},
            {QStringLiteral("mouse_button"), chord.mouseButton}};
    }
    if (value.metaType() == QMetaType::fromType<storage::ScreenshotToolbarLayout>()) {
        const auto layout = value.value<storage::ScreenshotToolbarLayout>();
        QJsonArray positions;
        for (const auto& position : layout.positions)
            positions.append(QJsonArray::fromStringList(position));
        return QJsonObject{{QStringLiteral("positions"), positions},
                           {QStringLiteral("hidden"), QJsonArray::fromStringList(layout.hidden)}};
    }
    return QJsonValue::fromVariant(value);
}

inline bool stringArray(const QJsonValue& value, QStringList* strings) {
    if (!value.isArray())
        return false;
    for (const auto& item : value.toArray()) {
        if (!item.isString())
            return false;
        strings->append(item.toString());
    }
    return true;
}

inline bool settingsValue(const settings::SettingsFieldDescriptor& field, const QJsonValue& json,
                          QVariant* value) {
    using Kind = settings::SettingsFieldKind;
    if (!field.definition || field.kind == Kind::Action)
        return false;
    storage::ConfigurationNormalization normalized;
    if (field.kind != Kind::GlobalMouseAction && !field.configurationKey.isEmpty()) {
        normalized = storage::ConfigurationSchema::normalize(field.configurationKey, json);
        if (!normalized.valid)
            return false;
    }
    switch (field.kind) {
    case Kind::Switch:
        if (!json.isBool())
            return false;
        break;
    case Kind::Integer:
    case Kind::Slider:
        if (!json.isDouble() || !std::isfinite(json.toDouble()) ||
            std::floor(json.toDouble()) != json.toDouble())
            return false;
        break;
    case Kind::Color: {
        const QColor color = storage::colorFromRgbaString(json.toString());
        if (!color.isValid())
            return false;
        *value = color;
        return true;
    }
    case Kind::ColorPalette: {
        const auto& palette =
            std::get<settings::SettingsColorPaletteDefinition>(field.definition->payload);
        if (!json.isArray() || json.toArray().size() != palette.colorCount)
            return false;
        QVector<QColor> colors;
        for (const auto& item : json.toArray()) {
            if (!item.isString())
                return false;
            const auto color = storage::colorFromRgbaString(item.toString());
            if (!color.isValid())
                return false;
            colors.append(color);
        }
        *value = QVariant::fromValue(colors);
        return true;
    }
    case Kind::ShortcutAction:
    case Kind::LocalShortcut: {
        // The storage schema can salvage a list when loading a file. An explicit edit must
        // accept every requested binding, including in an atomic multi-action swap.
        if (!json.isArray() || normalized.value.toArray().size() != json.toArray().size())
            return false;
        const auto policy = field.kind == Kind::ShortcutAction
                                ? shortcuts::ShortcutPolicy::Native
                                : shortcuts::ShortcutPolicy::Application;
        const auto bindings = shortcuts::shortcutBindingsFromJson(normalized.value, true, -1,
                                                                  nullptr, nullptr, true, policy);
        *value = QVariant::fromValue(bindings);
        return true;
    }
    case Kind::GlobalMouseAction: {
        if (!json.isObject())
            return false;
        const auto object = json.toObject();
        settings::SettingsGlobalMouseCombination chord;
        if (!stringArray(object.value(QStringLiteral("activation_keys")), &chord.activationKeys) ||
            !object.value(QStringLiteral("mouse_button")).isString())
            return false;
        chord.mouseButton = object.value(QStringLiteral("mouse_button")).toString();
        const QJsonObject persisted =
            chord.activationKeys.isEmpty() && chord.mouseButton.isEmpty()
                ? QJsonObject()
                : QJsonObject{{QStringLiteral("activation_key"),
                               QJsonArray::fromStringList(chord.activationKeys)},
                              {QStringLiteral("mouse_button"), chord.mouseButton}};
        if (!storage::ConfigurationSchema::normalize(field.configurationKey, persisted).valid)
            return false;
        *value = QVariant::fromValue(chord);
        return true;
    }
    case Kind::Custom: {
        const auto custom = std::get<settings::SettingsCustomDefinition>(field.definition->payload);
        using Renderer = settings::SettingsCustomRenderer;
        if (custom.renderer == Renderer::TrayMenuOptions) {
            if (!json.isArray())
                return false;
            break;
        }
        if (custom.renderer != Renderer::DrawingToolbarEditor &&
            custom.renderer != Renderer::ScreenshotToolbarEditor &&
            custom.renderer != Renderer::RecordingToolbarEditor &&
            custom.renderer != Renderer::PinnedToolbarEditor)
            return false;
        const auto decodeLayout = [](const QJsonValue& input,
                                     storage::ScreenshotToolbarLayout* layout) {
            const auto object = input.toObject();
            if (!input.isObject() || !object.value(QStringLiteral("positions")).isArray() ||
                !stringArray(object.value(QStringLiteral("hidden")), &layout->hidden))
                return false;
            for (const auto& position : object.value(QStringLiteral("positions")).toArray()) {
                QStringList tools;
                if (!stringArray(position, &tools))
                    return false;
                layout->positions.append(tools);
            }
            return true;
        };
        storage::ScreenshotToolbarLayout layout;
        if (!decodeLayout(json, &layout))
            return false;
        // Keep strict JSON shape checks above, then submit the same canonical layout
        // the storage backend will accept so runtime writes can settle successfully.
        layout = {};
        if (!decodeLayout(normalized.value, &layout))
            return false;
        *value = QVariant::fromValue(layout);
        return true;
    }
    case Kind::Text:
    case Kind::FilePath:
    case Kind::DirectoryPath:
        if (!json.isString())
            return false;
        break;
    case Kind::MultiSelect:
        if (!json.isArray())
            return false;
        break;
    default:
        break;
    }
    *value = json.toVariant();
    return true;
}
} // namespace snow_shot::app::mcp
