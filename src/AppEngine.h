#pragma once

#include <QObject>

// Owns the connection lifecycle and the ubus session; the protocol layer
// (ubus JSON-RPC over HTTPS) attaches beneath this and is not part of the
// skeleton.
class AppEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit AppEngine(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    Q_INVOKABLE void connectTo(const QString &host, const QString &username, const QString &password);

signals:
    void connectedChanged();

private:
    bool m_connected = false;
};
