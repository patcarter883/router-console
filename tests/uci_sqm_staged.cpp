// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <functional>

#include "AppEngine.h"
#include "config/ConfigEditor.h"
#include "config/ConfigModels.h"
#include "protocol/UbusClient.h"

// Milestone 4 star check: edit the SQM download bandwidth from the client's
// staged editor, prove the changes list holds exactly that change, apply, reboot
// the VM (vm-boot label), reconnect, prove the value persisted, then stage and
// revert and prove the config is unchanged.
//
// Exit 0 pass, 1 fail, 3 dev VM not reachable (test skipped).
namespace {

QTextStream out(stdout);

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return true;
}

// Independent ubus connection: every assertion about router state is read here,
// not from the editor that made the change.
class Probe
{
public:
    bool start(const QString &host, const QString &user, const QString &password,
               const QByteArray &pin)
    {
        m_client = new UbusClient;
        m_client->moveToThread(&m_thread);
        QObject::connect(&m_thread, &QThread::finished, m_client, &QObject::deleteLater);
        m_thread.start();

        const QUrl endpoint(QStringLiteral("https://%1/ubus").arg(host));
        bool done = false;
        bool ok = false;
        auto connected = QObject::connect(m_client, &UbusClient::loggedIn, m_client,
                                         [&done, &ok](const Ubus::Session &) {
                                             done = true;
                                             ok = true;
                                         });
        auto failed = QObject::connect(m_client, &UbusClient::loginFailed, m_client,
                                       [&done](const Ubus::Error &) { done = true; });
        QMetaObject::invokeMethod(
            m_client,
            [this, endpoint, pin, user, password] {
                m_client->setPinnedFingerprint(pin);
                m_client->login(endpoint, user, password);
            },
            Qt::QueuedConnection);
        waitFor([&done] { return done; }, 12000);
        QObject::disconnect(connected);
        QObject::disconnect(failed);
        return ok;
    }

    void stop()
    {
        if (!m_client)
            return;
        m_thread.quit();
        m_thread.wait();
        m_client = nullptr;
    }

    QString option(const QString &config, const QString &section, const QString &name)
    {
        QString value;
        bool done = false;
        auto received = QObject::connect(
            m_client, &UbusClient::uciOptionReceived, m_client,
            [&value, &done](const QString &, const QString &, const QString &, const QString &v) {
                value = v;
                done = true;
            });
        auto failed = QObject::connect(m_client, &UbusClient::uciOptionFailed, m_client,
                                      [&done](const Ubus::Error &) { done = true; });
        QMetaObject::invokeMethod(m_client, "uciGetOption", Qt::QueuedConnection,
                                  Q_ARG(QString, config), Q_ARG(QString, section),
                                  Q_ARG(QString, name));
        waitFor([&done] { return done; }, 8000);
        QObject::disconnect(received);
        QObject::disconnect(failed);
        return value;
    }

    QStringList changes(const QString &config)
    {
        QStringList lines;
        bool done = false;
        auto received = QObject::connect(
            m_client, &UbusClient::uciChangesReceived, m_client,
            [&lines, &done](const Ubus::UciChangeSet &set) {
                for (const Ubus::UciChange &change : set.changes)
                    lines.append(change.describe());
                done = true;
            });
        auto failed = QObject::connect(m_client, &UbusClient::uciChangesFailed, m_client,
                                      [&done](const Ubus::Error &) { done = true; });
        QMetaObject::invokeMethod(m_client, "uciChanges", Qt::QueuedConnection,
                                  Q_ARG(QString, config));
        waitFor([&done] { return done; }, 8000);
        QObject::disconnect(received);
        QObject::disconnect(failed);
        return lines;
    }

