// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTextStream>
#include <QThread>

#include <functional>

#include "AppEngine.h"
#include "config/ConfigEditor.h"
#include "config/ConfigModels.h"
#include "config/DomainRegistry.h"
#include "protocol/UbusClient.h"
#include "vm_probe.h"

// Star check for the core config domains: for each registry domain (network,
// wireless, firewall, dhcp) read the package through the client's staged editor,
// prove every registry field names an option the router actually stores, stage
// one reversible change and prove the staged-changes list holds exactly it.
// Network and firewall stop at staging + revert (an apply would rewrite the VM's
// live addressing / filtering); dhcp applies and restores its baseline.
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

struct Case {
    QString domain;
    QString section;    // empty when the section is found by selectOption
    QString option;
    QString value;      // staged value, reversible against the baseline
    int sections = 0;     // sections the package holds on the VM
    bool applies = false; // apply + restore vs stage + revert only
    bool absentPackage = false; // the router has no such UCI package at all
    // Anonymous sections (firewall zones) are found by one of their options
    // rather than by their generated name.
    QString selectOption;
    QString selectValue;
};

const QList<Case> &cases()
{
    static const QList<Case> list{
        {QStringLiteral("network"), QStringLiteral("lan"), QStringLiteral("ip6assign"),
         QStringLiteral("59"), 5, false, false, {}, {}},
        {QStringLiteral("wireless"), QString(), QString(), QString(), 0, false, true, {}, {}},
        {QStringLiteral("firewall"), QString(), QStringLiteral("mtu_fix"), QStringLiteral("0"), 2,
         false, false, QStringLiteral("name"), QStringLiteral("wan")},
        {QStringLiteral("dhcp"), QStringLiteral("lan"), QStringLiteral("leasetime"),
         QStringLiteral("24h"), 2, true, false, {}, {}},
    };
    return list;
}

