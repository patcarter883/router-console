#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <QVariantList>

#include "AppEngine.h"
#include "plugins/PluginHub.h"
#include "plugins/PluginSession.h"

// Data-plane check for one plugin against the dev VM: connect, open the plugin
// session exactly as PluginWindow does, and assert the view model receives the
// app's own data shape (typed Sample per declared source).
namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition) {
        QTextStream(stdout) << "  ok   " << what << Qt::endl;
        return;
    }
    ++failures;
    QTextStream(stdout) << "  FAIL " << what << Qt::endl;
}

QString value(const Plugin::Sample &sample, const QString &key)
{
    return sample.fields.value(key).toString();
}

QVariantMap object(const Plugin::Sample &sample, const QString &key)
{
    return sample.fields.value(key).toMap();
}

QVariantList list(const Plugin::Sample &sample, const QString &key)
{
    return sample.fields.value(key).toList();
}

void verifyRist2Rist(Plugin::Session &session)
{
    const Plugin::Sample service = session.sample(QStringLiteral("service"));
    const Plugin::Sample config = session.sample(QStringLiteral("config"));

    check(service.ok, QStringLiteral("service.list answered"));
    check(!service.fields.isEmpty(),
          QStringLiteral("rist2rist service is registered with procd (%1)").arg(service.fields.size()));

    check(config.ok, QStringLiteral("uci get rist2rist answered"));
    const QVariantMap main = object(config, QStringLiteral("main"));
    check(!main.isEmpty(), QStringLiteral("main section present"));
    check(main.value(QStringLiteral(".type")).toString() == QLatin1String("rist2rist"),
          QStringLiteral("main section type is rist2rist"));
    check(!main.value(QStringLiteral("listen_url")).toString().isEmpty(),
          QStringLiteral("listen_url = %1").arg(main.value(QStringLiteral("listen_url")).toString()));
    check(!main.value(QStringLiteral("profile")).toString().isEmpty(),
          QStringLiteral("profile = %1 / out_profile = %2")
              .arg(main.value(QStringLiteral("profile")).toString(),
                   main.value(QStringLiteral("out_profile")).toString()));

    int destinations = 0;
    for (const QVariant &value : config.fields) {
        const QVariantMap section = value.toMap();
        if (section.value(QStringLiteral(".type")).toString() == QLatin1String("destination")) {
            ++destinations;
            check(!section.value(QStringLiteral("address")).toString().isEmpty(),
                  QStringLiteral("destination %1 address = %2")
                      .arg(section.value(QStringLiteral(".name")).toString(),
                           section.value(QStringLiteral("address")).toString()));
        }
    }
    check(destinations >= 1, QStringLiteral("%1 destination sections").arg(destinations));
}

void verifyModemAim(Plugin::Session &session)
{
    const Plugin::Sample status = session.sample(QStringLiteral("status"));
    check(status.ok, QStringLiteral("modemaim.status answered"));
    check(!value(status, QStringLiteral("operator")).isEmpty(),
          QStringLiteral("operator = %1").arg(value(status, QStringLiteral("operator"))));
    check(!value(status, QStringLiteral("state")).isEmpty(),
          QStringLiteral("state = %1").arg(value(status, QStringLiteral("state"))));
    check(!value(status, QStringLiteral("rsrp")).isEmpty(),
          QStringLiteral("rsrp = %1 dBm").arg(value(status, QStringLiteral("rsrp"))));

    const QVariantMap serving = object(status, QStringLiteral("serving"));
    check(!serving.value(QStringLiteral("pci")).toString().isEmpty(),
          QStringLiteral("serving pci = %1, earfcn = %2")
              .arg(serving.value(QStringLiteral("pci")).toString(),
                   serving.value(QStringLiteral("earfcn")).toString()));
    check(!list(status, QStringLiteral("neighbors")).isEmpty(),
          QStringLiteral("%1 neighbours").arg(list(status, QStringLiteral("neighbors")).size()));

    const Plugin::Sample origin = session.sample(QStringLiteral("origin"));
    check(origin.ok && origin.fields.value(QStringLiteral("lat")).toDouble() != 0.0,
          QStringLiteral("origin = %1, %2")
              .arg(origin.fields.value(QStringLiteral("lat")).toDouble(), 0, 'f', 5)
              .arg(origin.fields.value(QStringLiteral("lon")).toDouble(), 0, 'f', 5));

    const Plugin::Sample towers = session.sample(QStringLiteral("towers"));
    check(towers.ok, QStringLiteral("towers.json fetched over the pinned TLS connection"));
    check(!value(towers, QStringLiteral("data_date")).isEmpty(),
          QStringLiteral("extract data_date = %1").arg(value(towers, QStringLiteral("data_date"))));

    const QVariantList rows = list(towers, QStringLiteral("towers"));
    check(rows.size() >= 1, QStringLiteral("%1 ACMA rows").arg(rows.size()));
    if (!rows.isEmpty()) {
        const QVariantMap site = rows.first().toMap();
        check(!site.value(QStringLiteral("site")).toString().isEmpty()
                  && site.contains(QStringLiteral("lat")) && site.contains(QStringLiteral("lon"))
                  && !site.value(QStringLiteral("band")).toString().isEmpty(),
              QStringLiteral("first row: site %1 %2 MHz %3 at %4, %5")
                  .arg(site.value(QStringLiteral("site")).toString())
                  .arg(site.value(QStringLiteral("mhz")).toString(),
                       site.value(QStringLiteral("band")).toString())
                  .arg(site.value(QStringLiteral("lat")).toDouble(), 0, 'f', 5)
                  .arg(site.value(QStringLiteral("lon")).toDouble(), 0, 'f', 5));
    }
}

