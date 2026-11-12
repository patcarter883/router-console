#pragma once

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QtGlobal>
#include <QObject>
#include <QString>

#include "AppEngine.h"

// Shared plumbing for the tool-tier tests: the same connect options as the
// milestone-1 smoke test and the certificate-trust handler every test needs.
namespace vmfixture {

struct ConnectOptions {
    // A lab VM can sit on different forwards (a private worktree's copy); the
    // environment override keeps ctest pointed at the right instance.
    QString host = qEnvironmentVariable("RC_VM_HOST", QStringLiteral("127.0.0.1:10443"));
    QString user = qEnvironmentVariable("RC_VM_USER", QStringLiteral("root"));
    QString password = qEnvironmentVariable("RC_VM_PASS", QStringLiteral("root"));
    bool acceptNewCert = false;
    int timeoutMs = 60000;

    void addTo(QCommandLineParser &parser)
    {
        hostOption = new QCommandLineOption(QStringLiteral("host"), QStringLiteral("host[:port]"),
                                            QStringLiteral("host"), host);
        userOption = new QCommandLineOption(QStringLiteral("user"), QStringLiteral("username"),
                                            QStringLiteral("user"), user);
        passwordOption = new QCommandLineOption(QStringLiteral("password"), QStringLiteral("password"),
                                                QStringLiteral("password"), password);
        trustOption = new QCommandLineOption(QStringLiteral("accept-new-cert"),
                                             QStringLiteral("accept an unseen certificate"));
        timeoutOption = new QCommandLineOption(QStringLiteral("timeout"),
                                               QStringLiteral("overall timeout in ms"),
                                               QStringLiteral("ms"), QString::number(timeoutMs));
        parser.addOptions({*hostOption, *userOption, *passwordOption, *trustOption, *timeoutOption});
    }

    void read(const QCommandLineParser &parser)
    {
        host = parser.value(*hostOption);
        user = parser.value(*userOption);
        password = parser.value(*passwordOption);
        acceptNewCert = parser.isSet(*trustOption);
        timeoutMs = parser.value(*timeoutOption).toInt();
    }

    void connect(AppEngine &engine) const
    {
        engine.connectTo(host, QString(), user, password);
    }

private:
    QCommandLineOption *hostOption = nullptr;
    QCommandLineOption *userOption = nullptr;
    QCommandLineOption *passwordOption = nullptr;
    QCommandLineOption *trustOption = nullptr;
    QCommandLineOption *timeoutOption = nullptr;
};

inline void wireTrust(AppEngine &engine, bool acceptNewCert)
{
    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&engine, acceptNewCert] {
        if (acceptNewCert && engine.trustPending())
            engine.trustCertificate();
    });
}

} // namespace vmfixture
