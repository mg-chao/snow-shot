// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/app/cloudsettingslayout_p.h"
#include "snow_shot/storage/cloudsyncconfiguration.h"
#include "snow_shot/storage/configurationschema.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {
using namespace Qt::StringLiterals;
using snow_shot::app::detail::mergeCloudLayout;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QJsonObject toolbar(std::initializer_list<QStringList> groups, const QStringList& hidden = {}) {
    QJsonArray positions;
    for (const auto& group : groups)
        positions.append(QJsonArray::fromStringList(group));
    return {{u"positions"_s, positions}, {u"hidden"_s, QJsonArray::fromStringList(hidden)}};
}

void toolbarMergePreservesUnrecognizedItems() {
    const auto a = u"a"_s;
    const auto b = u"b"_s;
    const auto c = u"c"_s;
    const auto future = u"future"_s;
    const auto hidden = u"hidden-item"_s;

    const auto base = toolbar({{a, b}, {future}, {c}});
    const auto projection = toolbar({{a, b}, {c}});
    const auto changedLocally = toolbar({{b, a}, {c}});
    const auto changedInCloud = toolbar({{a, b}, {future}}, {hidden, c});
    const auto merged = mergeCloudLayout(base, changedInCloud, projection, changedLocally);
    require(merged == toolbar({{b, a}, {future}}, {hidden, c}),
            "reordering a known group preserves cloud-only groups and independent visibility");
}

void toolbarMergeKeepsAnIrreconcilableEditLocal() {
    const auto a = u"a"_s;
    const auto b = u"b"_s;
    const auto future = u"future"_s;
    const auto base = toolbar({{a, future, b}});
    const auto projection = toolbar({{a, b}});
    const auto splitGroup = toolbar({{a}, {b}});
    require(mergeCloudLayout(base, base, projection, splitGroup).isUndefined(),
            "an unknown item cannot belong to both groups after a split");

    const auto hidden = toolbar({}, {a});
    const auto deleted = toolbar({});
    const auto visible = toolbar({{a}});
    require(mergeCloudLayout(visible, deleted, visible, hidden).isUndefined(),
            "cloud deletion and local hiding are distinct layout operations");
}

void toolbarMergeDoesNotSplitGroupsAtUnrelatedItems() {
    const auto a = u"a"_s;
    const auto cloudOnly = u"b"_s;
    const auto localOnly = u"c"_s;
    const auto base = toolbar({{a}});
    const auto cloud = toolbar({{a}, {cloudOnly}});
    const auto local = toolbar({{a, localOnly}});
    require(mergeCloudLayout(base, cloud, base, local) == toolbar({{a, localOnly}, {cloudOnly}}),
            "independent additions fit after an indivisible group despite lexical item order");
}

void trayMergePreservesOrderOrKeepsTheEditLocal() {
    const auto a = u"a"_s;
    const auto b = u"b"_s;
    const auto c = u"c"_s;
    const auto future = u"future"_s;

    const QJsonArray base{a, b, c, future};
    const QJsonArray projection{a, b, c};
    const QJsonArray reorderedLocally{b, a, c};
    require(mergeCloudLayout(base, base, projection, reorderedLocally) ==
                QJsonArray{b, a, c, future},
            "tray reorder keeps a cloud-only menu entry");

    const QJsonArray conflictingInCloud{a, c, b, future};
    require(mergeCloudLayout(base, conflictingInCloud, projection, reorderedLocally).isUndefined(),
            "independent but cyclic constraints cannot produce a complete menu order");
}

void currentDeviceProjectionDropsFutureLayoutIds() {
    using namespace snow_shot::storage;
    const auto key = u"screenshot_toolbar/layout"_s;
    const auto current = ConfigurationSchema::defaultValue(key);
    auto future = current.toObject();
    auto positions = future.value(u"positions"_s).toArray();
    auto hidden = future.value(u"hidden"_s).toArray();
    positions.append(QJsonArray{u"future-visible"_s});
    hidden.append(u"future-hidden"_s);
    future.insert(u"positions"_s, positions);
    future.insert(u"hidden"_s, hidden);
    require(cloudSettingLayoutComplete(key, current) && !cloudSettingLayoutComplete(key, future) &&
                cloudSettingLocalValue(key, future, current) == cloudSettingValue(key, current),
            "a device can display its supported layout while retaining the raw future snapshot");
}
} // namespace

void runCloudSettingsLayoutTests() {
    toolbarMergePreservesUnrecognizedItems();
    toolbarMergeKeepsAnIrreconcilableEditLocal();
    toolbarMergeDoesNotSplitGroupsAtUnrelatedItems();
    trayMergePreservesOrderOrKeepsTheEditLocal();
    currentDeviceProjectionDropsFutureLayoutIds();
}
