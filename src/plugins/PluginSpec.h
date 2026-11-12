#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// A plugin is a directory under plugins/ holding plugin.json plus the QML view
// it names. Nothing about a plugin is compiled into the client: the loader reads
// the directory at start-up, so adding one is a file drop, not a core change.
namespace Plugin {

enum class Transport { Ubus, Http };

// One data call the view needs. `ubus` calls go through the session the client
// already holds; `http` fetches a router path (e.g. a /luci-static/ JSON file)
// over the pinned TLS connection.
struct DataSource {
    QString id;
    Transport transport = Transport::Ubus;
    QString object;
    QString method;
    QJsonObject args;
    QString path;
    int intervalMs = 0; // 0: follow the plugin interval
};

struct Spec {
    QString id;
    QString title;
    QString directory;
    QString viewPath; // absolute
    int order = 100;
    int intervalMs = 2000;
    QList<DataSource> sources;
    QString error; // non-empty when the manifest was rejected

    bool isValid() const { return error.isEmpty(); }
    const DataSource *source(const QString &sourceId) const;
    QStringList sourceIds() const;
};

// Parses one directory. A rejected manifest still yields a Spec (id/title from
// the directory, error set) so the tree can show it as broken instead of hiding
// it.
Spec loadPlugin(const QString &directory);

// Every subdirectory of rootDirectory that holds a plugin.json, ordered by
// `order` then title.
QList<Spec> loadPlugins(const QString &rootDirectory);

// RC_PLUGIN_DIR, else <appdir>/plugins, else <appdir>/../plugins, else the
// source tree path baked in at configure time (dev runs from build/).
QString rootDirectory();

constexpr int minimumIntervalMs = 250;

} // namespace Plugin
