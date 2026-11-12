// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QAbstractItemModel>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QThread>

#include "protocol/MdnsTypes.h"
#include "DiscoveredHostsModel.h"
#include "config/ConfigModels.h"
#include "protocol/UbusTypes.h"

class UbusClient;
class InterfacesModel;
class ConfigEditor;
class MdnsDiscovery;
namespace Plugin {
class Hub;
}
namespace Routing {
class RoutesTool;
class OspfTool;
class BgpTool;
}
class PingTool;
class TracerouteTool;
class PortScanTool;
class FilesTool;
class SqmStatsTool;
class TorchModel;
class LogTool;
class LeaseTool;
class BackupTool;
class TerminalTool;

// Owns the connection lifecycle and exposes it to QML as plain properties.
// The ubus client lives in its own thread; every call into it is queued and
// every result arrives here as a typed struct, so the UI thread never touches
// sockets, TLS or JSON.
class AppEngine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString hostname READ hostname NOTIFY stateChanged)
    Q_PROPERTY(QString model READ model NOTIFY stateChanged)
    Q_PROPERTY(QString firmware READ firmware NOTIFY stateChanged)
    Q_PROPERTY(QString kernel READ kernel NOTIFY stateChanged)
    Q_PROPERTY(QString target READ target NOTIFY stateChanged)
    Q_PROPERTY(bool trustPending READ trustPending NOTIFY stateChanged)
    Q_PROPERTY(QString trustFingerprint READ trustFingerprint NOTIFY stateChanged)
    Q_PROPERTY(QAbstractItemModel *interfaces READ interfacesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *discoveredHosts READ discoveredHosts CONSTANT)
    Q_PROPERTY(int discoveredCount READ discoveredCount NOTIFY discoveredChanged)
    Q_PROPERTY(QString discoveryStatus READ discoveryStatus NOTIFY discoveredChanged)
    Q_PROPERTY(QAbstractItemModel *configDomains READ configDomains CONSTANT)
    Q_PROPERTY(QAbstractItemModel *plugins READ pluginsModel CONSTANT)
    Q_PROPERTY(QObject *pingTool READ pingTool CONSTANT)
    Q_PROPERTY(QObject *tracerouteTool READ tracerouteTool CONSTANT)
    Q_PROPERTY(QObject *portScanTool READ portScanTool CONSTANT)
    Q_PROPERTY(QObject *filesTool READ filesTool CONSTANT)
    Q_PROPERTY(QObject *sqmStatsTool READ sqmStatsTool CONSTANT)
    Q_PROPERTY(QObject *logTool READ logTool CONSTANT)
    Q_PROPERTY(QObject *leaseTool READ leaseTool CONSTANT)
    Q_PROPERTY(QObject *backupTool READ backupTool CONSTANT)
    Q_PROPERTY(QObject *terminalTool READ terminalTool CONSTANT)
    Q_PROPERTY(QObject *routesTool READ routesTool CONSTANT)
    Q_PROPERTY(QObject *ospfTool READ ospfTool CONSTANT)
    Q_PROPERTY(QObject *bgpTool READ bgpTool CONSTANT)
    Q_PROPERTY(QAbstractItemModel *torchModel READ torchModel CONSTANT)

public:
    explicit AppEngine(QObject *parent = nullptr);
    ~AppEngine() override;

    bool isConnected() const { return m_state == State::Connected; }
    bool isBusy() const { return m_state == State::Connecting; }
    bool trustPending() const { return m_state == State::TrustPending; }
    QString statusText() const;
    QString errorText() const { return m_error; }
    QString hostname() const { return m_board.hostname; }
    QString model() const { return m_board.model; }
    QString firmware() const { return m_board.firmwareLabel(); }
    QString kernel() const { return m_board.kernel; }
    QString target() const { return m_board.target; }
    QString trustFingerprint() const { return QString::fromLatin1(m_pendingPin); }
    QAbstractItemModel *interfacesModel() const;
    QAbstractItemModel *discoveredHosts() { return &m_discovered; }
    int discoveredCount() const { return m_discovered.count(); }
    QString discoveryStatus() const { return m_discoveryStatus; }
    QAbstractItemModel *configDomains() { return &m_domains; }
    QAbstractItemModel *pluginsModel() const;
    Plugin::Hub *pluginHub() const { return m_plugins; }
    QObject *pingTool();
    QObject *tracerouteTool();
    QObject *portScanTool();
    QObject *filesTool();
    QObject *sqmStatsTool();
    QObject *logTool();
    QObject *leaseTool();
    QObject *backupTool();
    QObject *terminalTool();
    QObject *routesTool();
    QObject *ospfTool();
    QObject *bgpTool();
    QAbstractItemModel *torchModel();

    // The ubus client this engine owns. The integration tests use it when a
    // staged UCI change must land in the same rpcd session the editor reads
    // (rpcd keeps each session's staged delta separate).
    UbusClient *client() const { return m_client; }

    Q_INVOKABLE void connectTo(const QString &host, const QString &port, const QString &username,
                               const QString &password);
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void rejectCertificate();
    Q_INVOKABLE void disconnectFromRouter();
    // One editor per domain id, created on first open and owned by the engine.
    Q_INVOKABLE QObject *configEditor(const QString &domainId);
    // One session per plugin id; the QML view of that plugin reads it.
    Q_INVOKABLE QObject *openPlugin(const QString &pluginId);
    Q_INVOKABLE void rebootRouter();

signals:
    void stateChanged();
    void discoveredChanged();
    void rebootRequested();
    void rebootFailed(const QString &error);

private:
    enum class State { Idle, Connecting, TrustPending, Connected, Failed };

    void startLogin();
    void setState(State state, const QString &error = {});
    void resetTools();
    QString settingsKey() const;

    void onLoggedIn(const Ubus::Session &session);
    void onLoginFailed(const Ubus::Error &error);
    void onBoardReceived(const Ubus::BoardInfo &board);
    void onBoardFailed(const Ubus::Error &error);
    void onCertificateRequired(const QString &host, const QByteArray &fingerprint);
    void onCertificateRejected(const QString &reason);
    void onHostDiscovered(const Mdns::Host &host);

    QThread m_thread;
    UbusClient *m_client = nullptr;
    InterfacesModel *m_interfaces = nullptr;
    ConfigDomainsModel m_domains;
    QHash<QString, ConfigEditor *> m_editors;
    PingTool *m_ping = nullptr;
    TracerouteTool *m_traceroute = nullptr;
    PortScanTool *m_portScan = nullptr;
    FilesTool *m_files = nullptr;
    SqmStatsTool *m_sqm = nullptr;
    Routing::RoutesTool *m_routes = nullptr;
    Routing::OspfTool *m_ospf = nullptr;
    Routing::BgpTool *m_bgp = nullptr;
    TorchModel *m_torch = nullptr;
    LogTool *m_log = nullptr;
    LeaseTool *m_leases = nullptr;
    BackupTool *m_backup = nullptr;
    TerminalTool *m_terminal = nullptr;
    QThread m_discoveryThread;
    MdnsDiscovery *m_discovery = nullptr;
    DiscoveredHostsModel m_discovered;
    QString m_discoveryStatus;
    Plugin::Hub *m_plugins = nullptr;
    State m_state = State::Idle;
    Ubus::BoardInfo m_board;
    QString m_host;
    quint16 m_port = 443;
    QString m_username;
    QString m_password;
    QByteArray m_pendingPin;
    QString m_error;
};