    bool set(const QString &config, const QString &section, const QString &name,
             const QString &value)
    {
        bool done = false;
        bool ok = false;
        auto staged = QObject::connect(m_client, &UbusClient::uciStaged, m_client,
                                      [&done, &ok](const Ubus::UciSetRequest &) {
                                          done = true;
                                          ok = true;
                                      });
        auto failed = QObject::connect(m_client, &UbusClient::uciStageFailed, m_client,
                                      [&done](const Ubus::Error &) { done = true; });
        const Ubus::UciSetRequest request{config, section, name, value};
        QMetaObject::invokeMethod(m_client, "uciSet", Qt::QueuedConnection,
                                  Q_ARG(Ubus::UciSetRequest, request));
        waitFor([&done] { return done; }, 8000);
        QObject::disconnect(staged);
        QObject::disconnect(failed);
        return ok;
    }

    bool revert(const QString &config)
    {
        bool done = false;
        bool ok = false;
        auto reverted = QObject::connect(m_client, &UbusClient::uciReverted, m_client,
                                        [&done, &ok](const QString &) {
                                            done = true;
                                            ok = true;
                                        });
        auto failed = QObject::connect(m_client, &UbusClient::uciRevertFailed, m_client,
                                      [&done](const Ubus::Error &) { done = true; });
        QMetaObject::invokeMethod(m_client, "uciRevert", Qt::QueuedConnection,
                                  Q_ARG(QString, config));
        waitFor([&done] { return done; }, 8000);
        QObject::disconnect(reverted);
        QObject::disconnect(failed);
        return ok;
    }

private:
    QThread m_thread;
    UbusClient *m_client = nullptr;
};

