#include "AppEngine.h"

AppEngine::AppEngine(QObject *parent) : QObject(parent) {}

void AppEngine::connectTo(const QString &host, const QString &username, const QString &password)
{
    // Skeleton: the real call is the rpcd session login over /ubus —
    // docs/ubus-rpcd-read-surface.md, "Auth".
    Q_UNUSED(host);
    Q_UNUSED(username);
    Q_UNUSED(password);
    m_connected = false;
    emit connectedChanged();
}
