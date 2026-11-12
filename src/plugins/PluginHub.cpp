#include "PluginHub.h"

#include <QDir>
#include <QMetaObject>

#include "plugins/PluginHttpFetcher.h"
#include "plugins/PluginSession.h"
#include "protocol/UbusClient.h"

namespace Plugin {

ListModel::ListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_specs.size();
}

QVariant ListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_specs.size())
        return {};

    const Spec &spec = m_specs.at(index.row());
    switch (role) {
    case PluginIdRole: return spec.id;
    case TitleRole: return spec.title;
    case AvailableRole: return spec.isValid();
    case ErrorRole: return spec.error;
    case OrderRole: return spec.order;
    default: return {};
    }
}

QHash<int, QByteArray> ListModel::roleNames() const
{
    return {
        {PluginIdRole, QByteArrayLiteral("pluginId")},
        {TitleRole, QByteArrayLiteral("title")},
        {AvailableRole, QByteArrayLiteral("available")},
        {ErrorRole, QByteArrayLiteral("error")},
        {OrderRole, QByteArrayLiteral("order")},
    };
}

void ListModel::setSpecs(const QList<Spec> &specs)
{
    beginResetModel();
    m_specs = specs;
    endResetModel();
    emit countChanged();
}

Hub::Hub(const QString &rootDirectory, UbusClient *client, QObject *parent)
    : QObject(parent)
    , m_root(rootDirectory)
    , m_client(client)
    , m_fetcher(new HttpFetcher)
{
    m_model.setSpecs(loadPlugins(rootDirectory));

    m_fetcher->moveToThread(&m_httpThread);
    connect(&m_httpThread, &QThread::finished, m_fetcher, &QObject::deleteLater);
    m_httpThread.setObjectName(QStringLiteral("plugin-http"));
    m_httpThread.start();
}

Hub::~Hub()
{
    m_httpThread.quit();
    m_httpThread.wait();
    qDeleteAll(m_sessions);
}

QStringList Hub::errors() const
{
    QStringList problems;
    for (const Spec &spec : m_model.specs()) {
        if (!spec.isValid())
            problems.append(QStringLiteral("%1: %2").arg(spec.id, spec.error));
    }
    return problems;
}

void Hub::setEndpoint(const QUrl &origin, const QByteArray &sha256Pin)
{
    if (m_origin == origin && m_pin == sha256Pin)
        return;

    stopAll();
    qDeleteAll(m_sessions);
    m_sessions.clear();
    m_origin = origin;
    m_pin = sha256Pin;
}

void Hub::stopAll()
{
    for (Session *session : m_sessions)
        session->stop();
}

Session *Hub::sessionFor(const QString &pluginId) const
{
    return m_sessions.value(pluginId);
}

QObject *Hub::open(const QString &pluginId)
{
    if (Session *existing = m_sessions.value(pluginId)) {
        existing->start();
        return existing;
    }

    for (const Spec &spec : m_model.specs()) {
        if (spec.id != pluginId)
            continue;
        if (!spec.isValid())
            return nullptr;

        auto *session = new Session(spec, m_client, m_fetcher, m_origin, m_pin, this);
        m_sessions.insert(pluginId, session);
        session->start();
        return session;
    }
    return nullptr;
}

QObject *Hub::find(const QString &pluginId) const
{
    return m_sessions.value(pluginId);
}

} // namespace Plugin
