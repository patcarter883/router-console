#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QThread>
#include <QUrl>

#include "plugins/PluginSpec.h"

class UbusClient;

namespace Plugin {

class HttpFetcher;
class Session;

// Tree rows: every plugin directory found under the root, including the ones
// whose manifest was rejected (shown with their error, never hidden).
class ListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        PluginIdRole = Qt::UserRole + 1,
        TitleRole,
        AvailableRole,
        ErrorRole,
        OrderRole,
    };

    explicit ListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_specs.size(); }
    void setSpecs(const QList<Spec> &specs);
    const QList<Spec> &specs() const { return m_specs; }

signals:
    void countChanged();

private:
    QList<Spec> m_specs;
};

// Owns the scan, the shared HTTP fetcher thread and the open plugin sessions.
// Constructed once by AppEngine; the endpoint (origin + pinned fingerprint) is
// pushed in when a router session exists, so a plugin view can only be opened
// on a live connection.
class Hub : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *plugins READ plugins CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(QStringList errors READ errors NOTIFY changed)

public:
    Hub(const QString &rootDirectory, UbusClient *client, QObject *parent = nullptr);
    ~Hub() override;

    QAbstractItemModel *plugins() { return &m_model; }
    int count() const { return m_model.rowCount(); }
    QString root() const { return m_root; }
    QStringList errors() const;

    void setEndpoint(const QUrl &origin, const QByteArray &sha256Pin);
    QUrl origin() const { return m_origin; }

    // One session per plugin, created on first open and reused afterwards.
    Q_INVOKABLE QObject *open(const QString &pluginId);
    Q_INVOKABLE QObject *find(const QString &pluginId) const;

    Session *sessionFor(const QString &pluginId) const;
    const QList<Spec> &specs() const { return m_model.specs(); }

    void stopAll();

signals:
    void changed();

private:
    QString m_root;
    UbusClient *m_client = nullptr;
    ListModel m_model;
    QThread m_httpThread;
    HttpFetcher *m_fetcher = nullptr;
    QUrl m_origin;
    QByteArray m_pin;
    QHash<QString, Session *> m_sessions;
};

} // namespace Plugin
