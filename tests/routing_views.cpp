// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QThread>

#include <functional>

#include "config/ConfigEditor.h"
#include "routing/RoutingTools.h"
#include "vm_fixture.h"
#include "vm_probe.h"

// Routing milestone checks, one mode per ctest entry:
//   routes — `show ip route` + `ip -6 route` parsed into typed entries
//   ospf   — router-id, interface table, and the adjacency when one exists
//   bgp    — `show bgp summary` into a typed peers table
//   config — stage a router-id change in frr.conf, assert the diff, apply,
//            then revert from the backup and prove the baseline is back.
namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return predicate();
}

QString cell(const QAbstractItemModel *model, int row, const char *role)
{
    const QHash<int, QByteArray> names = model->roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        if (it.value() == role)
            return model->data(model->index(row, 0), it.key()).toString();
    }
    return {};
}

int fail(QTextStream &out, const QString &message)
{
    out << "FAILED: " << message << Qt::endl;
    out.flush();
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    const QCommandLineOption modeOption("mode", "routes | ospf | bgp | config", "mode",
                                        "routes");
    parser.addOptions({modeOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    const QString mode = parser.value(modeOption);

    AppEngine engine;
    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);
    connectOptions.connect(engine);
    if (!waitFor([&] { return engine.isConnected(); }, connectOptions.timeoutMs))
        return fail(out, QStringLiteral("connect to %1 failed: %2")
                             .arg(connectOptions.host, engine.errorText()));

    auto *routes = qobject_cast<Routing::RoutesTool *>(engine.routesTool());
    auto *ospf = qobject_cast<Routing::OspfTool *>(engine.ospfTool());
    auto *bgp = qobject_cast<Routing::BgpTool *>(engine.bgpTool());
    if (!routes || !ospf || !bgp)
        return fail(out, QStringLiteral("routing tools missing from the engine"));

    // FRR is an optional package: a router without it is a skip, not a failure.
    routes->refresh();
    if (!waitFor([&] { return !routes->daemonText().isEmpty(); }, 20000))
        return fail(out, QStringLiteral("daemon probe did not answer"));
    if (routes->daemonText().contains(QStringLiteral("not installed"))) {
        out << "SKIP: frr not installed on the router (opkg/apk add frr frr-ospfd frr-bgpd)"
            << Qt::endl;
        out.flush();
        return 3;
    }

    if (mode == QLatin1String("routes")) {
        if (!waitFor([&] { return !routes->busy() && routes->model()->totalCount() > 0; }, 30000))
            return fail(out, QStringLiteral("no routes parsed (%1)").arg(routes->errorText()));

        const int total = routes->model()->totalCount();
        if (total < 5)
            return fail(out, QStringLiteral("only %1 routes, expected the kernel table").arg(total));

        const QString connected = QStringLiteral("connected");
        const QString ospfRoutes = QStringLiteral("ospf");
        if (routes->countFor(connected) == 0)
            return fail(out, QStringLiteral("no connected route in the parsed table"));
        if (routes->countFor(QStringLiteral("kernel")) == 0
            && routes->countFor(QStringLiteral("static")) == 0)
            return fail(out, QStringLiteral("no kernel/static route: ip -6 route is not merged"));

        out << "routes: " << total << " entries, connected=" << routes->countFor(connected)
            << " ospf=" << routes->countFor(ospfRoutes)
            << " bgp=" << routes->countFor(QStringLiteral("bgp"))
            << " kernel=" << routes->countFor(QStringLiteral("kernel")) << Qt::endl;
        out << "daemons: " << routes->daemonText() << Qt::endl;

        const QAbstractItemModel *model = routes->routes();
        for (int row = 0; row < qMin(6, model->rowCount()); ++row) {
            out << "  " << cell(model, row, "prefix") << "  " << cell(model, row, "protocol")
                << "  ad " << cell(model, row, "distance") << " metric "
                << cell(model, row, "metric") << "  " << cell(model, row, "nexthop") << Qt::endl;
        }

        // Filter path: the window's protocol selector must narrow the model.
        routes->setProtocolFilter(connected);
        if (routes->routes()->rowCount() != routes->countFor(connected))
            return fail(out, QStringLiteral("protocol filter did not narrow the table"));
        out << "filter connected -> " << routes->routes()->rowCount() << " rows" << Qt::endl;
        routes->setProtocolFilter(QStringLiteral("all"));
        out << "PASS routes" << Qt::endl;
        return 0;
    }

    if (mode == QLatin1String("ospf")) {
        ospf->refresh();
        if (!waitFor([&] { return !ospf->busy(); }, 30000))
            return fail(out, QStringLiteral("ospf refresh timed out"));
        if (!ospf->errorText().isEmpty())
            return fail(out, QStringLiteral("ospf refresh failed: %1").arg(ospf->errorText()));
        if (ospf->routerId().isEmpty())
            return fail(out, QStringLiteral("no OSPF router-id: ospfd not running/configured"));
        if (ospf->interfaces()->rowCount() == 0)
            return fail(out, QStringLiteral("no OSPF interface parsed"));

        const QAbstractItemModel *interfaces = ospf->interfaces();
        out << "ospf: router-id " << ospf->routerId() << ", " << interfaces->rowCount()
            << " interface(s), " << ospf->neighborCount() << " neighbor(s), " << ospf->fullCount()
            << " full" << Qt::endl;
        for (int row = 0; row < interfaces->rowCount(); ++row) {
            out << "  if " << cell(interfaces, row, "name") << " area "
                << cell(interfaces, row, "area") << " state " << cell(interfaces, row, "ospfState")
                << " cost " << cell(interfaces, row, "cost") << " nbrs "
                << cell(interfaces, row, "neighborCount") << Qt::endl;
            if (cell(interfaces, row, "area").isEmpty())
                return fail(out, QStringLiteral("OSPF interface row without an area"));
        }

        const QAbstractItemModel *neighbors = ospf->neighbors();
        for (int row = 0; row < neighbors->rowCount(); ++row) {
            out << "  nbr " << cell(neighbors, row, "neighborId") << " state "
                << cell(neighbors, row, "ospfState") << " on " << cell(neighbors, row, "iface")
                << " up " << cell(neighbors, row, "uptime") << Qt::endl;
        }
        if (neighbors->rowCount() > 0 && ospf->fullCount() == 0)
            return fail(out, QStringLiteral("neighbors present but none reached Full"));

        int lsaCount = 0;
        for (const QVariant &row : ospf->database())
            lsaCount += row.toMap().value(QStringLiteral("count")).toInt();
        out << "database: " << ospf->database().size() << " area block(s), " << lsaCount
            << " LSA(s)" << Qt::endl;
        if (!ospf->database().isEmpty() && lsaCount == 0)
            return fail(out, QStringLiteral("database blocks parsed without any LSA count"));
        out << "PASS ospf" << Qt::endl;
        return 0;
    }

    if (mode == QLatin1String("bgp")) {
        bgp->refresh();
        if (!waitFor([&] { return !bgp->busy(); }, 30000))
            return fail(out, QStringLiteral("bgp refresh timed out"));
        if (!bgp->errorText().isEmpty())
            return fail(out, QStringLiteral("bgp refresh failed: %1").arg(bgp->errorText()));

        out << "bgp: " << bgp->statusText() << Qt::endl;
        if (bgp->localAs() == 0)
            return fail(out, QStringLiteral("no local AS: bgpd not running or no BGP instance"));

        const QAbstractItemModel *peers = bgp->peers();
        for (int row = 0; row < peers->rowCount(); ++row) {
            out << "  peer " << cell(peers, row, "address") << " AS " << cell(peers, row, "remoteAs")
                << " state " << cell(peers, row, "bgpState") << " pfx in "
                << cell(peers, row, "pfxRcvd") << " out " << cell(peers, row, "pfxSnt")
                << " up " << cell(peers, row, "uptime") << Qt::endl;
            if (cell(peers, row, "remoteAs").toUInt() == 0 || cell(peers, row, "bgpState").isEmpty())
                return fail(out, QStringLiteral("peer row without remote AS or state"));
        }
        out << "PASS bgp (" << peers->rowCount() << " peer(s), " << bgp->establishedCount()
            << " established)" << Qt::endl;
        return 0;
    }

    if (mode == QLatin1String("config")) {
        auto *editor = qobject_cast<ConfigEditor *>(engine.configEditor(QStringLiteral("frr")));
        if (!editor)
            return fail(out, QStringLiteral("frr domain missing from the registry"));

        editor->reload();
        if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 20000))
            return fail(out, QStringLiteral("frr.conf read timed out"));
        if (!editor->errorText().isEmpty())
            return fail(out, QStringLiteral("frr.conf read failed: %1").arg(editor->errorText()));

        // The router's frr.conf carries router ospf, router bgp and interface
        // blocks; the editor must enumerate them from the file.
        const QAbstractItemModel *sections = editor->sections();
        out << "frr.conf: " << sections->rowCount() << " block(s) in " << editor->sourceLabel()
            << Qt::endl;
        if (sections->rowCount() < 3)
            return fail(out, QStringLiteral("expected router/interface blocks in frr.conf"));

        int ospfBlock = -1;
        int ospfRow = -1;
        for (int row = 0; row < sections->rowCount(); ++row) {
            const QString type = cell(sections, row, "type");
            out << "  block " << cell(sections, row, "detail") << "  [" << type << "]  "
                << cell(sections, row, "summary") << Qt::endl;
            if (type == QLatin1String("router ospf") && ospfRow < 0) {
                ospfRow = row;
                ospfBlock = row;
            }
        }
        if (ospfRow < 0)
            return fail(out, QStringLiteral("no `router ospf` block in frr.conf"));

        editor->selectSection(sections->index(ospfRow, 0).data(Qt::UserRole + 1).toString());
        const QString baseline = editor->value(QStringLiteral("ospf router-id"));
        if (baseline.isEmpty())
            return fail(out, QStringLiteral("router ospf block has no ospf router-id row"));
        out << "baseline ospf router-id " << baseline << Qt::endl;

        // Stage: the diff list must name the block, the statement, old and new.
        const QString staged = baseline.endsWith(QLatin1Char('9'))
                                   ? baseline.chopped(1) + QStringLiteral("99")
                                   : baseline + QStringLiteral("9");
        editor->stageValue(QStringLiteral("ospf router-id"), staged);
        if (editor->pendingCount() != 1)
            return fail(out, QStringLiteral("staged router-id change did not appear in the diff"));
        const QAbstractItemModel *pendingModel = editor->pendingChanges();
        const QString text = pendingModel->data(pendingModel->index(0, 0), Qt::UserRole + 6).toString();
        out << "staged diff: " << text << Qt::endl;
        if (!text.contains(staged) || !text.contains(baseline))
            return fail(out, QStringLiteral("diff row does not carry old/new values"));

        // Revert before apply: local discard, the router file must not change.
        editor->revert();
        if (!waitFor([&] { return editor->pendingCount() == 0; }, 5000))
            return fail(out, QStringLiteral("revert did not clear the staged diff"));

        // Apply for real: write + frr restart + daemon probe.
        editor->stageValue(QStringLiteral("ospf router-id"), staged);
        editor->apply();
        if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 90000))
            return fail(out, QStringLiteral("apply timed out: %1").arg(editor->statusText()));
        if (!editor->errorText().isEmpty())
            return fail(out, QStringLiteral("apply failed: %1").arg(editor->errorText()));
        if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 20000))
            return fail(out, QStringLiteral("post-apply reload timed out"));
        if (editor->value(QStringLiteral("ospf router-id")) != staged)
            return fail(out, QStringLiteral("router did not keep the applied router-id (%1)")
                                 .arg(editor->value(QStringLiteral("ospf router-id"))));
        out << "applied: " << editor->statusText() << Qt::endl;

        // Revert after apply: restore /etc/frr/frr.conf.bak and restart again.
        editor->revert();
        if (!waitFor([&] { return !editor->isBusy() && editor->isLoaded(); }, 90000))
            return fail(out, QStringLiteral("backup restore timed out: %1").arg(editor->statusText()));
        if (editor->value(QStringLiteral("ospf router-id")) != baseline)
            return fail(out, QStringLiteral("backup restore did not bring back %1 (got %2)")
                                 .arg(baseline, editor->value(QStringLiteral("ospf router-id"))));
        out << "restored: " << editor->statusText() << Qt::endl;
        out << "PASS config" << Qt::endl;
        return 0;
    }

    return fail(out, QStringLiteral("unknown mode %1").arg(mode));
}
