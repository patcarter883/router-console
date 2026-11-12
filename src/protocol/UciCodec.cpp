// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "UciCodec.h"

#include <QJsonArray>
#include <QJsonValue>

namespace Ubus::UciCodec {

namespace {

constexpr const char *kValues = "values";
constexpr const char *kChanges = "changes";

bool isSectionObject(const QJsonValue &value)
{
    return value.isObject() && value.toObject().contains(QStringLiteral(".type"));
}

} // namespace

UciSection parseSection(const QJsonObject &values)
{
    UciSection section;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String(".name")) {
            section.name = it.value().toString();
        } else if (key == QLatin1String(".type")) {
            section.type = it.value().toString();
        } else if (key == QLatin1String(".anonymous")) {
            section.anonymous = it.value().toBool();
        } else if (key == QLatin1String(".index")) {
            section.index = it.value().toInt();
        } else if (!key.startsWith(QLatin1Char('.'))) {
            // UCI list options come back as a JSON array (zone network,
            // rule icmp_type); the editor shows a UCI list as one
            // space-separated value, which is also what `uci get` prints.
            if (it.value().isArray()) {
                QStringList items;
                for (const QJsonValue &item : it.value().toArray()) {
                    const QString text = item.toVariant().toString();
                    if (!text.isEmpty())
                        items.append(text);
                }
                section.options.insert(key, items.join(QLatin1Char(' ')));
            } else {
                section.options.insert(key, it.value().toVariant().toString());
            }
        }
    }
    return section;
}

UciConfig parseConfig(const QString &config, const QJsonObject &result)
{
    UciConfig parsed;
    parsed.name = config;

    const QJsonObject values = result.value(QLatin1String(kValues)).toObject();
    if (values.isEmpty())
        return parsed;

    // A flat map is one section (`get` with config+section); a map of objects is
    // every section of a type (`get` with config+type).
    if (isSectionObject(values)) {
        UciSection section = parseSection(values);
        if (section.name.isEmpty())
            section.name = result.value(QStringLiteral("section")).toString();
        parsed.sections.append(section);
        return parsed;
    }

    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!it.value().isObject())
            continue;
        UciSection section = parseSection(it.value().toObject());
        if (section.name.isEmpty())
            section.name = it.key();
        parsed.sections.append(section);
    }
    return parsed;
}

UciChangeSet parseChanges(const QString &config, const QJsonObject &result)
{
    UciChangeSet parsed;
    parsed.config = config;

    for (const QJsonValue &entry : result.value(QLatin1String(kChanges)).toArray()) {
        const QJsonArray tuple = entry.toArray();
        if (tuple.size() < 3)
            continue;
        UciChange change;
        change.operation = tuple.at(0).toString();
        change.section = tuple.at(1).toString();
        change.option = tuple.at(2).toString();
        if (tuple.size() > 3)
            change.value = tuple.at(3).toVariant().toString();
        parsed.changes.append(change);
    }
    return parsed;
}

QString parseOptionValue(const QJsonObject &result)
{
    return result.value(QStringLiteral("value")).toVariant().toString();
}

QJsonObject getTypeArgs(const QString &config, const QString &sectionType)
{
    return {{QStringLiteral("config"), config}, {QStringLiteral("type"), sectionType}};
}

QJsonObject getSectionArgs(const QString &config, const QString &section)
{
    return {{QStringLiteral("config"), config}, {QStringLiteral("section"), section}};
}

QJsonObject getOptionArgs(const QString &config, const QString &section, const QString &option)
{
    QJsonObject args = getSectionArgs(config, section);
    args.insert(QStringLiteral("option"), option);
    return args;
}

QJsonObject changesArgs(const QString &config)
{
    return {{QStringLiteral("config"), config}};
}

// rpcd's uci `set` takes a `values` table (option/value pairs), not a single
// option/value pair: the pair form answers INVALID_ARGUMENT. A list option's
// array value creates the list elements; a space-separated string would become
// one element containing a space.
QJsonObject setArgs(const UciSetRequest &request)
{
    QJsonValue value = request.value;
    if (request.list) {
        QJsonArray array;
        for (const QString &item : request.value.split(QLatin1Char(' '), Qt::SkipEmptyParts))
            array.append(item);
        value = array;
    }
    return {{QStringLiteral("config"), request.config},
            {QStringLiteral("section"), request.section},
            {QStringLiteral("values"), QJsonObject{{request.option, value}}}};
}

QJsonObject revertArgs(const QString &config)
{
    return {{QStringLiteral("config"), config}};
}

QJsonObject applyArgs(int rollbackTimeoutSeconds)
{
    return {{QStringLiteral("rollback"), true}, {QStringLiteral("timeout"), rollbackTimeoutSeconds}};
}

} // namespace Ubus::UciCodec
