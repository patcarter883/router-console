#include "PluginSpec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace Plugin {

namespace {

constexpr const char *kManifestName = "plugin.json";

QString resolveView(const QString &directory, const QString &relative)
{
    if (relative.isEmpty())
        return {};
    const QFileInfo info(QDir(directory).filePath(relative));
    QFileInfo resolved(info.canonicalFilePath());
    if (!resolved.exists() || !resolved.isFile())
        return {};
    return resolved.absoluteFilePath();
}

} // namespace

const DataSource *Spec::source(const QString &sourceId) const
{
    for (const DataSource &candidate : sources) {
        if (candidate.id == sourceId)
            return &candidate;
    }
    return nullptr;
}

QStringList Spec::sourceIds() const
{
    QStringList ids;
    ids.reserve(sources.size());
    for (const DataSource &source : sources)
        ids.append(source.id);
    return ids;
}

Spec loadPlugin(const QString &directory)
{
    Spec spec;
    spec.directory = QFileInfo(directory).absoluteFilePath();
    spec.id = QFileInfo(spec.directory).fileName();

    const QString manifestPath = QDir(spec.directory).filePath(QLatin1String(kManifestName));
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        spec.error = QStringLiteral("%1 is missing").arg(QLatin1String(kManifestName));
        return spec;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        spec.error = QStringLiteral("%1: %2").arg(QLatin1String(kManifestName),
                                                 parseError.errorString());
        return spec;
    }
    if (!doc.isObject()) {
        spec.error = QStringLiteral("%1: not a JSON object").arg(QLatin1String(kManifestName));
        return spec;
    }

    const QJsonObject root = doc.object();
    const QString declaredId = root.value(QStringLiteral("id")).toString();
    if (!declaredId.isEmpty())
        spec.id = declaredId;
    if (!QRegularExpression(QStringLiteral("^[a-z0-9][a-z0-9-]{1,47}$"))
             .match(spec.id)
             .hasMatch()) {
        spec.error = QStringLiteral("id '%1' is not a lowercase, hyphenated name").arg(spec.id);
        return spec;
    }

    spec.title = root.value(QStringLiteral("title")).toString();
    if (spec.title.isEmpty())
        spec.title = spec.id;
    spec.order = root.value(QStringLiteral("order")).toInt(100);
    spec.intervalMs = qMax(minimumIntervalMs,
                           root.value(QStringLiteral("interval")).toInt(2000));

    const QString view = root.value(QStringLiteral("view")).toString();
    if (view.isEmpty()) {
        spec.error = QStringLiteral("no view declared");
        return spec;
    }
    spec.viewPath = resolveView(spec.directory, view);
    if (spec.viewPath.isEmpty()) {
        spec.error = QStringLiteral("view '%1' does not exist").arg(view);
        return spec;
    }

    const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    if (sources.isEmpty())
        spec.error = QStringLiteral("no data sources declared");

    for (const QJsonValue &value : sources) {
        const QJsonObject entry = value.toObject();
        DataSource source;
        source.id = entry.value(QStringLiteral("id")).toString();
        source.intervalMs = entry.value(QStringLiteral("interval")).toInt(0);

        const QString transport = entry.value(QStringLiteral("transport")).toString();
        if (transport == QLatin1String("http")) {
            source.transport = Transport::Http;
            source.path = entry.value(QStringLiteral("path")).toString();
            if (!source.path.startsWith(QLatin1Char('/')))
                spec.error = QStringLiteral("source '%1': http path must start with '/'")
                                 .arg(source.id);
        } else {
            source.transport = Transport::Ubus;
            source.object = entry.value(QStringLiteral("object")).toString();
            source.method = entry.value(QStringLiteral("method")).toString();
            source.args = entry.value(QStringLiteral("args")).toObject();
            if (source.object.isEmpty() || source.method.isEmpty())
                spec.error = QStringLiteral("source '%1': ubus object and method are required")
                                 .arg(source.id);
        }

        if (source.id.isEmpty()) {
            spec.error = QStringLiteral("a source has no id");
            continue;
        }
        for (const DataSource &existing : spec.sources) {
            if (existing.id == source.id)
                spec.error = QStringLiteral("duplicate source id '%1'").arg(source.id);
        }
        spec.sources.append(source);
    }

    return spec;
}

QList<Spec> loadPlugins(const QString &rootDirectoryPath)
{
    QList<Spec> specs;
    const QDir root(rootDirectoryPath);
    if (!root.exists())
        return specs;

    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString directory = root.filePath(entry);
        if (!QFileInfo::exists(QDir(directory).filePath(QLatin1String(kManifestName))))
            continue;
        specs.append(loadPlugin(directory));
    }

    std::sort(specs.begin(), specs.end(), [](const Spec &a, const Spec &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return specs;
}

QString rootDirectory()
{
    const QString override = qEnvironmentVariable("RC_PLUGIN_DIR");
    if (!override.isEmpty())
        return override;

    auto holdsPlugins = [](const QString &directory) {
        const QDir root(directory);
        if (!root.exists())
            return false;
        for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(QDir(root.filePath(entry)).filePath(QLatin1String(kManifestName))))
                return true;
        }
        return false;
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{QDir(appDir).filePath(QStringLiteral("plugins")),
                                 QDir(appDir).filePath(QStringLiteral("../plugins"))};
    for (const QString &candidate : candidates) {
        if (holdsPlugins(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }

#ifdef RC_SOURCE_PLUGIN_DIR
    return QStringLiteral(RC_SOURCE_PLUGIN_DIR);
#else
    return QDir(appDir).filePath(QStringLiteral("plugins"));
#endif
}

} // namespace Plugin