int fail(const QString &step, const QString &detail)
{
    out << "FAIL " << step << ": " << detail << Qt::endl;
    out.flush();
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");
    qRegisterMetaType<Ubus::UciSetRequest>();

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOption("host", "host[:port]", "host", "127.0.0.1:10443");
    const QCommandLineOption userOption("user", "username", "user", "root");
    const QCommandLineOption passwordOption("password", "password", "password", "root");
    const QCommandLineOption trustOption("accept-new-cert", "accept an unseen certificate");
    const QCommandLineOption domainOption("domain", "config domain id", "id", "sqm");
    const QCommandLineOption sectionOption("section", "uci section", "name", "eth1");
    const QCommandLineOption fieldOption("option", "uci option to edit", "name", "download");
    const QCommandLineOption stepOption("step", "value step applied to the committed number", "n",
                                        "2000");
    const QCommandLineOption rebootOption("reboot", "reboot the VM between apply and verify");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, domainOption,
                       sectionOption, fieldOption, stepOption, rebootOption});
    parser.process(app);

    const QString hostArg = parser.value(hostOption);
    const QString user = parser.value(userOption);
    const QString password = parser.value(passwordOption);
    const QString domain = parser.value(domainOption);
    const QString section = parser.value(sectionOption);
    const QString option = parser.value(fieldOption);
    const int step = parser.value(stepOption).toInt();

    AppEngine engine;
    bool trusted = false;
    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            out << "certificate: trusting " << engine.trustFingerprint() << Qt::endl;
            engine.trustCertificate();
        }
    });

    out << "[1] connect " << hostArg << Qt::endl;
    engine.connectTo(hostArg, QString(), user, password);
    if (!waitFor([&] { return engine.isConnected(); }, 15000)) {
        out << "SKIP: dev VM not reachable at " << hostArg << ": " << engine.errorText()
            << Qt::endl;
        out.flush();
        return 3;
    }
    out << "    CONNECTED " << engine.hostname() << " — " << engine.firmware() << Qt::endl;

    QString endpoint = hostArg;
    if (!endpoint.contains(QLatin1Char(':')))
        endpoint.append(QStringLiteral(":443"));
    QSettings settings;
    const QByteArray pin = settings.value(QStringLiteral("certificates/%1").arg(endpoint))
                               .toByteArray();

    auto *editor = qobject_cast<ConfigEditor *>(engine.configEditor(domain));
    if (!editor)
        return fail("editor", QStringLiteral("no editor for domain %1").arg(domain));

    out << "[2] read " << domain << Qt::endl;
    editor->reload();
    if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 10000))
        return fail("read", editor->errorText().isEmpty() ? editor->statusText()
                                                          : editor->errorText());
    editor->selectSection(section);
    const QString original = editor->value(option);
    if (original.isEmpty())
        return fail("read", QStringLiteral("%1.%2 is empty").arg(section, option));
    out << "    " << section << "." << option << " = " << original << "  (sections: "
        << editor->sections()->rowCount() << ")" << Qt::endl;

    const QString target = QString::number(original.toInt() + step);
    out << "[3] stage " << section << "." << option << " = " << target << Qt::endl;
    editor->stageValue(option, target);

    QAbstractItemModel *pending = editor->pendingChanges();
    const QString expectedText =
        QStringLiteral("%1.%2  %3 → %4").arg(section, option, original, target);
    if (editor->pendingCount() != 1 || pending->rowCount() != 1)
        return fail("pending list", QStringLiteral("expected exactly 1 change, got %1")
                                        .arg(pending->rowCount()));
    const QString pendingText = pending->index(0, 0).data(PendingChangesModel::TextRole).toString();
    if (pendingText != expectedText)
        return fail("pending list", QStringLiteral("row is \"%1\", expected \"%2\"")
                                        .arg(pendingText, expectedText));
    out << "    Pending changes (" << editor->pendingCount() << ")" << Qt::endl
        << "      •  " << pendingText << Qt::endl;

    out << "[4] apply" << Qt::endl;
    bool applied = false;
    QObject::connect(editor, &ConfigEditor::applied, &engine, [&applied](int) { applied = true; });
    editor->apply();
    if (!waitFor([&] { return applied || !editor->errorText().isEmpty(); }, 30000))
        return fail("apply", QStringLiteral("timed out"));
    if (!applied)
        return fail("apply", editor->errorText());
    if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 10000))
        return fail("apply", QStringLiteral("pending list not cleared"));
    out << "    applied; pending list now (" << editor->pendingCount() << "), committed "
        << section << "." << option << " = " << editor->value(option) << Qt::endl;

    Probe probe;
    if (!probe.start(endpoint, user, password, pin))
        return fail("probe", QStringLiteral("independent ubus login failed"));
    const QStringList afterApply = probe.changes(domain);
    if (!afterApply.isEmpty())
        return fail("apply", QStringLiteral("router still stages: %1")
                                .arg(afterApply.join(QStringLiteral(", "))));
    const QString routerValue = probe.option(domain, section, option);
    if (routerValue != target)
        return fail("apply", QStringLiteral("router reads %1, expected %2")
                                .arg(routerValue, target));
    out << "    router: " << section << "." << option << " = " << routerValue
        << ", uci changes empty" << Qt::endl;

    QString persisted = routerValue;
    if (parser.isSet(rebootOption)) {
        out << "[5] reboot the VM (vm-boot)" << Qt::endl;
        probe.stop();
        bool rebooted = false;
        QObject::connect(&engine, &AppEngine::rebootRequested, &engine,
                         [&rebooted] { rebooted = true; });
        engine.rebootRouter();
        if (!waitFor([&] { return rebooted; }, 10000))
            return fail("reboot", QStringLiteral("system.reboot was not acknowledged"));
        out << "    reboot acknowledged, waiting for the VM to come back" << Qt::endl;

        QElapsedTimer timer;
        timer.start();
        bool back = false;
        while (timer.elapsed() < 240000) {
            QThread::msleep(4000);
            engine.connectTo(hostArg, QString(), user, password);
            if (waitFor([&] { return engine.isConnected(); }, 15000)) {
                back = true;
                break;
            }
        }
        if (!back)
            return fail("reboot", QStringLiteral("VM did not come back in 240 s"));
        out << "    reconnected to " << engine.hostname() << " after "
            << (timer.elapsed() / 1000) << " s" << Qt::endl;

        if (!probe.start(endpoint, user, password, pin))
            return fail("probe", QStringLiteral("independent ubus login failed after reboot"));
        persisted = probe.option(domain, section, option);
        if (persisted != target)
            return fail("persistence", QStringLiteral("after reboot %1.%2 = %3, expected %4")
                                           .arg(section, option, persisted, target));
        out << "[6] persisted after reboot: " << section << "." << option << " = " << persisted
            << "  PASS" << Qt::endl;
    }

    out << "[7] stage " << section << "." << option << " = " << (persisted.toInt() + step)
        << " then revert" << Qt::endl;
    editor->reload();
    waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 10000);
    editor->selectSection(section);
    editor->stageValue(option, QString::number(persisted.toInt() + step));
    if (editor->pendingCount() != 1)
        return fail("revert", QStringLiteral("expected 1 pending change, got %1")
                                  .arg(editor->pendingCount()));
    out << "    pending: " << editor->pendingChanges()
                                 ->index(0, 0)
                                 .data(PendingChangesModel::TextRole)
                                 .toString()
        << Qt::endl;

    // Stage the same option on the router too, so revert has something real to
    // discard: rpcd keeps the staged delta per ubus session, so this uses the
    // probe's own session and the probe's own revert.
    const QString routerStaged = QString::number(persisted.toInt() + step * 2);
    if (!probe.set(domain, section, option, routerStaged))
        return fail("revert", QStringLiteral("could not stage a router-side change"));
    const QStringList stagedBeforeRevert = probe.changes(domain);
    if (stagedBeforeRevert.size() != 1)
        return fail("revert", QStringLiteral("router-side staging missing: %1")
                                  .arg(stagedBeforeRevert.join(QStringLiteral(", "))));
    out << "    router session staged: " << stagedBeforeRevert.join(QStringLiteral(", "))
        << Qt::endl;
    if (!probe.revert(domain))
        return fail("revert", QStringLiteral("uci revert failed"));
    const QString afterRouterRevert = probe.option(domain, section, option);
    if (!probe.changes(domain).isEmpty() || afterRouterRevert != persisted)
        return fail("revert", QStringLiteral("uci revert left %1 / %2")
                                  .arg(afterRouterRevert, probe.changes(domain).join(
                                           QStringLiteral(", "))));
    out << "    uci revert discarded it: " << section << "." << option << " = "
        << afterRouterRevert << ", changes empty" << Qt::endl;

    bool reverted = false;
    QObject::connect(editor, &ConfigEditor::reverted, &engine, [&reverted] { reverted = true; });
    editor->revert();
    if (!waitFor([&] { return reverted || !editor->errorText().isEmpty(); }, 15000))
        return fail("revert", QStringLiteral("timed out"));
    if (!reverted)
        return fail("revert", editor->errorText());
    if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 10000))
        return fail("revert", QStringLiteral("pending list not cleared"));

    const QString afterRevert = probe.option(domain, section, option);
    const QStringList changesAfterRevert = probe.changes(domain);
    if (afterRevert != persisted)
        return fail("revert", QStringLiteral("value changed to %1").arg(afterRevert));
    if (!changesAfterRevert.isEmpty())
        return fail("revert", QStringLiteral("router still stages: %1")
                                .arg(changesAfterRevert.join(QStringLiteral(", "))));
    out << "    after revert: pending (" << editor->pendingCount() << "), " << section << "."
        << option << " = " << afterRevert << ", uci changes empty  PASS" << Qt::endl;

    // Leave the lab VM as found: the original value goes back through the same
    // staged path, so repeated runs start from the same baseline.
    editor->stageValue(option, original);
    bool restored = false;
    QObject::connect(editor, &ConfigEditor::applied, &engine, [&restored](int) { restored = true; });
    editor->apply();
    if (!waitFor([&] { return restored || !editor->errorText().isEmpty(); }, 30000))
        return fail("restore", QStringLiteral("timed out"));
    if (!restored)
        return fail("restore", editor->errorText());
    if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 10000))
        return fail("restore", QStringLiteral("pending list not cleared"));
    out << "[8] restored " << section << "." << option << " = " << editor->value(option)
        << " (lab baseline)" << Qt::endl;

    probe.stop();
    out << "PASS staged sqm edit: " << original << " -> " << target
        << (parser.isSet(rebootOption) ? ", persisted across reboot" : "")
        << ", revert clean" << Qt::endl;
    out.flush();
    return 0;
}
