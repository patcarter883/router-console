// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include "tools/TerminalTool.h"
#include "vm_fixture.h"
#include "vm_probe.h"

namespace {

// Rows appended after `from` in one of the window's kinds.
QStringList textsOfKind(const QVariantList &lines, int from, const QString &kind)
{
    QStringList texts;
    for (int i = from; i < lines.size(); ++i) {
        const QVariantMap row = lines.at(i).toMap();
        if (row.value(QStringLiteral("kind")).toString() == kind)
            texts.append(row.value(QStringLiteral("text")).toString());
    }
    return texts;
}

} // namespace

// Terminal window check: commands go out one at a time over rpcd file exec and
// come back parsed — stdout, stderr and exit status per command — while the
// window carries the working directory and its history across commands.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    vmfixture::ConnectOptions connectOptions;
    QCommandLineParser parser;
    parser.addHelpOption();
    connectOptions.addTo(parser);
    parser.process(app);
    connectOptions.read(parser);

    QTextStream out(stdout);
    if (const int code = skipWithoutDevVm(out, connectOptions.host))
        return code;

    AppEngine engine;
    auto *tool = qobject_cast<TerminalTool *>(engine.terminalTool());
    if (!tool) {
        out << "FAILED: terminal tool missing" << Qt::endl;
        return 1;
    }

    bool finished = false;
    int step = 0;
    int linesAtRun = 0;
    int historyAtRun = 0;

    auto finish = [&](int code, const QString &line) {
        if (finished)
            return;
        finished = true;
        out << line << Qt::endl;
        out.flush();
        app.exit(code);
    };
    auto fail = [&](const QString &line) { finish(1, QStringLiteral("FAILED: %1").arg(line)); };

    auto send = [&](const QString &command) {
        linesAtRun = tool->lines().size();
        historyAtRun = tool->history().size();
        out << "  $ " << command << Qt::endl;
        tool->run(command);
    };

    QObject::connect(tool, &TerminalTool::changed, &app, [&] {
        if (finished || tool->isBusy())
            return;

        switch (step) {
        case 0:
            send(QStringLiteral("echo hello-m14"));
            ++step;
            return;

        case 1: {
            const QStringList stdoutLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("out"));
            const QStringList stderrLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("err"));
            out << "    stdout: " << stdoutLines.join(QStringLiteral("|")) << Qt::endl;
            if (!stdoutLines.contains(QStringLiteral("hello-m14"))) {
                fail(QStringLiteral("echo output not parsed: %1")
                         .arg(stdoutLines.join(QStringLiteral("|"))));
                return;
            }
            if (!stderrLines.isEmpty()) {
                fail(QStringLiteral("echo wrote to stderr: %1").arg(stderrLines.join(QStringLiteral("|"))));
                return;
            }
            if (tool->lastExitCode() != 0) {
                fail(QStringLiteral("echo exit code %1, expected 0").arg(tool->lastExitCode()));
                return;
            }
            if (tool->history().size() != historyAtRun + 1) {
                fail(QStringLiteral("history did not grow: %1 -> %2")
                         .arg(historyAtRun)
                         .arg(tool->history().size()));
                return;
            }
            send(QStringLiteral("uname -a"));
            ++step;
            return;
        }

        case 2: {
            const QStringList stdoutLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("out"));
            out << "    stdout: " << stdoutLines.join(QStringLiteral("|")) << Qt::endl;
            if (stdoutLines.isEmpty() || !stdoutLines.first().contains(QStringLiteral("Linux"))) {
                fail(QStringLiteral("uname -a not parsed: %1")
                         .arg(stdoutLines.join(QStringLiteral("|"))));
                return;
            }
            if (tool->lastExitCode() != 0) {
                fail(QStringLiteral("uname exit code %1").arg(tool->lastExitCode()));
                return;
            }
            send(QStringLiteral("echo oops >&2; exit 3"));
            ++step;
            return;
        }

        case 3: {
            const QStringList stderrLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("err"));
            out << "    stderr: " << stderrLines.join(QStringLiteral("|"))
                << " exit: " << tool->lastExitCode() << Qt::endl;
            if (!stderrLines.contains(QStringLiteral("oops"))) {
                fail(QStringLiteral("stderr not parsed: %1")
                         .arg(stderrLines.join(QStringLiteral("|"))));
                return;
            }
            if (tool->lastExitCode() != 3) {
                fail(QStringLiteral("exit code %1, expected 3").arg(tool->lastExitCode()));
                return;
            }
            send(QStringLiteral("cd /tmp"));
            ++step;
            return;
        }

        case 4:
            out << "    cwd: " << tool->cwd() << ", prompt: " << tool->prompt() << Qt::endl;
            if (tool->cwd() != QLatin1String("/tmp")) {
                fail(QStringLiteral("cd /tmp left the window in %1").arg(tool->cwd()));
                return;
            }
            if (!tool->prompt().contains(QLatin1String(":/tmp#"))) {
                fail(QStringLiteral("prompt does not show the directory: %1").arg(tool->prompt()));
                return;
            }
            send(QStringLiteral("pwd"));
            ++step;
            return;

        case 5: {
            const QStringList stdoutLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("out"));
            out << "    stdout: " << stdoutLines.join(QStringLiteral("|")) << Qt::endl;
            if (!stdoutLines.contains(QStringLiteral("/tmp"))) {
                fail(QStringLiteral("pwd from /tmp returned: %1")
                         .arg(stdoutLines.join(QStringLiteral("|"))));
                return;
            }
            send(QStringLiteral("ls /nonexistent"));
            ++step;
            return;
        }

        case 6: {
            const QStringList stderrLines =
                textsOfKind(tool->lines(), linesAtRun, QStringLiteral("err"));
            out << "    stderr: " << stderrLines.join(QStringLiteral("|"))
                << " exit: " << tool->lastExitCode() << Qt::endl;
            if (stderrLines.isEmpty() || !stderrLines.first().contains(QLatin1String("No such file"))) {
                fail(QStringLiteral("ls failure not reported on stderr"));
                return;
            }
            if (tool->lastExitCode() != 1) {
                fail(QStringLiteral("ls exit code %1, expected 1").arg(tool->lastExitCode()));
                return;
            }
            if (tool->history().size() != 6) {
                fail(QStringLiteral("history holds %1 commands, expected 6")
                         .arg(tool->history().size()));
                return;
            }
            if (!tool->prompt().contains(engine.hostname())) {
                fail(QStringLiteral("prompt %1 does not carry the router hostname %2")
                         .arg(tool->prompt(), engine.hostname()));
                return;
            }

            // History recall reads the same list the window's up/down uses.
            out << "  history: " << tool->history().join(QStringLiteral(" | ")) << Qt::endl;
            finish(0, QStringLiteral("PASS: 6 commands, stdout/stderr/exit parsed, cwd %1, "
                                     "history %2 entries")
                          .arg(tool->cwd())
                          .arg(tool->history().size()));
            return;
        }

        default:
            return;
        }
    });

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (!engine.errorText().isEmpty()) {
            fail(engine.errorText());
            return;
        }
        if (!engine.isConnected())
            return;
        out << "CONNECTED to " << engine.hostname() << Qt::endl;
    });

    vmfixture::wireTrust(engine, connectOptions.acceptNewCert);

    QTimer::singleShot(connectOptions.timeoutMs, &app, [&] {
        fail(QStringLiteral("timed out at step %1 (state: %2, terminal: %3)")
                 .arg(step)
                 .arg(engine.statusText(), tool->statusText()));
    });

    connectOptions.connect(engine);
    return app.exec();
}
