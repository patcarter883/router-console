#include "ConfigEditor.h"

#include <QMetaObject>

#include "protocol/UbusClient.h"

ConfigEditor::ConfigEditor(UbusClient *client, const Config::DomainSpec &spec, QObject *parent)
    : QObject(parent)
    , m_spec(spec)
    , m_client(client)
{
    qRegisterMetaType<Ubus::UciSetRequest>();
    qRegisterMetaType<QList<Ubus::UciSetRequest>>();

    if (m_client) {
        connect(m_client, &UbusClient::uciConfigReceived, this, &ConfigEditor::onConfigReceived);
        connect(m_client, &UbusClient::uciConfigFailed, this, &ConfigEditor::onConfigFailed);
        connect(m_client, &UbusClient::uciApplied, this, &ConfigEditor::onApplied);
        connect(m_client, &UbusClient::uciApplyFailed, this, &ConfigEditor::onApplyFailed);
        connect(m_client, &UbusClient::uciReverted, this, &ConfigEditor::onReverted);
        connect(m_client, &UbusClient::uciRevertFailed, this, &ConfigEditor::onRevertFailed);
    }
}

const Ubus::UciSection *ConfigEditor::currentUciSection() const
{
    if (m_section.isEmpty())
        return nullptr;
    return m_config.section(m_section);
}

QString ConfigEditor::value(const QString &option) const
{
    const auto sectionStaged = m_staged.constFind(m_section);
    if (sectionStaged != m_staged.constEnd()) {
        const auto staged = sectionStaged->constFind(option);
        if (staged != sectionStaged->constEnd())
            return *staged;
    }
    if (const Ubus::UciSection *section = currentUciSection())
        return section->option(option);
    return {};
}

void ConfigEditor::reload()
{
    if (!m_client)
        return;
    setError({});
    setBusy(true, QStringLiteral("reading %1…").arg(m_spec.config));
    QMetaObject::invokeMethod(m_client, "uciGetConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_spec.config),
                              Q_ARG(QString, m_spec.sectionType));
}

void ConfigEditor::selectSection(const QString &name)
{
    if (m_section == name)
        return;
    m_section = name;
    rebuildFields();
    emit sectionChanged();
}

void ConfigEditor::stageValue(const QString &option, const QString &value)
{
    const Ubus::UciSection *section = currentUciSection();
    if (!section)
        return;

    const QString committed = section->option(option);
    QMap<QString, QString> &sectionStaged = m_staged[m_section];
    if (value == committed)
        sectionStaged.remove(option);
    else
        sectionStaged.insert(option, value);
    if (sectionStaged.isEmpty())
        m_staged.remove(m_section);

    // No field-model reset here: it would destroy the delegate the operator is
    // typing into. The pending list is the live feedback.
    rebuildPending();
    emit pendingChanged();
}

void ConfigEditor::apply()
{
    if (!m_client)
        return;
    if (m_pending.isEmpty()) {
        setError({});
        m_status = QStringLiteral("no pending changes");
        emit stateChanged();
        return;
    }

    QList<Ubus::UciSetRequest> requests;
    for (const PendingChangesModel::Change &change : m_pending) {
        requests.append({change.config, change.section, change.option, change.newValue});
    }

    setError({});
    setBusy(true, QStringLiteral("applying %1 change(s)…").arg(requests.size()));
    QMetaObject::invokeMethod(m_client, "uciApplyStaged", Qt::QueuedConnection,
                              Q_ARG(QString, m_spec.config),
                              Q_ARG(QList<Ubus::UciSetRequest>, requests),
                              Q_ARG(int, 30));
}

void ConfigEditor::revert()
{
    if (!m_client)
        return;
    setError({});
    setBusy(true, QStringLiteral("reverting…"));
    QMetaObject::invokeMethod(m_client, "uciRevert", Qt::QueuedConnection,
                              Q_ARG(QString, m_spec.config));
}

void ConfigEditor::onConfigReceived(const Ubus::UciConfig &config)
{
    if (config.name != m_spec.config)
        return;

    m_config = config;
    m_sections.setSections(config.sections, m_spec.summaryOptions);

    if (m_config.section(m_section) == nullptr) {
        m_section = config.sections.isEmpty() ? QString() : config.sections.first().name;
        emit sectionChanged();
    }

    rebuildFields();
    rebuildPending();
    setBusy(false, QStringLiteral("%1 section(s) in %2").arg(config.sections.size()).arg(m_spec.config));
    emit pendingChanged();
}

void ConfigEditor::onConfigFailed(const Ubus::Error &error)
{
    setBusy(false, QStringLiteral("read failed"));
    setError(error.message.isEmpty() ? QStringLiteral("uci get failed (status %1)")
                                           .arg(int(error.status))
                                     : error.message);
}

void ConfigEditor::onApplied(int changeCount)
{
    m_staged.clear();
    m_pending.clear();
    m_pendingModel.setChanges(m_pending);
    emit pendingChanged();
    setBusy(false, QStringLiteral("applied %1 change(s)").arg(changeCount));
    emit applied(changeCount);
    reload();
}

void ConfigEditor::onApplyFailed(const Ubus::Error &error)
{
    setBusy(false, QStringLiteral("apply failed"));
    setError(error.message.isEmpty() ? QStringLiteral("uci apply failed (status %1)")
                                           .arg(int(error.status))
                                     : error.message);
}

void ConfigEditor::onReverted(const QString &config)
{
    if (config != m_spec.config)
        return;
    const int discarded = int(m_pending.size());
    m_staged.clear();
    m_pending.clear();
    m_pendingModel.setChanges(m_pending);
    emit pendingChanged();
    setBusy(false, QStringLiteral("reverted %1 change(s)").arg(discarded));
    emit reverted();
    reload();
}

void ConfigEditor::onRevertFailed(const Ubus::Error &error)
{
    setBusy(false, QStringLiteral("revert failed"));
    setError(error.message.isEmpty() ? QStringLiteral("uci revert failed (status %1)")
                                           .arg(int(error.status))
                                     : error.message);
}

void ConfigEditor::rebuildFields()
{
    const Ubus::UciSection *section = currentUciSection();
    if (!section) {
        m_fields.setRows({}, {}, {});
        return;
    }
    m_fields.setRows(m_spec.fields, section->options, m_staged.value(m_section));
}

void ConfigEditor::rebuildPending()
{
    m_pending.clear();
    for (auto sectionIt = m_staged.constBegin(); sectionIt != m_staged.constEnd(); ++sectionIt) {
        const Ubus::UciSection *section = m_config.section(sectionIt.key());
        if (!section)
            continue;
        for (auto optionIt = sectionIt->constBegin(); optionIt != sectionIt->constEnd(); ++optionIt) {
            const QString committed = section->option(optionIt.key());
            if (committed == *optionIt)
                continue;
            m_pending.append({m_spec.config, sectionIt.key(), optionIt.key(), committed, *optionIt});
        }
    }
    m_pendingModel.setChanges(m_pending);
}

void ConfigEditor::setBusy(bool busy, const QString &status)
{
    m_busy = busy;
    m_status = status;
    emit stateChanged();
}

void ConfigEditor::setError(const QString &error)
{
    m_error = error;
    emit stateChanged();
}
