#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "plugins/PluginSpec.h"

// Manifest contract check, no router needed: the repository's plugins/ tree
// parses, each of the three custom apps declares the sources it binds to, and a
// manifest with a missing view is rejected with the reason rather than silently
// dropped.
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

const Plugin::Spec *find(const QList<Plugin::Spec> &specs, const QString &id)
{
    for (const Plugin::Spec &spec : specs) {
        if (spec.id == id)
            return &spec;
    }
    return nullptr;
}

bool hasSource(const Plugin::Spec &spec, const QString &id, Plugin::Transport transport)
{
    const Plugin::DataSource *source = spec.source(id);
    return source && source->transport == transport;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption dirOption("dir", "plugin root to scan", "path",
                                       QStringLiteral("plugins"));
    const QCommandLineOption dumpOption("dump", "print every loaded plugin");
    parser.addOptions({dirOption, dumpOption});
    parser.process(app);

    QTextStream out(stdout);
    const QString root = parser.value(dirOption);
    const QList<Plugin::Spec> specs = Plugin::loadPlugins(root);

    out << "plugin root: " << QFileInfo(root).absoluteFilePath() << Qt::endl;
    for (const Plugin::Spec &spec : specs) {
        out << "  " << spec.id << " | " << spec.title
            << " | available=" << (spec.isValid() ? "yes" : "no")
            << " | sources=" << spec.sourceIds().join(",")
            << " | view=" << (spec.viewPath.isEmpty() ? "-" : spec.viewPath)
            << (spec.error.isEmpty() ? QString() : QStringLiteral(" | error=") + spec.error)
            << Qt::endl;
    }

    if (parser.isSet(dumpOption))
        return specs.isEmpty() ? 1 : 0;

    out << "checks:" << Qt::endl;

    const Plugin::Spec *rist = find(specs, QStringLiteral("rist2rist"));
    const Plugin::Spec *modem = find(specs, QStringLiteral("modemaim"));
    const Plugin::Spec *nettime = find(specs, QStringLiteral("nettime"));
    const Plugin::Spec *hello = find(specs, QStringLiteral("hello"));

    check(specs.size() >= 4, QStringLiteral("four plugins found (%1)").arg(specs.size()));
    check(rist && rist->isValid(), QStringLiteral("rist2rist manifest is valid"));
    check(modem && modem->isValid(), QStringLiteral("modemaim manifest is valid"));
    check(nettime && nettime->isValid(), QStringLiteral("nettime manifest is valid"));
    check(hello && hello->isValid(), QStringLiteral("hello stub manifest is valid"));

    if (rist) {
        check(hasSource(*rist, QStringLiteral("service"), Plugin::Transport::Ubus),
              QStringLiteral("rist2rist reads procd's service.list"));
        const Plugin::DataSource *config = rist->source(QStringLiteral("config"));
        check(config && config->object == QLatin1String("uci")
                  && config->method == QLatin1String("get")
                  && config->args.value(QStringLiteral("config")).toString()
                         == QLatin1String("rist2rist"),
              QStringLiteral("rist2rist reads its own UCI package"));
    }

    if (modem) {
        check(hasSource(*modem, QStringLiteral("status"), Plugin::Transport::Ubus),
              QStringLiteral("modemaim reads its rpcd status method"));
        const Plugin::DataSource *towers = modem->source(QStringLiteral("towers"));
        check(towers && towers->transport == Plugin::Transport::Http
                  && towers->path == QLatin1String("/luci-static/resources/modemaim/towers.json"),
              QStringLiteral("modemaim fetches the static towers.json"));
        check(towers && towers->intervalMs >= 30000,
              QStringLiteral("modemaim treats the extract as slow-moving"));
    }

    if (nettime) {
        check(hasSource(*nettime, QStringLiteral("status"), Plugin::Transport::Ubus),
              QStringLiteral("nettime reads its status method"));
    }

    if (hello) {
        check(QFileInfo::exists(hello->viewPath),
              QStringLiteral("hello view exists outside the QML module"));
    }

    // A broken manifest is reported, not dropped.
    QTemporaryDir scratch;
    const QString brokenDir = QDir(scratch.path()).filePath(QStringLiteral("broken"));
    QDir().mkpath(brokenDir);
    QFile manifest(QDir(brokenDir).filePath(QStringLiteral("plugin.json")));
    manifest.open(QIODevice::WriteOnly);
    manifest.write(R"({"id":"broken","title":"Broken","view":"Missing.qml",
                       "sources":[{"id":"s","transport":"ubus","object":"system","method":"board"}]})");
    manifest.close();

    const Plugin::Spec broken = Plugin::loadPlugin(brokenDir);
    check(!broken.isValid() && broken.error.contains(QLatin1String("does not exist")),
          QStringLiteral("missing view rejected: %1").arg(broken.error));

    const QString noSourceDir = QDir(scratch.path()).filePath(QStringLiteral("nosource"));
    QDir().mkpath(noSourceDir);
    QFile view(QDir(noSourceDir).filePath(QStringLiteral("View.qml")));
    view.open(QIODevice::WriteOnly);
    view.write("import QtQuick\nItem {}\n");
    view.close();
    QFile emptyManifest(QDir(noSourceDir).filePath(QStringLiteral("plugin.json")));
    emptyManifest.open(QIODevice::WriteOnly);
    emptyManifest.write(R"({"id":"nosource","title":"No Source","view":"View.qml"})");
    emptyManifest.close();

    const Plugin::Spec noSource = Plugin::loadPlugin(noSourceDir);
    check(!noSource.isValid() && noSource.error.contains(QLatin1String("no data sources")),
          QStringLiteral("manifest without sources rejected: %1").arg(noSource.error));

    out << Qt::endl << (failures == 0 ? "PASS" : "FAIL") << " (" << failures
        << " failing checks)" << Qt::endl;
    return failures == 0 ? 0 : 1;
}
