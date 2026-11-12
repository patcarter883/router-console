#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "plugins/PluginSpec.h"

class UbusClient;

namespace Plugin {

// One plugin's data as last seen: the typed record the session keeps and the
// test asserts on. `fields` is the app's own JSON object, already decoded from
// QJsonValue to QVariant; it is the declared boundary where the payload stops
// being schema-checked by the client.
struct Sample {
    QString sourceId;
    bool ok = false;
    QString error;
    QDateTime at;
    QVariantMap fields;
};

class HttpFetcher;

// View model for one open plugin window. Polling happens on a UI-thread timer,
// but every request is handed to the ubus client's thread or to the HTTP
// fetcher's thread, and results arrive back as queued signals.
class Session : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString viewUrl READ viewUrl CONSTANT)
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(QStringList sourceIds READ sourceIds CONSTANT)
    Q_PROPERTY(int intervalMs READ intervalMs CONSTANT)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(int revision READ revision NOTIFY updated)
    Q_PROPERTY(QVariantMap samples READ allSamples NOTIFY updated)
    Q_PROPERTY(bool ready READ isReady NOTIFY updated)

public:
    Session(const Spec &spec, UbusClient *client, HttpFetcher *fetcher, const QUrl &origin,
            const QByteArray &sha256Pin, QObject *parent = nullptr);

    QString pluginId() const { return m_spec.id; }
    QString title() const { return m_spec.title; }
    QString viewUrl() const;
    QString host() const { return m_origin.host(); }
    QStringList sourceIds() const { return m_spec.sourceIds(); }
    int intervalMs() const { return m_spec.intervalMs; }
    bool isActive() const { return m_timer.isActive(); }
    int revision() const { return m_revision; }
    bool isReady() const { return !m_samples.isEmpty(); }

    const Spec &spec() const { return m_spec; }
    Sample sample(const QString &sourceId) const;
    bool allSourcesOk() const;

    Q_INVOKABLE QVariantMap data(const QString &sourceId) const;
    QVariantMap allSamples() const;

public slots:
    void start();
    void stop();
    void refresh();

signals:
    void updated();
    void activeChanged();

    void busRequested(const QString &key, const QString &object, const QString &method,
                      const QJsonObject &args);
    void httpRequested(int requestId, const QUrl &url, const QByteArray &sha256Pin);

private:
    struct Pending {
        QString sourceId;
        bool http = false;
        qint64 issuedAt = 0;
    };

    void issue(const DataSource &source, qint64 now);
    void applySource(const QString &key, const QJsonObject &payload, const QString &error);

    static constexpr int requestTimeoutMs = 15000;

    Spec m_spec;
    UbusClient *m_client = nullptr;
    HttpFetcher *m_fetcher = nullptr;
    QUrl m_origin;
    QByteArray m_pin;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QHash<QString, Sample> m_samples;
    QHash<QString, Pending> m_pending;
    QHash<int, QString> m_httpKeys;
    QHash<QString, qint64> m_lastRequest;
    int m_sequence = 0;
    int m_revision = 0;
};

} // namespace Plugin
