// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AppEngine.h"

#include <QCoreApplication>
#include <QSettings>

#include "models/InterfacesModel.h"
#include "DiscoveredHostsModel.h"
#include "config/ConfigEditor.h"
#include "plugins/PluginHub.h"
#include "plugins/PluginSession.h"
#include "protocol/MdnsDiscovery.h"
#include "protocol/UbusClient.h"
#include "tools/FilesTool.h"
#include "tools/BackupTool.h"
#include "tools/LeaseTool.h"
#include "tools/LogTool.h"
#include "tools/TerminalTool.h"
#include "tools/PingTool.h"
#include "tools/PortScanTool.h"
#include "routing/RoutingTools.h"
#include "tools/SqmStatsTool.h"
#include "tools/TorchModel.h"
#include "tools/TracerouteTool.h"

AppEngine::AppEngine(QObject *parent) : QObject(parent)
{
    if (QCoreApplication::organizationName().isEmpty())
        QCoreApplication::setOrganizationName(QStringLiteral("open-broadcast"));
    if (QCoreApplication::applicationName().isEmpty())
        QCoreApplication::setApplicationName(QStringLiteral("router-console"));

    qRegisterMetaType<Ubus::Session>();
    qRegisterMetaType<Ubus::BoardInfo>();
    qRegisterMetaType<Ubus::Error>();
    qRegisterMetaType<Mdns::Host>();

    m_client = new UbusClient;
    m_interfaces = new InterfacesModel(m_client, this);
    m_plugins = new Plugin::Hub(Plugin::rootDirectory(), m_client, this);
    m_ping = new PingTool(m_client, this);
    m_traceroute = new TracerouteTool(m_client, this);
    m_portScan = new PortScanTool(m_client, this);
    m_files = new FilesTool(m_client, this);
    m_sqm = new SqmStatsTool(m_client, this);
    m_routes = new Routing::RoutesTool(m_client, this);
    m_ospf = new Routing::OspfTool(m_client, this);
    m_bgp = new Routing::BgpTool(m_client, this);
    m_torch = new TorchModel(m_client, this);
    m_log = new LogTool(m_client, this);
    m_leases = new LeaseTool(m_client, this);
    m_backup = new BackupTool(m_client, this);
    m_terminal = new TerminalTool(m_client, this);
    m_client->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_client, &QObject::deleteLater);

    connect(m_client, &UbusClient::loggedIn, this, &AppEngine::onLoggedIn);
    connect(m_client, &UbusClient::loginFailed, this, &AppEngine::onLoginFailed);
    connect(m_client, &UbusClient::boardReceived, this, &AppEngine::onBoardReceived);
    connect(m_client, &UbusClient::boardFailed, this, &AppEngine::onBoardFailed);
    connect(m_client, &UbusClient::certificateRequired, this, &AppEngine::onCertificateRequired);
    connect(m_client, &UbusClient::certificateRejected, this, &AppEngine::onCertificateRejected);
    connect(m_client, &UbusClient::rebootRequested, this, &AppEngine::rebootRequested);
    connect(m_client, &UbusClient::rebootFailed, this, [this](const Ubus::Error &error) {
        emit rebootFailed(error.message.isEmpty() ? QStringLiteral("system.reboot failed")
                                                  : error.message);
    });

    m_thread.start();

    // Discovery never blocks connecting: it starts on its own thread and the
    // connect window falls back to manual entry when nothing is found.
    m_discovery = new MdnsDiscovery;
    m_discovery->moveToThread(&m_discoveryThread);
    connect(&m_discoveryThread, &QThread::finished, m_discovery, &QObject::deleteLater);
    connect(m_discovery, &MdnsDiscovery::hostFound, this, &AppEngine::onHostDiscovered);
    connect(m_discovery, &MdnsDiscovery::stateChanged, this, [this](const QString &text) {
        m_discoveryStatus = text;
        emit discoveredChanged();
    });
    connect(&m_discovered, &DiscoveredHostsModel::countChanged, this,
            &AppEngine::discoveredChanged);
    m_discoveryThread.start();
    QMetaObject::invokeMethod(m_discovery, &MdnsDiscovery::start, Qt::QueuedConnection);
}

