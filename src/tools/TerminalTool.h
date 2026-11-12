// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "protocol/ToolTypes.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// Terminal window state: a command-at-a-time console over rpcd's `file exec`.
// Each Enter is one `/bin/sh -c` on the router (no tty, no stdin), and this
// object — not the shell — keeps the working directory between commands
// (docs/ubus-rpcd-read-surface.md, "Terminal window").
class TerminalTool : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(QStringList history READ history NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString prompt READ prompt NOTIFY changed)
    Q_PROPERTY(QString cwd READ cwd NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int lastExitCode READ lastExitCode NOTIFY changed)

public:
    explicit TerminalTool(UbusClient *client, QObject *parent = nullptr);

    QVariantList lines() const { return m_lines; }
    QStringList history() const { return m_history; }
    bool isBusy() const { return m_busy; }
    QString prompt() const;
    QString cwd() const { return m_cwd; }
    QString statusText() const { return m_status; }
    int lastExitCode() const { return m_lastExitCode; }

    // The router's hostname, for the prompt; empty until the board reply lands.
    void setHostname(const QString &hostname);

    Q_INVOKABLE void run(const QString &command);
    Q_INVOKABLE void clear();
    void reset();

signals:
    void changed();

private:
    void onFinished(const Ubus::TerminalResult &result);
    void onFailed(const Ubus::Error &error);
    void append(const QString &kind, const QString &text);
    void appendBlock(const QString &kind, const QString &text);

    UbusClient *m_client = nullptr;
    QVariantList m_lines;
    QStringList m_history;
    QString m_hostname;
    QString m_cwd = QStringLiteral("/");
    QString m_status;
    QElapsedTimer m_started;
    int m_lastExitCode = 0;
    bool m_busy = false;
};
