// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUrl>

#include <functional>

#include "ToolTypes.h"
#include "RoutingTypes.h"
#include "UbusTypes.h"

class QNetworkAccessManager;
class QNetworkReply;

// ubus over HTTPS JSON-RPC (uhttpd `/ubus`). One instance per router; it must
// live in a worker thread — every request is a QUEUED signal/slot pair from the
// UI thread, and QNetworkAccessManager is never touched from the UI thread.
//
// The router serves a self-signed certificate, so the peer certificate is
// pinned by SHA-256: an unpinned host emits certificateRequired() and aborts;
// a pin mismatch is a hard failure. session tokens expire after the ACL
// `timeout` (300 s by default), so an expired or rejected session re-logins once
// and retries before reporting an error.
class UbusClient : public QObject
{
    Q_OBJECT

public:
    explicit UbusClient(QObject *parent = nullptr);

    void setPinnedFingerprint(const QByteArray &sha256Hex) { m_pin = sha256Hex; }
    QByteArray pinnedFingerprint() const { return m_pin; }

public slots:
    void login(const QUrl &endpoint, const QString &username, const QString &password);
    void fetchBoard();
    void fetchInterfaces();
    void fetchDeviceStatus();
    void closeSession();

    // UCI staged editing. Reads are per section type (rpcd has no get_all);
    // writes stage, then uciApplyStaged commits set* -> apply -> confirm.
    void uciGetConfig(const QString &config, const QString &sectionType);
    void uciGetOption(const QString &config, const QString &section, const QString &option);
    void uciChanges(const QString &config);
    void uciSet(const Ubus::UciSetRequest &request);
    void uciApplyStaged(const QString &config, const QList<Ubus::UciSetRequest> &requests,
                        int rollbackTimeoutSeconds);
    void uciRevert(const QString &config);
    void systemReboot();

    // Generic call for plugin-declared surfaces. `key` is echoed back unchanged
    // so one caller thread can keep several requests in flight.
    void callApp(const QString &key, const QString &object, const QString &method,
                 const QJsonObject &args);
    // Tool tier: rpcd file.exec / file.* / rpc-sys, and the two read surfaces
    // that have no ubus object of their own (/proc/net/nf_conntrack, tc).
    void ping(const QString &target, int count);
    void traceroute(const QString &target, int maxHops);
    void portScan(const QString &target, const QList<int> &ports);
    void fetchQdiscs();
    void fetchConntrack();
    void fetchLocalAddresses();
    void fileList(const QString &path);
    void fileRead(const QString &path);
    void fileWrite(const QString &path, const QByteArray &data);
    void fileRemove(const QString &path);
    void sysupgradeTest();
    void sysupgradeStart();
    void packageInstall(const QStringList &packages);

    // Routing (docs/frr-routing.md): daemon state, the kernel routing table via
    // vtysh, OSPF/BGP status, and the frr.conf document the config editor
    // stages. Apply = back up the file, write it, restart frr, probe the
    // daemons; FRR has no UCI-style apply/confirm.
    void fetchRoutingDaemons();
    void fetchRoutes();
    void fetchOspfStatus();
    void fetchBgpStatus();
    void frrReadConfig(const QString &path);
    void frrApplyConfig(const QString &path, const QByteArray &text, const QByteArray &backupText);
    void frrRestoreBackup(const QString &path);


signals:
    void loggedIn(const Ubus::Session &session);
    void loginFailed(const Ubus::Error &error);
    void boardReceived(const Ubus::BoardInfo &board);
    void boardFailed(const Ubus::Error &error);
    void interfacesReceived(const QList<Ubus::InterfaceInfo> &interfaces);
    void interfacesFailed(const Ubus::Error &error);
    void deviceStatusReceived(const Ubus::DeviceStatusSnapshot &snapshot);
    void deviceStatusFailed(const Ubus::Error &error);
    void certificateRequired(const QString &host, const QByteArray &sha256Hex);
    void certificateRejected(const QString &reason);

