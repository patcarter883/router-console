// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

#include "vm_fixture.h"
#include "config/ConfigEditor.h"
#include "tools/SqmStatsTool.h"
#include "vm_probe.h"

// Tool-tier check for the SQM/CAKE stats window: enable the VM's sqm queue
// through the UCI editor, wait for the cake qdisc to appear, parse it out of
// `tc -s qdisc show`, then restore the baseline.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    QCommandLineOption domainOption("domain", "config domain holding the queue", "id", "sqm");
    QCommandLineOption sectionOption("section", "queue section name", "name", "eth1");
    QCommandLineOption deviceOption("device", "device the cake qdisc must sit on", "name", "eth1");
    parser.addOptions({domainOption, sectionOption, deviceOption});
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;
    const QString section = parser.value(sectionOption);
    const QString device = parser.value(deviceOption);

    AppEngine engine;
    auto *sqm = qobject_cast<SqmStatsTool *>(engine.sqmStatsTool());
    auto *editor = qobject_cast<ConfigEditor *>(engine.configEditor(parser.value(domainOption)));
    if (!sqm || !editor) {
        out << "FAILED: sqm stats tool or config editor missing" << Qt::endl;
        return 1;
    }

    enum class Step { ReadBaseline, Enabling, WaitingForCake, Restoring, WaitingForBaseline, Done };
    Step step = Step::ReadBaseline;
    QString baseline;
    bool finished = false;
    int polls = 0;
    QVariantMap cakeRow;

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        sqm->setAutoRefresh(false);
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };

    auto restore = [&] {
        if (baseline == QLatin1String("1")) {
            out << "baseline already enabled; nothing to restore" << Qt::endl;
            step = Step::Done;
            return;
        }
        out << "restoring enabled=" << baseline << Qt::endl;
        step = Step::Restoring;
        editor->stageValue(QStringLiteral("enabled"), baseline);
        editor->apply();
    };

    auto findCake = [&] {
        for (const QVariant &value : sqm->qdiscs()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("cake")).toBool()
                && row.value(QStringLiteral("device")).toString() == device) {
                cakeRow = row;
                return true;
            }
        }
        return false;
    };

    QObject::connect(sqm, &SqmStatsTool::changed, &app, [&] {
        if (finished)
            return;

        switch (step) {
        case Step::WaitingForCake: {
            ++polls;
            if (!sqm->isCakeActive() || !findCake()) {
                if (polls > 25)
                    finish(1, QStringLiteral("FAILED: no cake qdisc on %1 after %2 polls")
                                  .arg(device)
                                  .arg(polls));
                return;
            }

            const QVariantList tins = cakeRow.value(QStringLiteral("tins")).toList();
            out << "cake qdisc on " << cakeRow.value(QStringLiteral("device")).toString() << " ("
                << cakeRow.value(QStringLiteral("handle")).toString() << "): "
                << cakeRow.value(QStringLiteral("detail")).toString() << Qt::endl;
            out << "  sent " << cakeRow.value(QStringLiteral("sent")).toString() << " in "
                << cakeRow.value(QStringLiteral("packets")).toString() << " packets, dropped "
                << cakeRow.value(QStringLiteral("dropped")).toString() << ", memory "
                << cakeRow.value(QStringLiteral("memory")).toString() << ", capacity "
                << cakeRow.value(QStringLiteral("capacity")).toString() << Qt::endl;
            for (const QVariant &tinValue : tins) {
                const QVariantMap tin = tinValue.toMap();
                out << "  tin " << tin.value(QStringLiteral("index")).toInt() << " threshold "
                    << tin.value(QStringLiteral("threshold")).toString() << " bytes "
                    << tin.value(QStringLiteral("bytes")).toString() << " drops "
                    << tin.value(QStringLiteral("drops")).toString() << " delay "
                    << tin.value(QStringLiteral("delay")).toString() << Qt::endl;
            }
            out.flush();

            const bool parsed = cakeRow.value(QStringLiteral("detail")).toString().contains(
                                    QLatin1String("bandwidth"))
                                && !tins.isEmpty();
            if (!parsed) {
                restore();
                finish(1, QStringLiteral("FAILED: cake row did not parse (bandwidth or tins missing)"));
                return;
            }
            out << "cake row parsed from `tc -s qdisc show`" << Qt::endl;
            restore();
            return;
        }
        case Step::Restoring:
        case Step::WaitingForBaseline:
            return;
        default:
            return;
        }
    });

    QObject::connect(editor, &ConfigEditor::stateChanged, &app, [&] {
        if (finished)
            return;

        if (step == Step::ReadBaseline) {
            if (!editor->isLoaded())
                return;
            editor->selectSection(section);
            baseline = editor->value(QStringLiteral("enabled"));
            out << "sqm." << section << ".enabled baseline: "
                << (baseline.isEmpty() ? QStringLiteral("(unset)") : baseline) << Qt::endl;
            if (baseline == QLatin1String("1")) {
                out << "sqm already enabled; sampling as-is" << Qt::endl;
                step = Step::WaitingForCake;
                sqm->setAutoRefresh(true);
                return;
            }
            out << "enabling sqm via the staged UCI editor" << Qt::endl;
            step = Step::Enabling;
            editor->stageValue(QStringLiteral("enabled"), QStringLiteral("1"));
            editor->apply();
            return;
        }
    });

    QObject::connect(editor, &ConfigEditor::applied, &app, [&](int changes) {
        if (finished)
            return;
        if (step == Step::Enabling) {
            out << "applied " << changes << " change(s); waiting for the cake qdisc" << Qt::endl;
            step = Step::WaitingForCake;
            polls = 0;
            sqm->setAutoRefresh(true);
            return;
        }
        if (step == Step::Restoring) {
            out << "restored baseline (applied " << changes << " change(s))" << Qt::endl;
            step = Step::Done;
            finish(0, QStringLiteral("PASS: cake qdisc parsed and sqm.enabled=%1 restored")
                          .arg(baseline));
            return;
        }
    });

    QObject::connect(editor, &ConfigEditor::reverted, &app, [&] {
        finish(1, QStringLiteral("FAILED: editor reverted the staged enable"));
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.errorText().isEmpty())
            return;
        finish(1, QStringLiteral("FAILED: %1").arg(engine.errorText()));
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.isConnected())
            return;
        out << "CONNECTED to " << engine.hostname() << "; reading the sqm queue" << Qt::endl;
        editor->reload();
    });

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        finish(3, QStringLiteral("SKIPPED: timed out (state: %1)").arg(engine.statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
