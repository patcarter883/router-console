// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QThread>

#include <functional>

#include "AppEngine.h"
#include "config/ConfigEditor.h"
#include "config/ConfigModels.h"
#include "config/DomainRegistry.h"
#include "protocol/UbusClient.h"
#include "protocol/UciCodec.h"
#include "vm_probe.h"

// Star check for the firewall domain's second half: firewall.rule filter rules
// and firewall.redirect port-forward NAT. The VM has nine rules and no
// redirect, so the test stages one redirect section (uci add, never applied),
// stages one change on a real rule and one on that redirect, asserts the exact
// pending list, then reverts and proves the router is back at its baseline.
//
// Exit 0 pass, 1 fail, 3 dev VM not reachable (test skipped).
namespace {

QTextStream out(stdout);
int failures = 0;

bool check(bool condition, const QString &description)
{
    out << (condition ? "  ok   " : "  FAIL ") << description << Qt::endl;
    if (!condition)
        ++failures;
    return condition;
}

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

// Independent ubus connection: router state is read here, never from the
// editor that staged the change.
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

    Ubus::UciConfig config(const QString &name, const QString &type)
    {
        Ubus::UciConfig result;
        bool done = false;
        auto received = QObject::connect(
            m_client, &UbusClient::uciConfigReceived, m_client,
            [&result, &done](const Ubus::UciConfig &config) {
                result = config;
                done = true;
            });
        auto failed = QObject::connect(m_client, &UbusClient::uciConfigFailed, m_client,
                                       [&done](const Ubus::Error &) { done = true; });
        QMetaObject::invokeMethod(m_client, "uciGetConfig", Qt::QueuedConnection,
                                  Q_ARG(QString, name), Q_ARG(QString, type));
        waitFor([&done] { return done; }, 10000);
        QObject::disconnect(received);
        QObject::disconnect(failed);
        return result;
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


private:
    QThread m_thread;
    UbusClient *m_client = nullptr;
};

const Ubus::UciSection *findByName(const Ubus::UciConfig &config, const QString &option,
                                   const QString &value)
{
    for (const Ubus::UciSection &section : config.sections) {
        if (section.option(option) == value)
            return &section;
    }
    return nullptr;
}

// The editor and the test must share one rpcd session: rpcd keeps each
// session's staged UCI delta separate, so an add made from another session is
// invisible to the editor. These helpers drive the engine's own client.
QString addRedirectOn(UbusClient *client, const QString &name, const QString &externalPort,
                      const QString &internalAddress, const QString &internalPort)
{
    const QJsonObject values{{QStringLiteral("name"), name},
                             {QStringLiteral("src"), QStringLiteral("wan")},
                             {QStringLiteral("dest"), QStringLiteral("lan")},
                             {QStringLiteral("proto"), QStringLiteral("tcp")},
                             {QStringLiteral("src_dport"), externalPort},
                             {QStringLiteral("dest_ip"), internalAddress},
                             {QStringLiteral("dest_port"), internalPort},
                             {QStringLiteral("target"), QStringLiteral("DNAT")}};
    const QJsonObject args{{QStringLiteral("config"), QStringLiteral("firewall")},
                           {QStringLiteral("type"), QStringLiteral("redirect")},
                           {QStringLiteral("values"), values}};
    bool done = false;
    QString section;
    auto succeeded = QObject::connect(
        client, &UbusClient::appCallSucceeded, client,
        [&done, &section](const QString &, const QJsonObject &result) {
            section = result.value(QStringLiteral("section")).toString();
            done = true;
        });
    auto failed = QObject::connect(client, &UbusClient::appCallFailed, client,
                                   [&done](const QString &, const Ubus::Error &) { done = true; });
    QMetaObject::invokeMethod(
        client,
        [client, args] { client->callApp(QStringLiteral("m11-add"), QStringLiteral("uci"),
                                         QStringLiteral("add"), args); },
        Qt::QueuedConnection);
    waitFor([&done] { return done; }, 8000);
    QObject::disconnect(succeeded);
    QObject::disconnect(failed);
    return section;
}

QStringList changesOn(UbusClient *client, const QString &config)
{
    QStringList lines;
    bool done = false;
    auto received = QObject::connect(
        client, &UbusClient::uciChangesReceived, client,
        [&lines, &done](const Ubus::UciChangeSet &set) {
            for (const Ubus::UciChange &change : set.changes)
                lines.append(change.describe());
            done = true;
        });
    auto failed = QObject::connect(client, &UbusClient::uciChangesFailed, client,
                                   [&done](const Ubus::Error &) { done = true; });
    QMetaObject::invokeMethod(client, "uciChanges", Qt::QueuedConnection, Q_ARG(QString, config));
    waitFor([&done] { return done; }, 8000);
    QObject::disconnect(received);
    QObject::disconnect(failed);
    return lines;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("open-broadcast"));
    QCoreApplication::setApplicationName(QStringLiteral("router-console-firewall-nat-test"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOption({QStringLiteral("host")}, QStringLiteral("host:port"),
                                        QStringLiteral("host"),
                                        QStringLiteral("127.0.0.1:10443"));
    const QCommandLineOption userOption({QStringLiteral("user")}, QStringLiteral("username"),
                                        QStringLiteral("user"), QStringLiteral("root"));
    const QCommandLineOption passwordOption({QStringLiteral("password")},
                                            QStringLiteral("password"), QStringLiteral("password"),
                                            QStringLiteral("root"));
    const QCommandLineOption pinOption(QStringLiteral("accept-new-cert"),
                                       QStringLiteral("accept an unseen certificate"));
    parser.addOptions({hostOption, userOption, passwordOption, pinOption});
    parser.process(app);

    const QString endpoint = parser.value(hostOption);
    if (int rc = skipWithoutDevVm(out, endpoint))
        return rc;

    // A list-valued option must reach rpcd as an array, not as one string with
    // a space in it (uci would store a single element). No router needed.
    {
        const Ubus::UciSetRequest listRequest{QStringLiteral("firewall"), QStringLiteral("cfg"),
                                              QStringLiteral("icmp_type"),
                                              QStringLiteral("130/0 131/0"), true};
        const QJsonArray values =
            Ubus::UciCodec::setArgs(listRequest).value(QStringLiteral("values"))
                .toObject()
                .value(QStringLiteral("icmp_type"))
                .toArray();
        check(values.size() == 2 && values.at(0).toString() == QLatin1String("130/0")
                  && values.at(1).toString() == QLatin1String("131/0"),
              QStringLiteral("list option staged as a UCI array"));
    }

    AppEngine engine;
    auto *editor = qobject_cast<ConfigEditor *>(engine.configEditor(QStringLiteral("firewall")));
    if (!editor) {
        out << "FAILED: no firewall domain in the registry" << Qt::endl;
        return 1;
    }

    bool connected = false;
    bool trusted = false;
    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(pinOption) && !trusted) {
            trusted = true;
            engine.trustCertificate();
        }
        if (engine.isConnected())
            connected = true;
    });
    engine.connectTo(endpoint, QString(), parser.value(userOption), parser.value(passwordOption));
    if (!waitFor([&] { return connected; }, 15000)) {
        out << "SKIP: dev VM not reachable at " << endpoint << ": " << engine.errorText()
            << Qt::endl;
        out.flush();
        return 3;
    }

    QString pinned = endpoint;
    if (!pinned.contains(QLatin1Char(':')))
        pinned.append(QStringLiteral(":443"));
    const QByteArray pin =
        QSettings().value(QStringLiteral("certificates/%1").arg(pinned)).toByteArray();

    Probe probe;
    if (!probe.start(endpoint, parser.value(userOption), parser.value(passwordOption), pin)) {
        out << "FAILED: probe could not log in" << Qt::endl;
        out.flush();
        return 1;
    }

    // A redirect section to edit: the VM's real firewall has none, so stage one
    // the same way the redirect editor would (uci add, not commit).
    const QString redirectName = QStringLiteral("M11 NAT proof");
    const QString redirectSection =
        addRedirectOn(engine.client(), redirectName, QStringLiteral("8080"),
                      QStringLiteral("192.168.77.10"), QStringLiteral("80"));
    if (!check(!redirectSection.isEmpty(), QStringLiteral("staged redirect section")) ||
        !check(changesOn(engine.client(), QStringLiteral("firewall")).size() > 0,
               QStringLiteral("firewall holds the staged redirect"))) {
        probe.stop();
        return 1;
    }
    out << "    redirect section: " << redirectSection << Qt::endl;

    editor->reload();
    if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 12000)) {
        out << "  FAIL read: " << editor->errorText() << Qt::endl;
        probe.stop();
        return 1;
    }

    auto *sections = editor->sections();
    int ruleRow = -1;
    int redirectRow = -1;
    QStringList sectionRows;
    for (int row = 0; row < sections->rowCount(); ++row) {
        const QModelIndex index = sections->index(row, 0);
        const QString type = sections->data(index, ConfigSectionsModel::TypeRole).toString();
        const QString name = sections->data(index, ConfigSectionsModel::NameRole).toString();
        sectionRows << type + QLatin1Char(':') + name;
        if (type == QLatin1String("rule") && ruleRow < 0)
            ruleRow = row;
        if (type == QLatin1String("redirect") && name == redirectSection)
            redirectRow = row;
    }
    out << "    editor sections: " << sectionRows.join(QStringLiteral(", ")) << Qt::endl;
    if (!check(ruleRow >= 0, QStringLiteral("rule sections visible")) ||
        !check(redirectRow >= 0, QStringLiteral("redirect sections visible"))) {
        probe.stop();
        return 1;
    }

    // One rule change on the real VM config.
    const Ubus::UciConfig ruleConfig =
        probe.config(QStringLiteral("firewall"), QStringLiteral("rule"));
    const Ubus::UciSection *rule =
        findByName(ruleConfig, QStringLiteral("name"), QStringLiteral("Allow-Ping"));
    if (!check(rule != nullptr, QStringLiteral("Allow-Ping rule present"))) {
        probe.stop();
        return 1;
    }
    editor->selectSection(rule->name);
    editor->stageValue(QStringLiteral("target"), QStringLiteral("DROP"));
    if (!check(editor->pendingCount() == 1, QStringLiteral("rule change staged"))) {
        probe.stop();
        return 1;
    }
    check(editor->value(QStringLiteral("target")) == QLatin1String("DROP"),
          QStringLiteral("rule target staged to DROP (was %1)")
              .arg(rule->option(QStringLiteral("target"))));

    // One redirect change on the staged section.
    editor->selectSection(redirectSection);
    editor->stageValue(QStringLiteral("dest_port"), QStringLiteral("8081"));
    if (!check(editor->pendingCount() == 2, QStringLiteral("redirect change staged"))) {
        probe.stop();
        return 1;
    }
    check(editor->value(QStringLiteral("dest_port")) == QLatin1String("8081"),
          QStringLiteral("redirect dest_port staged to 8081"));

    auto *pending = qobject_cast<PendingChangesModel *>(editor->pendingChanges());
    QStringList descriptions;
    for (int row = 0; row < pending->rowCount(); ++row) {
        descriptions << pending->data(pending->index(row, 0), PendingChangesModel::TextRole)
                            .toString();
    }
    out << "    pending: " << descriptions.join(QStringLiteral(" | ")) << Qt::endl;
    check(descriptions.size() == 2, QStringLiteral("changes list holds exactly two changes"));
    check(descriptions.filter(QStringLiteral("target")).size() == 1,
          QStringLiteral("changes list names the rule change"));
    check(descriptions.filter(QStringLiteral("dest_port")).size() == 1,
          QStringLiteral("changes list names the redirect change"));

    // Nothing was applied: the probe's own view of the package is still the
    // baseline plus the staged add.
    check(probe.option(QStringLiteral("firewall"), rule->name, QStringLiteral("target"))
              == QLatin1String("ACCEPT"),
          QStringLiteral("router target still ACCEPT before revert"));

    bool reverted = false;
    QObject::connect(editor, &ConfigEditor::reverted, editor, [&reverted] { reverted = true; });
    editor->revert();
    if (!waitFor([&] { return reverted || !editor->errorText().isEmpty(); }, 15000)) {
        out << "  FAIL revert: timed out" << Qt::endl;
        probe.stop();
        return 1;
    }
    if (!check(reverted, QStringLiteral("editor reverted"))) {
        probe.stop();
        return 1;
    }
    check(editor->pendingCount() == 0, QStringLiteral("pending list cleared"));
    check(changesOn(engine.client(), QStringLiteral("firewall")).isEmpty(),
          QStringLiteral("router uci changes empty after revert"));
    check(probe.option(QStringLiteral("firewall"), rule->name, QStringLiteral("target"))
              == QLatin1String("ACCEPT"),
          QStringLiteral("router target restored to ACCEPT"));
    // The revert also discarded the staged redirect section: the editor's next
    // read shows zones and rules only.
    if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 12000)) {
        out << "  FAIL reload after revert" << Qt::endl;
        probe.stop();
        return 1;
    }
    bool redirectGone = true;
    for (int row = 0; row < editor->sections()->rowCount(); ++row) {
        const QModelIndex index = editor->sections()->index(row, 0);
        if (editor->sections()->data(index, ConfigSectionsModel::TypeRole).toString()
            == QLatin1String("redirect"))
            redirectGone = false;
    }
    check(redirectGone, QStringLiteral("staged redirect discarded by revert"));

    probe.stop();
    out << Qt::endl << (failures == 0 ? "PASS" : "FAIL") << " firewall rules + NAT ("
        << failures << " failing checks)" << Qt::endl;
    out.flush();
    return failures == 0 ? 0 : 1;
}
