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
            section.options.insert(key, it.value().toVariant().toString());
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
// option/value pair: the pair form answers INVALID_ARGUMENT.
QJsonObject setArgs(const UciSetRequest &request)
{
    return {{QStringLiteral("config"), request.config},
            {QStringLiteral("section"), request.section},
            {QStringLiteral("values"),
             QJsonObject{{request.option, request.value}}}};
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
