#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "AppEngine.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("open-broadcast");
    app.setApplicationName("router-console");

    qmlRegisterSingletonInstance("RouterConsole", 1, 0, "AppEngine", new AppEngine(&app));

    QQmlApplicationEngine engine;
    engine.loadFromModule("RouterConsole", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