AppEngine::~AppEngine()
{
    if (m_discovery) {
        QMetaObject::invokeMethod(m_discovery, &MdnsDiscovery::stop, Qt::BlockingQueuedConnection);
    }
    m_discoveryThread.quit();
    m_discoveryThread.wait();
    m_thread.quit();
    m_thread.wait();
}

void AppEngine::onHostDiscovered(const Mdns::Host &host)
{
    m_discovered.upsert(host);
}

QString AppEngine::settingsKey() const
{
    return QStringLiteral("certificates/%1:%2").arg(m_host).arg(m_port);
}

QAbstractItemModel *AppEngine::interfacesModel() const
{
    return m_interfaces;
}

QObject *AppEngine::pingTool()
{
    return m_ping;
}

QObject *AppEngine::tracerouteTool()
{
    return m_traceroute;
}

QObject *AppEngine::portScanTool()
{
    return m_portScan;
}

QObject *AppEngine::filesTool()
{
    return m_files;
}

QObject *AppEngine::sqmStatsTool()
{
    return m_sqm;
}

QObject *AppEngine::logTool()
{
    return m_log;
}

QObject *AppEngine::leaseTool()
{
    return m_leases;
}

QObject *AppEngine::backupTool()
{
    return m_backup;
}

QObject *AppEngine::terminalTool()
{
    return m_terminal;
}

QObject *AppEngine::routesTool()
{
    return m_routes;
}

QObject *AppEngine::ospfTool()
{
    return m_ospf;
}

QObject *AppEngine::bgpTool()
{
    return m_bgp;
}

QAbstractItemModel *AppEngine::torchModel()
{
    return m_torch;
}

QString AppEngine::statusText() const
{
    switch (m_state) {
    case State::Idle: return QStringLiteral("not connected");
    case State::Connecting: return QStringLiteral("connecting to %1").arg(m_host);
    case State::TrustPending: return QStringLiteral("router certificate is not trusted");
    case State::Connected: return QStringLiteral("connected to %1").arg(m_host);
    case State::Failed: return QStringLiteral("connect failed");
    }
    return {};
}

void AppEngine::setState(State state, const QString &error)
{
    if (m_state == state && m_error == error)
        return;
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void AppEngine::connectTo(const QString &host, const QString &port, const QString &username,
                          const QString &password)
{
    QString trimmedHost = host.trimmed();
    quint16 portNumber = port.trimmed().toUShort();
    if (port.isEmpty()) {
        // Accept "host:port" in the host field; default to 443 otherwise.
        const int colon = trimmedHost.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !trimmedHost.contains(QLatin1String("::"))) {
            bool ok = false;
            const quint16 parsed = trimmedHost.mid(colon + 1).toUShort(&ok);
            if (ok) {
                portNumber = parsed;
                trimmedHost = trimmedHost.left(colon);
            }
        }
    }
    if (portNumber == 0)
        portNumber = 443;

    m_host = trimmedHost;
    m_port = portNumber;
    m_username = username.trimmed();
    m_password = password;
    m_board = {};
    m_pendingPin.clear();

    setState(State::Connecting);
    startLogin();
}

void AppEngine::startLogin()
{
    if (m_host.isEmpty()) {
        setState(State::Failed, QStringLiteral("host is empty"));
        return;
    }
    if (m_password.isNull()) {
        setState(State::Failed, QStringLiteral("no password entered"));
        return;
    }

    const QUrl endpoint(QStringLiteral("https://%1:%2/ubus").arg(m_host).arg(m_port));
    QSettings settings;
    const QByteArray pin = settings.value(settingsKey()).toByteArray();

    setState(State::Connecting);
    UbusClient *client = m_client;
    const QString user = m_username;
    const QString password = m_password;
    QMetaObject::invokeMethod(
        client,
        [client, endpoint, pin, user, password] {
            client->setPinnedFingerprint(pin);
            client->login(endpoint, user, password);
        },
        Qt::QueuedConnection);
}