const Case *findCase(const QString &domain)
{
    for (const Case &entry : cases()) {
        if (entry.domain == domain)
            return &entry;
    }
    return nullptr;
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

    // Whole package as UCI stores it, or an empty config with `absent` set when
    // uci answers "not found" (the package does not exist on this router).
    Ubus::UciConfig config(const QString &name, const QString &type, bool *absent = nullptr)
    {
        Ubus::UciConfig result;
        bool done = false;
        if (absent)
            *absent = false;
        auto received = QObject::connect(
            m_client, &UbusClient::uciConfigReceived, m_client,
            [&result, &done](const Ubus::UciConfig &config) {
                result = config;
                done = true;
            });
        auto failed = QObject::connect(m_client, &UbusClient::uciConfigFailed, m_client,
                                       [&done, &absent](const Ubus::Error &error) {
                                           if (absent && error.status == Ubus::Status::NotFound)
                                               *absent = true;
                                           done = true;
                                       });
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

int fail(const QString &domain, const QString &step, const QString &detail)
{
    out << "FAIL " << domain << " " << step << ": " << detail << Qt::endl;
    out.flush();
    return 1;
}

// Stage through the editor, then assert the staged-changes list holds exactly
// this one change with that text.
int checkStaged(ConfigEditor *editor, const Case &entry, const QString &section,
                const QString &original, const QString &target)
{
    editor->stageValue(entry.option, target);
    const QString expected =
        QStringLiteral("%1.%2  %3 → %4").arg(section, entry.option, original, target);
    if (editor->pendingCount() != 1 || editor->pendingChanges()->rowCount() != 1)
        return fail(entry.domain, "staged list",
                    QStringLiteral("expected exactly 1 change, got %1")
                        .arg(editor->pendingChanges()->rowCount()));
    const QString text =
        editor->pendingChanges()->index(0, 0).data(PendingChangesModel::TextRole).toString();
    if (text != expected)
        return fail(entry.domain, "staged list",
                    QStringLiteral("row is \"%1\", expected \"%2\"").arg(text, expected));
    out << "      •  " << text << Qt::endl;
    return 0;
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
    const QCommandLineOption domainOption("domain", "comma-separated domain ids", "ids",
                                          "network,wireless,firewall,dhcp");
    parser.addOptions({hostOption, userOption, passwordOption, trustOption, domainOption});
    parser.process(app);

    const QString hostArg = parser.value(hostOption);
    const QString user = parser.value(userOption);
    const QString password = parser.value(passwordOption);
    const QStringList wanted = parser.value(domainOption).split(QLatin1Char(','),
                                                                Qt::SkipEmptyParts);

    for (const QString &id : wanted) {
        if (!findCase(id)) {
            out << "FAIL registry: no test case for domain " << id << Qt::endl;
            return 1;
        }
        if (!Config::domain(id)) {
            out << "FAIL registry: domain " << id << " is not in the registry" << Qt::endl;
            return 1;
        }
    }

    AppEngine engine;
    bool trusted = false;
    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            out << "certificate: trusting " << engine.trustFingerprint() << Qt::endl;
            engine.trustCertificate();
        }
    });

    if (const int skip = skipWithoutDevVm(out, hostArg))
        return skip;

    out << "[0] connect " << hostArg << Qt::endl;
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
    const QByteArray pin = QSettings()
                               .value(QStringLiteral("certificates/%1").arg(endpoint))
                               .toByteArray();

    Probe probe;
    if (!probe.start(endpoint, user, password, pin)) {
        out << "FAIL probe: independent ubus login failed" << Qt::endl;
        return 1;
    }

    for (const QString &id : wanted) {
        const Case &entry = *findCase(id);
        const Config::DomainSpec *spec = Config::domain(id);
        out << Qt::endl << "[" << id << "] " << spec->title << " — " << spec->config << "."
            << spec->sectionType << Qt::endl;

        auto *editor = qobject_cast<ConfigEditor *>(engine.configEditor(id));
        if (!editor) {
            probe.stop();
            return fail(id, "editor", QStringLiteral("no editor for domain %1").arg(id));
        }

        // Registry fidelity: the package, read independently, holds the sections
        // the registry expects, and every field names an option the router
        // stores in one of them.
        bool absent = false;
        const Ubus::UciConfig stored = probe.config(spec->config, spec->sectionType, &absent);
        out << "    router: " << stored.sections.size() << " section(s)"
            << (absent ? "  (package absent: uci answers not found)" : QString()) << Qt::endl;
        if (stored.sections.size() != entry.sections) {
            probe.stop();
            return fail(id, "read", QStringLiteral("registry expects %1 section(s), router holds %2")
                                       .arg(entry.sections)
                                       .arg(stored.sections.size()));
        }

        int checkedFields = 0;
        for (const Config::FieldSpec &field : spec->fields) {
            bool present = false;
            for (const Ubus::UciSection &section : stored.sections)
                present = present || section.hasOption(field.option);
            if (!present && !absent) {
                probe.stop();
                return fail(id, "schema", QStringLiteral("option '%1' is not in any %2 section")
                                          .arg(field.option, spec->sectionType));
            }
            ++checkedFields;
        }
        if (absent) {
            out << "    no " << spec->config << " package on this router: "
                << checkedFields << " field(s) defined from the UCI schema, 0 rows to show"
                << Qt::endl;
        } else {
            out << "    " << checkedFields << " field(s) all present in the package" << Qt::endl;
        }

        // A domain may enumerate several section types (firewall: zone, rule,
        // redirect); the editor shows all of them, the registry check above
        // only covers the primary type.
        int expectedEditorSections = entry.sections;
        if (!spec->sections.isEmpty()) {
            expectedEditorSections = 0;
            for (const QString &type : spec->sectionTypes())
                expectedEditorSections += probe.config(spec->config, type).sections.size();
        }

        editor->reload();
        if (!waitFor([&] { return editor->isLoaded() && !editor->isBusy(); }, 12000)) {
            probe.stop();
            return fail(id, "read", editor->errorText().isEmpty() ? editor->statusText()
                                                                 : editor->errorText());
        }
        if (!editor->errorText().isEmpty()) {
            probe.stop();
            return fail(id, "read", editor->errorText());
        }
        if (editor->sections()->rowCount() != expectedEditorSections) {
            probe.stop();
            return fail(id, "read", QStringLiteral("editor shows %1 section(s), expected %2")
                                       .arg(editor->sections()->rowCount())
                                       .arg(expectedEditorSections));
        }
        out << "    editor loaded: " << editor->statusText() << Qt::endl;

        if (id == QLatin1String("dhcp")) {
            // The interface field is a registry-level InterfaceRef: its choices
            // come from network.interface.dump, not from a hand-written list.
            auto *fields = qobject_cast<ConfigFieldsModel *>(editor->fields());
            auto choicesFor = [fields](const QString &option) {
                QStringList values;
                if (!fields)
                    return values;
                for (int row = 0; row < fields->rowCount(); ++row) {
                    const QModelIndex index = fields->index(row, 0);
                    if (fields->data(index, ConfigFieldsModel::OptionRole).toString() == option)
                        return fields->data(index, ConfigFieldsModel::ChoicesRole).toStringList();
                }
                return values;
            };
            const bool named = waitFor(
                [&] {
                    const QStringList choices = choicesFor(QStringLiteral("interface"));
                    return choices.contains(QStringLiteral("lan"))
                           && choices.contains(QStringLiteral("wan"))
                           && choices.contains(QStringLiteral("wan6"));
                },
                8000);
            if (!named) {
                probe.stop();
                return fail(id, "interface-ref",
                            QStringLiteral("interface choices from network.interface.dump: %1")
                                .arg(choicesFor(QStringLiteral("interface")).join(QStringLiteral(", "))));
            }
            out << "    interface choices: "
                << choicesFor(QStringLiteral("interface")).join(QStringLiteral(", ")) << Qt::endl;
        }

        if (entry.absentPackage) {
            // Absent package: the editor loads it, shows nothing, and staging
            // has nowhere to go.
            if (editor->sections()->rowCount() != 0) {
                probe.stop();
                return fail(id, "empty", QStringLiteral("expected an empty domain"));
            }
            if (!spec->fields.isEmpty())
                editor->stageValue(spec->fields.first().option, QStringLiteral("x"));
            if (editor->pendingCount() != 0) {
                probe.stop();
                return fail(id, "empty", QStringLiteral("staging on an empty domain produced %1 change(s)")
                                          .arg(editor->pendingCount()));
            }
            // Revert on an absent package is a local no-op, not a uci error.
            bool reverted = false;
            auto revertedConn = QObject::connect(editor, &ConfigEditor::reverted, editor,
                                                [&reverted] { reverted = true; });
            editor->revert();
            if (!waitFor([&] { return reverted; }, 5000) || !editor->errorText().isEmpty()) {
                probe.stop();
                return fail(id, "revert", editor->errorText().isEmpty()
                                              ? QStringLiteral("no reverted signal")
                                              : editor->errorText());
            }
            QObject::disconnect(revertedConn);
            out << "    revert on the absent package: local no-op, no router error" << Qt::endl;
            out << "    PASS " << id << ": domain present, package absent on this router, "
                << "nothing faked" << Qt::endl;
            continue;
        }

        QString section = entry.section;
        if (!entry.selectOption.isEmpty()) {
            for (const Ubus::UciSection &candidate : stored.sections) {
                if (candidate.option(entry.selectOption) == entry.selectValue) {
                    section = candidate.name;
                    break;
                }
            }
            if (section.isEmpty()) {
                probe.stop();
                return fail(id, "read", QStringLiteral("no %1 section named %2")
                                          .arg(spec->sectionType, entry.selectValue));
            }
        }

        editor->selectSection(section);
        const QString original = editor->value(entry.option);
        if (original.isEmpty()) {
            probe.stop();
            return fail(id, "read", QStringLiteral("%1.%2 is empty").arg(section, entry.option));
        }
        out << "    " << section << "." << entry.option << " = " << original
            << "  (baseline)" << Qt::endl;

        out << "    stage " << entry.option << " = " << entry.value << Qt::endl;
        if (const int code = checkStaged(editor, entry, section, original, entry.value)) {
            probe.stop();
            return code;
        }

        // Staging is local to the editor: the router must still hold the
        // baseline at this point.
        if (probe.option(spec->config, section, entry.option) != original
            || !probe.changes(spec->config).isEmpty()) {
            probe.stop();
            return fail(id, "stage", QStringLiteral("router state changed before apply"));
        }

        if (entry.applies) {
            bool applied = false;
            auto appliedConn = QObject::connect(editor, &ConfigEditor::applied, editor,
                                               [&applied](int) { applied = true; });
            editor->apply();
            if (!waitFor([&] { return applied || !editor->errorText().isEmpty(); }, 30000)) {
                probe.stop();
                return fail(id, "apply", QStringLiteral("timed out"));
            }
            if (!applied) {
                probe.stop();
                return fail(id, "apply", editor->errorText());
            }
            QObject::disconnect(appliedConn);
            if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 12000)) {
                probe.stop();
                return fail(id, "apply", QStringLiteral("pending list not cleared"));
            }
            const QString routerValue = probe.option(spec->config, section, entry.option);
            if (routerValue != entry.value || !probe.changes(spec->config).isEmpty()) {
                probe.stop();
                return fail(id, "apply", QStringLiteral("router reads %1 (staged: %2)")
                                          .arg(routerValue,
                                               probe.changes(spec->config)
                                                   .join(QStringLiteral(", "))));
            }
            out << "    applied: router " << section << "." << entry.option << " = "
                << routerValue << ", uci changes empty" << Qt::endl;

            // Baseline back through the same staged path.
            editor->stageValue(entry.option, original);
            bool restored = false;
            auto restoredConn = QObject::connect(editor, &ConfigEditor::applied, editor,
                                                [&restored](int) { restored = true; });
            editor->apply();
            if (!waitFor([&] { return restored || !editor->errorText().isEmpty(); }, 30000)) {
                probe.stop();
                return fail(id, "restore", QStringLiteral("timed out"));
            }
            if (!restored) {
                probe.stop();
                return fail(id, "restore", editor->errorText());
            }
            QObject::disconnect(restoredConn);
            waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 12000);
            if (probe.option(spec->config, section, entry.option) != original) {
                probe.stop();
                return fail(id, "restore", QStringLiteral("baseline not back: %1")
                                              .arg(probe.option(spec->config, section,
                                                                entry.option)));
            }
            out << "    restored " << section << "." << entry.option << " = " << original
                << " (lab baseline)" << Qt::endl;
        } else {
            // Never applied: stage the same option on the router in the probe's
            // own ubus session (proves the ACL + typed set path on this package),
            // then discard it with uci revert.
            const QString probeStaged = entry.value;
            if (!probe.set(spec->config, section, entry.option, probeStaged)) {
                probe.stop();
                return fail(id, "set", QStringLiteral("router rejected the staged set"));
            }
            const QStringList onRouter = probe.changes(spec->config);
            if (onRouter.size() != 1) {
                probe.stop();
                return fail(id, "set", QStringLiteral("router-side staging missing: %1")
                                           .arg(onRouter.join(QStringLiteral(", "))));
            }
            out << "    router session staged: " << onRouter.join(QStringLiteral(", "))
                << Qt::endl;
            if (!probe.revert(spec->config)
                || !probe.changes(spec->config).isEmpty()
                || probe.option(spec->config, section, entry.option) != original) {
                probe.stop();
                return fail(id, "revert", QStringLiteral("uci revert did not restore %1.%2")
                                              .arg(section, entry.option));
            }
            out << "    uci revert discarded it: " << section << "." << entry.option << " = "
                << original << ", changes empty" << Qt::endl;

            bool reverted = false;
            auto revertedConn = QObject::connect(editor, &ConfigEditor::reverted, editor,
                                                [&reverted] { reverted = true; });
            editor->revert();
            if (!waitFor([&] { return reverted || !editor->errorText().isEmpty(); }, 15000)) {
                probe.stop();
                return fail(id, "revert", QStringLiteral("timed out"));
            }
            if (!reverted) {
                probe.stop();
                return fail(id, "revert", editor->errorText());
            }
            QObject::disconnect(revertedConn);
            if (!waitFor([&] { return editor->pendingCount() == 0 && !editor->isBusy(); }, 12000)) {
                probe.stop();
                return fail(id, "revert", QStringLiteral("pending list not cleared"));
            }
            if (probe.option(spec->config, section, entry.option) != original
                || !probe.changes(spec->config).isEmpty()) {
                probe.stop();
                return fail(id, "revert", QStringLiteral("config not back at baseline"));
            }
            out << "    after revert: pending (" << editor->pendingCount() << "), " << section
                << "." << entry.option << " = " << original << ", uci changes empty"
                << Qt::endl;
        }

        out << "    PASS " << id << ": read, staged, "
            << (entry.applies ? "applied + restored" : "reverted, never applied") << Qt::endl;
    }

    probe.stop();
    out << Qt::endl << "PASS config domains: " << wanted.join(QStringLiteral(", ")) << Qt::endl;
    out.flush();
    return 0;
}