    void uciConfigReceived(const Ubus::UciConfig &config);
    void uciConfigFailed(const Ubus::Error &error);
    void uciOptionReceived(const QString &config, const QString &section, const QString &option,
                           const QString &value);
    void uciOptionFailed(const Ubus::Error &error);
    void uciChangesReceived(const Ubus::UciChangeSet &changes);
    void uciChangesFailed(const Ubus::Error &error);
    void uciStaged(const Ubus::UciSetRequest &request);
    void uciStageFailed(const Ubus::Error &error);
    void uciApplied(int changeCount);
    void uciApplyFailed(const Ubus::Error &error);
    void uciReverted(const QString &config);
    void uciRevertFailed(const Ubus::Error &error);
    void rebootRequested();
    void rebootFailed(const Ubus::Error &error);

    void appCallSucceeded(const QString &key, const QJsonObject &result);
    void appCallFailed(const QString &key, const Ubus::Error &error);
    void pingFinished(const Ubus::PingReport &report);
    void pingFailed(const Ubus::Error &error);
    void tracerouteFinished(const Ubus::TracerouteReport &report);
    void tracerouteFailed(const Ubus::Error &error);
    void portScanFinished(const Ubus::PortScanReport &report);
    void portScanFailed(const Ubus::Error &error);
    void qdiscsReceived(const QList<Ubus::QdiscStats> &qdiscs);
    void qdiscsFailed(const Ubus::Error &error);
    void conntrackReceived(const QList<Ubus::ConntrackFlow> &flows);
    void conntrackFailed(const Ubus::Error &error);
    void localAddressesReceived(const QStringList &addresses);
    void fileListingReceived(const Ubus::FileListing &listing);
    void fileListingFailed(const Ubus::Error &error);
    void fileReadFinished(const QString &path, const QByteArray &data);
    void fileReadFailed(const Ubus::Error &error);
    void fileWriteFinished(const QString &path, int bytes);
    void fileWriteFailed(const Ubus::Error &error);
    void fileRemoveFinished(const QString &path);
    void fileRemoveFailed(const Ubus::Error &error);
    void sysupgradeChecked(const Ubus::SysupgradeCheck &check);
    void sysupgradeCheckFailed(const Ubus::Error &error);
    void sysupgradeStarted();
    void sysupgradeStartFailed(const Ubus::Error &error);
    void packageInstallFinished(const Ubus::ExecResult &result);
    void packageInstallFailed(const Ubus::Error &error);

    void routingDaemonsReceived(const Routing::Daemons &daemons);
    void routesReceived(const Routing::RouteTable &table);
    void routingFailed(const Ubus::Error &error);
    void ospfStatusReceived(const Routing::OspfStatus &status);
    void bgpStatusReceived(const Routing::BgpSummary &summary);
    void frrConfigReceived(const QString &path, const QString &text);
    void frrConfigFailed(const Ubus::Error &error);
    void frrApplyProgress(const QString &stage);
    void frrApplied(const QString &path, const Routing::FrrApplyResult &result);
    void frrApplyFailed(const Ubus::Error &error);

private:
    using ResultHandler = std::function<void(const QJsonObject &result, const Ubus::Error &error)>;

    // Apply chain shared by frrApplyConfig and frrRestoreBackup.
    void frrRestartAndProbe(const QString &path);

    void call(const QString &object, const QString &method, const QJsonObject &args,
              ResultHandler handler, bool allowRelogin = true);
    void callRaw(const QString &sessionToken, const QString &object, const QString &method,
                 const QJsonObject &args, ResultHandler handler);
    void post(const QByteArray &body, ResultHandler handler);
    void queueAfterLogin(const QString &object, const QString &method, const QJsonObject &args,
                         ResultHandler handler, bool allowRelogin);
    void sessionExpired();

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_endpoint;
    QString m_username;
    QString m_password;
    Ubus::Session m_session;
    QElapsedTimer m_sessionAge;
    QByteArray m_pin;
    std::function<void()> m_afterLogin;
    int m_requestId = 0;
    bool m_loggingIn = false;
};
