#include "PluginSession.h"

#include <QMetaObject>
#include <QStringList>

#include "plugins/PluginHttpFetcher.h"
#include "protocol/UbusClient.h"

namespace Plugin {

namespace {

// rpcd wraps `uci get` as {"values": {section: {...}}}; flattening it here keeps
// the plugin QML free of transport knowledge.
QVariantMap unwrapPayload(const QJsonObject &payload)
{
    if (payload.size() == 1) {
        const QJsonValue values = payload.value(QStringLiteral("values"));
        if (values.isObject())
            return values.toObject().toVariantMap();
    }
    return payload.toVariantMap();
}

} // namespace

Session::Session(const Spec &spec, UbusClient *client, HttpFetcher *fetcher, const QUrl &origin,
                 const QByteArray &sha256Pin, QObject *parent)
    : QObject(parent)
    , m_spec(spec)
    , m_client(client)
    , m_fetcher(fetcher)
    , m_origin(origin)
    , m_pin(sha256Pin)
{
    m_clock.start();

    int tick = m_spec.intervalMs;
    for (const DataSource &source : m_spec.sources) {
        const int interval = source.intervalMs > 0 ? source.intervalMs : m_spec.intervalMs;
        tick = qMin(tick, interval);
    }
    m_timer.setInterval(qMax(minimumIntervalMs, tick));
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Session::refresh);

    if (m_client) {
        connect(this, &Session::busRequested, m_client, &UbusClient::callApp,
                Qt::QueuedConnection);
        connect(m_client, &UbusClient::appCallSucceeded, this,
                [this](const QString &key, const QJsonObject &result) {
                    applySource(key, result, {});
                });
        connect(m_client, &UbusClient::appCallFailed, this,
                [this](const QString &key, const Ubus::Error &error) {
                    applySource(key, {}, error.message.isEmpty()
                                            ? QStringLiteral("ubus status %1")
                                                  .arg(static_cast<int>(error.status))
                                            : error.message);
                });
    }

    if (m_fetcher) {
        connect(this, &Session::httpRequested, m_fetcher, &HttpFetcher::fetch,
                Qt::QueuedConnection);
        connect(m_fetcher, &HttpFetcher::finished, this,
                [this](int requestId, const QJsonObject &payload, const QString &error) {
                    const QString key = m_httpKeys.take(requestId);
                    if (key.isEmpty())
                        return;
                    applySource(key, payload, error);
                });
    }
}

QString Session::viewUrl() const
{
    return QUrl::fromLocalFile(m_spec.viewPath).toString();
}

Sample Session::sample(const QString &sourceId) const
{
    return m_samples.value(sourceId);
}

bool Session::allSourcesOk() const
{
    for (const DataSource &source : m_spec.sources) {
        const Sample sample = m_samples.value(source.id);
        if (!sample.ok || sample.fields.isEmpty())
            return false;
    }
    return !m_spec.sources.isEmpty();
}

QVariantMap Session::data(const QString &sourceId) const
{
    return allSamples().value(sourceId).toMap();
}

QVariantMap Session::allSamples() const
{
    QVariantMap all;
    for (auto it = m_samples.cbegin(); it != m_samples.cend(); ++it) {
        QVariantMap entry;
        entry.insert(QStringLiteral("ok"), it->ok);
        entry.insert(QStringLiteral("error"), it->error);
        entry.insert(QStringLiteral("at"), it->at.toString(Qt::ISODate));
        entry.insert(QStringLiteral("fields"), it->fields);
        all.insert(it.key(), entry);
    }
    return all;
}

void Session::start()
{
    if (m_timer.isActive())
        return;
    m_timer.start();
    emit activeChanged();
    refresh();
}

void Session::stop()
{
    if (!m_timer.isActive())
        return;
    m_timer.stop();
    m_pending.clear();
    m_httpKeys.clear();
    m_lastRequest.clear();
    emit activeChanged();
}

void Session::refresh()
{
    if (!m_client)
        return;

    const qint64 now = m_clock.elapsed();

    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now - it->issuedAt < requestTimeoutMs) {
            ++it;
            continue;
        }
        const QString sourceId = it->sourceId;
        it = m_pending.erase(it);
        Sample sample;
        sample.sourceId = sourceId;
        sample.error = QStringLiteral("no answer within %1 s").arg(requestTimeoutMs / 1000);
        sample.at = QDateTime::currentDateTimeUtc();
        m_samples.insert(sourceId, sample);
        ++m_revision;
        emit updated();
    }

    for (const DataSource &source : m_spec.sources) {
        bool inFlight = false;
        for (const Pending &pending : m_pending) {
            if (pending.sourceId == source.id) {
                inFlight = true;
                break;
            }
        }
        if (inFlight)
            continue;

        const int interval = source.intervalMs > 0 ? source.intervalMs : m_spec.intervalMs;
        if (m_lastRequest.contains(source.id) && now - m_lastRequest.value(source.id) < interval)
            continue;

        issue(source, now);
    }
}

void Session::issue(const DataSource &source, qint64 now)
{
    const int sequence = ++m_sequence;
    const QString key = QStringLiteral("%1#%2#%3").arg(m_spec.id).arg(source.id).arg(sequence);

    Pending pending;
    pending.sourceId = source.id;
    pending.http = source.transport == Transport::Http;
    pending.issuedAt = now;
    m_pending.insert(key, pending);
    m_lastRequest.insert(source.id, now);

    if (pending.http) {
        m_httpKeys.insert(sequence, key);
        emit httpRequested(sequence, m_origin.resolved(QUrl(source.path)), m_pin);
        return;
    }

    emit busRequested(key, source.object, source.method, source.args);
}

void Session::applySource(const QString &key, const QJsonObject &payload, const QString &error)
{
    auto pending = m_pending.find(key);
    if (pending == m_pending.end())
        return;

    Sample sample;
    sample.sourceId = pending->sourceId;
    sample.at = QDateTime::currentDateTimeUtc();
    if (error.isEmpty()) {
        sample.ok = true;
        sample.fields = unwrapPayload(payload);
    } else {
        sample.error = error;
    }

    const QString sourceId = pending->sourceId;
    m_pending.erase(pending);
    m_samples.insert(sourceId, sample);
    ++m_revision;
    emit updated();
}

} // namespace Plugin