void AppEngine::trustCertificate()
{
    if (m_pendingPin.isEmpty())
        return;

    QSettings settings;
    settings.setValue(settingsKey(), m_pendingPin);
    settings.sync();
    m_pendingPin.clear();
    startLogin();
}

void AppEngine::rejectCertificate()
{
    m_pendingPin.clear();
    setState(State::Failed, QStringLiteral("router certificate rejected"));
}

QAbstractItemModel *AppEngine::pluginsModel() const
{
    return m_plugins->plugins();
}

QObject *AppEngine::openPlugin(const QString &pluginId)
{
    if (!isConnected())
        return nullptr;
    const QSettings settings;
    m_plugins->setEndpoint(QUrl(QStringLiteral("https://%1:%2").arg(m_host).arg(m_port)),
                           settings.value(settingsKey()).toByteArray());
    return m_plugins->open(pluginId);
}

void AppEngine::disconnectFromRouter()
{
    UbusClient *client = m_client;
    QMetaObject::invokeMethod(client, &UbusClient::closeSession, Qt::QueuedConnection);
    m_plugins->stopAll();
    m_interfaces->stop();
    resetTools();
    m_board = {};
    setState(State::Idle);
}

void AppEngine::resetTools()
{
    m_torch->stop();
    m_ping->reset();
    m_traceroute->reset();
    m_portScan->reset();
    m_files->reset();
    m_sqm->reset();
    m_log->reset();
    m_leases->reset();
    m_backup->reset();
    m_terminal->reset();
    m_routes->reset();
    m_ospf->reset();
    m_bgp->reset();
}

QObject *AppEngine::configEditor(const QString &domainId)
{
    if (ConfigEditor *existing = m_editors.value(domainId))
        return existing;

    const Config::DomainSpec *spec = Config::domain(domainId);
    if (!spec)
        return nullptr;

    auto *editor = new ConfigEditor(m_client, *spec, this);
    m_editors.insert(domainId, editor);
    return editor;
}

void AppEngine::rebootRouter()
{
    UbusClient *client = m_client;
    QMetaObject::invokeMethod(client, &UbusClient::systemReboot, Qt::QueuedConnection);
}

void AppEngine::onLoggedIn(const Ubus::Session &session)
{
    Q_UNUSED(session);
    UbusClient *client = m_client;
    QMetaObject::invokeMethod(client, &UbusClient::fetchBoard, Qt::QueuedConnection);
}

void AppEngine::onLoginFailed(const Ubus::Error &error)
{
    // The certificate prompt is its own state; the aborted request that follows
    // it is not a connection failure.
    if (error.status == Ubus::Status::CertificateRequired)
        return;
    m_interfaces->stop();
    setState(State::Failed, QStringLiteral("login: %1").arg(error.message));
}

void AppEngine::onBoardReceived(const Ubus::BoardInfo &board)
{
    m_board = board;
    m_terminal->setHostname(board.hostname);
    setState(State::Connected);
    m_interfaces->start();
}

void AppEngine::onBoardFailed(const Ubus::Error &error)
{
    m_interfaces->stop();
    setState(State::Failed, QStringLiteral("system.board: %1").arg(error.message));
}

void AppEngine::onCertificateRequired(const QString &host, const QByteArray &fingerprint)
{
    Q_UNUSED(host);
    m_pendingPin = fingerprint;
    setState(State::TrustPending);
}

void AppEngine::onCertificateRejected(const QString &reason)
{
    m_pendingPin.clear();
    setState(State::Failed, reason);
}