void verifyNetTime(Plugin::Session &session)
{
    const Plugin::Sample status = session.sample(QStringLiteral("status"));
    check(status.ok, QStringLiteral("nettime.status answered"));
    check(status.fields.value(QStringLiteral("port")).toInt() == 5637,
          QStringLiteral("provider port = %1").arg(status.fields.value(QStringLiteral("port")).toInt()));
    check(value(status, QStringLiteral("clock")) == QLatin1String("CLOCK_REALTIME"),
          QStringLiteral("clock = %1").arg(value(status, QStringLiteral("clock"))));
    check(status.fields.value(QStringLiteral("enabled")).toBool(),
          QStringLiteral("enabled = %1").arg(value(status, QStringLiteral("enabled"))));
    check(status.fields.value(QStringLiteral("uptime")).toInt() > 0,
          QStringLiteral("uptime = %1 s").arg(status.fields.value(QStringLiteral("uptime")).toInt()));

    const Plugin::Sample service = session.sample(QStringLiteral("service"));
    check(service.ok && !service.fields.isEmpty(),
          QStringLiteral("procd reports the provider daemon running"));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption pluginOption("plugin", "plugin id to exercise", "id");
    const QCommandLineOption hostOption("host", "host[:port]", "host", "127.0.0.1:10443");
    const QCommandLineOption userOption("user", "username", "user", "root");
    const QCommandLineOption passwordOption("password", "password", "password", "root");
    const QCommandLineOption trustOption("accept-new-cert", "accept an unseen certificate");
    const QCommandLineOption timeoutOption("timeout", "overall timeout in ms", "ms", "30000");
    parser.addOptions({pluginOption, hostOption, userOption, passwordOption, trustOption,
                       timeoutOption});
    parser.process(app);

    QTextStream out(stdout);
    const QString pluginId = parser.value(pluginOption);
    if (pluginId.isEmpty()) {
        out << "FAILED: --plugin is required" << Qt::endl;
        return 2;
    }

    AppEngine engine;
    bool trusted = false;
    bool finished = false;

    QObject::connect(&engine, &AppEngine::stateChanged, &engine, [&] {
        if (engine.trustPending() && parser.isSet(trustOption) && !trusted) {
            trusted = true;
            engine.trustCertificate();
            return;
        }
        if (!engine.isConnected() || finished)
            return;

        auto *session = qobject_cast<Plugin::Session *>(engine.openPlugin(pluginId));
        if (!session) {
            finished = true;
            out << "FAILED: no session for plugin '" << pluginId << "' (manifest rejected?)"
                << Qt::endl;
            out.flush();
            app.exit(1);
            return;
        }

        out << "plugin " << session->pluginId() << " (" << session->title() << ")" << Qt::endl
            << "  view: " << session->viewUrl() << Qt::endl
            << "  sources: " << session->sourceIds().join(", ") << Qt::endl;

        QObject::connect(session, &Plugin::Session::updated, session, [&, session] {
            if (finished || !session->allSourcesOk())
                return;
            finished = true;
            session->stop();

            for (const QString &sourceId : session->sourceIds()) {
                const Plugin::Sample sample = session->sample(sourceId);
                out << "  sample " << sourceId << " ok at " << sample.at.toString(Qt::ISODate)
                    << ", " << sample.fields.size() << " fields" << Qt::endl;
            }

            out << "checks:" << Qt::endl;
            if (pluginId == QLatin1String("rist2rist"))
                verifyRist2Rist(*session);
            else if (pluginId == QLatin1String("modemaim"))
                verifyModemAim(*session);
            else if (pluginId == QLatin1String("nettime"))
                verifyNetTime(*session);

            out << Qt::endl << (failures == 0 ? "PASS" : "FAIL") << " (" << failures
                << " failing checks)" << Qt::endl;
            out.flush();
            app.exit(failures == 0 ? 0 : 1);
        });
    });

    QTimer::singleShot(parser.value(timeoutOption).toInt(), &app, [&] {
        if (finished)
            return;
        finished = true;
        if (!engine.isConnected()) {
            out << "SKIPPED: no router session (" << engine.statusText() << ")" << Qt::endl;
            out.flush();
            app.exit(3);
            return;
        }
        out << "FAILED: timed out waiting for plugin data" << Qt::endl;
        if (auto *session = qobject_cast<Plugin::Session *>(engine.openPlugin(pluginId))) {
            for (const QString &sourceId : session->sourceIds()) {
                const Plugin::Sample sample = session->sample(sourceId);
                out << "  " << sourceId << " ok=" << sample.ok << " error=" << sample.error
                    << Qt::endl;
            }
        }
        out.flush();
        app.exit(1);
    });

    engine.connectTo(parser.value(hostOption), QString(), parser.value(userOption),
                     parser.value(passwordOption));
    return app.exec();
}
