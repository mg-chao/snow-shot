// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <utility>

namespace snow_shot::app::detail {

struct CloudLayout {
    struct Item {
        int group = 0;
        int order = 0;
    };
    QMap<QString, Item> visible;
    QStringList hidden;

    static CloudLayout fromJson(const QJsonValue& value) {
        CloudLayout layout;
        int groupIndex = 0;
        int order = 0;
        if (value.isArray()) {
            for (const auto id : value.toArray())
                layout.visible.insert(id.toString(), {groupIndex++, order++});
        } else {
            const auto object = value.toObject();
            for (const auto group : object.value(QStringLiteral("positions")).toArray()) {
                for (const auto item : group.toArray())
                    layout.visible.insert(item.toString(), {groupIndex, order++});
                ++groupIndex;
            }
            for (const auto id : object.value(QStringLiteral("hidden")).toArray())
                layout.hidden.append(id.toString());
        }
        return layout;
    }

    int visibility(const QString& item) const {
        if (visible.contains(item))
            return 1;
        return hidden.contains(item) ? 2 : 0;
    }

    template <typename Compare>
    std::optional<bool> compare(const QString& first, const QString& second, Compare fn) const {
        const auto a = visible.constFind(first);
        const auto b = visible.constFind(second);
        if (a == visible.cend() || b == visible.cend())
            return std::nullopt;
        return fn(*a, *b);
    }
};

template <typename T>
T mergeLayoutProperty(T base, T cloud, T projection, T local, bool& conflict) {
    if (local == projection)
        return cloud;
    if (cloud != base && cloud != local)
        conflict = true;
    return local;
}

using LayoutRelations = QMap<QString, QSet<QString>>;

inline std::optional<QStringList> orderLayoutItems(QStringList items,
                                                   LayoutRelations predecessors) {
    items.sort();
    QStringList ordered;
    ordered.reserve(items.size());
    while (!items.isEmpty()) {
        const auto ready = std::find_if(items.cbegin(), items.cend(), [&](const QString& id) {
            const auto incoming = predecessors.constFind(id);
            return incoming == predecessors.cend() || incoming->isEmpty();
        });
        if (ready == items.cend())
            return std::nullopt;
        const auto id = *ready;
        ordered.append(id);
        items.removeOne(id);
        for (auto& incoming : predecessors)
            incoming.remove(id);
    }
    return ordered;
}

// An older client can only edit properties it could observe. Merge item visibility,
// grouping and pairwise order, then verify that those constraints describe a layout.
inline QJsonValue mergeCloudLayout(const QJsonValue& rawBase, const QJsonValue& rawCloud,
                                   const QJsonValue& projectedBase, const QJsonValue& localValue) {
    const auto undefined = QJsonValue(QJsonValue::Undefined);
    const bool flat = rawCloud.isArray();
    if (flat != localValue.isArray() || flat != rawBase.isArray() ||
        flat != projectedBase.isArray() ||
        (!flat && (!rawCloud.isObject() || !rawBase.isObject() || !projectedBase.isObject() ||
                   !localValue.isObject())))
        return undefined;
    const auto base = CloudLayout::fromJson(rawBase);
    const auto cloud = CloudLayout::fromJson(rawCloud);
    const auto projection = CloudLayout::fromJson(projectedBase);
    const auto local = CloudLayout::fromJson(localValue);
    QSet<QString> all;
    for (const auto* layout : {&base, &cloud, &projection, &local}) {
        for (auto it = layout->visible.cbegin(); it != layout->visible.cend(); ++it)
            all.insert(it.key());
        for (const auto& id : layout->hidden)
            all.insert(id);
    }
    QStringList visible;
    QSet<QString> hidden;
    bool conflict = false;
    for (const auto& id : all) {
        const int visibility =
            mergeLayoutProperty(base.visibility(id), cloud.visibility(id),
                                projection.visibility(id), local.visibility(id), conflict);
        if (visibility == 1)
            visible.append(id);
        else if (visibility == 2)
            hidden.insert(id);
    }
    if (conflict)
        return undefined;

    visible.sort();
    LayoutRelations predecessors;
    LayoutRelations groupPeers;
    LayoutRelations separateGroups;
    for (qsizetype i = 0; i < visible.size(); ++i) {
        const auto& first = visible.at(i);
        for (qsizetype j = i + 1; j < visible.size(); ++j) {
            const auto& second = visible.at(j);
            const auto mergePair = [&](auto compare) {
                const auto before = projection.compare(first, second, compare);
                const auto after = local.compare(first, second, compare);
                return mergeLayoutProperty(base.compare(first, second, compare),
                                           cloud.compare(first, second, compare), before,
                                           after.has_value() ? after : before, conflict);
            };
            const auto order = mergePair([](auto a, auto b) { return a.order < b.order; });
            if (order.has_value()) {
                if (*order)
                    predecessors[second].insert(first);
                else
                    predecessors[first].insert(second);
            }
            if (flat)
                continue;
            const auto grouped = mergePair([](auto a, auto b) { return a.group == b.group; });
            if (grouped == true) {
                groupPeers[first].insert(second);
                groupPeers[second].insert(first);
            } else if (grouped == false) {
                separateGroups[first].insert(second);
            }
        }
    }
    if (conflict)
        return undefined;

    if (flat) {
        const auto ordered = orderLayoutItems(std::move(visible), std::move(predecessors));
        return ordered ? QJsonValue(QJsonArray::fromStringList(*ordered)) : undefined;
    }

    QMap<QString, QString> groupOf;
    QMap<QString, QStringList> groups;
    for (const auto& id : visible) {
        if (groupOf.contains(id))
            continue;
        auto& group = groups[id];
        group.append(id);
        groupOf.insert(id, id);
        for (qsizetype index = 0; index < group.size(); ++index) {
            const auto peers = groupPeers.constFind(group.at(index));
            if (peers == groupPeers.cend())
                continue;
            for (const auto& peer : *peers) {
                if (!groupOf.contains(peer)) {
                    groupOf.insert(peer, id);
                    group.append(peer);
                }
            }
        }
    }
    for (auto it = separateGroups.cbegin(); it != separateGroups.cend(); ++it) {
        for (const auto& other : it.value()) {
            if (groupOf.value(it.key()) == groupOf.value(other))
                return undefined;
        }
    }

    // Contract each required group before ordering, so unrelated items cannot split it.
    LayoutRelations groupOrder;
    QMap<QString, LayoutRelations> itemOrder;
    for (auto it = predecessors.cbegin(); it != predecessors.cend(); ++it) {
        const auto group = groupOf.value(it.key());
        for (const auto& predecessor : it.value()) {
            const auto previousGroup = groupOf.value(predecessor);
            if (previousGroup == group)
                itemOrder[group][it.key()].insert(predecessor);
            else
                groupOrder[group].insert(previousGroup);
        }
    }
    const auto orderedGroups = orderLayoutItems(groups.keys(), std::move(groupOrder));
    if (!orderedGroups)
        return undefined;
    QJsonArray positions;
    for (const auto& group : *orderedGroups) {
        const auto ordered = orderLayoutItems(groups.value(group), itemOrder.value(group));
        if (!ordered)
            return undefined;
        positions.append(QJsonArray::fromStringList(*ordered));
    }

    QJsonArray hiddenItems;
    for (const auto* layout : {&cloud, &local, &base}) {
        for (const auto& id : layout->hidden) {
            if (hidden.remove(id))
                hiddenItems.append(id);
        }
    }
    return QJsonObject{{QStringLiteral("positions"), positions},
                       {QStringLiteral("hidden"), hiddenItems}};
}
} // namespace snow_shot::app::detail
