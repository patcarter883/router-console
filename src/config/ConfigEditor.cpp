// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

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
        connect(m_client, &UbusClient::interfacesReceived, this,
                &ConfigEditor::onInterfacesReceived);
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
        return effectiveValue(*section, option);
    return {};
}

// Stored value, or the field's effective default when the option is absent
// (fw4's `enabled` is on unless the section says otherwise).
QString ConfigEditor::effectiveValue(const Ubus::UciSection &section, const QString &option) const
{
    const QString stored = section.option(option);
    if (!stored.isEmpty())
        return stored;
    for (const Config::FieldSpec &spec : m_spec.fieldsFor(section.type)) {
        if (spec.option == option)
            return spec.defaultValue;
    }
    return {};
}

void ConfigEditor::reload()
{
    if (!m_client)
        return;
    m_loaded = false;
    m_absent = false;
    setError({});
    setBusy(true, QStringLiteral("reading %1…").arg(m_spec.config));
    m_mergeConfig = {};
    m_mergeConfig.name = m_spec.config;
    m_pendingTypes = m_spec.sectionTypes();
    m_collecting = !m_pendingTypes.isEmpty();
    if (m_interfaceNames.isEmpty()) {
        QMetaObject::invokeMethod(m_client, &UbusClient::fetchInterfaces, Qt::QueuedConnection);
    }
    readNextSectionType();
}

void ConfigEditor::readNextSectionType()
{
    if (!m_client)
        return;
    if (m_pendingTypes.isEmpty()) {
        m_collecting = false;
        applyRead(m_mergeConfig);
        return;
    }
    const QString type = m_pendingTypes.takeFirst();
    QMetaObject::invokeMethod(m_client, "uciGetConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_spec.config), Q_ARG(QString, type));
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

    for (const Config::FieldSpec &spec : m_spec.fieldsFor(section->type)) {
        if (spec.option == option && spec.readOnly)
            return;
    }

    const QString committed = effectiveValue(*section, option);
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
    if (m_absent) {
        // No package on this router: nothing is staged on either side.
        m_staged.clear();
        rebuildPending();
        emit pendingChanged();
        emit reverted();
        return;
    }
    setBusy(true, QStringLiteral("reverting…"));
    QMetaObject::invokeMethod(m_client, "uciRevert", Qt::QueuedConnection,
                              Q_ARG(QString, m_spec.config));
}

void ConfigEditor::onConfigReceived(const Ubus::UciConfig &config)
{
    if (config.name != m_spec.config)
        return;

    if (m_collecting) {
        m_mergeConfig.sections.append(config.sections);
        if (!m_pendingTypes.isEmpty()) {
            readNextSectionType();
            return;
        }
        m_collecting = false;
        applyRead(m_mergeConfig);
        return;
    }
    applyRead(config);
}

void ConfigEditor::applyRead(const Ubus::UciConfig &config)
{
    m_config = config;
    m_loaded = true;
    m_sections.setSections(config.sections, m_spec);

    if (m_config.section(m_section) == nullptr) {
        m_section = config.sections.isEmpty() ? QString() : config.sections.first().name;
        emit sectionChanged();
    }

    rebuildFields();
    rebuildPending();
    setBusy(false, m_absent
                       ? QStringLiteral("no /etc/config/%1 on this router").arg(m_spec.config)
                       : QStringLiteral("%1 section(s) in %2")
                             .arg(config.sections.size())
                             .arg(m_spec.config));
    emit pendingChanged();
}

void ConfigEditor::onConfigFailed(const Ubus::Error &error)
{
    // UCI answers "not found" for a package this router does not have (no
    // wireless stack, so no /etc/config/wireless): an empty domain, not a
    // failed read. Anything else is a failure.
    if (error.status == Ubus::Status::NotFound) {
        m_absent = true;
        m_pendingTypes.clear();
        m_collecting = false;
        Ubus::UciConfig missing;
        missing.name = m_spec.config;
        applyRead(missing);
        return;
    }
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

    QList<Config::FieldSpec> specs = m_spec.fieldsFor(section->type);
    QMap<QString, QString> originals = section->options;
    for (const Config::FieldSpec &spec : specs) {
        if (!originals.contains(spec.option) && !spec.defaultValue.isEmpty())
            originals.insert(spec.option, spec.defaultValue);
    }
    const QStringList zones = zoneNames();
    for (Config::FieldSpec &spec : specs) {
        switch (spec.kind) {
        case Config::FieldKind::InterfaceRef: {
            spec.choices = m_interfaceNames;
            const QString current = section->option(spec.option);
            if (!current.isEmpty() && !spec.choices.contains(current))
                spec.choices.prepend(current);
            break;
        }
        case Config::FieldKind::ZoneRef: {
            spec.choices = zones;
            const QString current = section->option(spec.option);
            if (!current.isEmpty() && !spec.choices.contains(current))
                spec.choices.prepend(current);
            break;
        }
        case Config::FieldKind::Chain:
            originals.insert(spec.option, chainFor(*section));
            break;
        default:
            break;
        }
    }
    m_fields.setRows(specs, originals, m_staged.value(m_section));
}

// Zone names the loaded package itself declares; "*" is fw4's any-zone.
QStringList ConfigEditor::zoneNames() const
{
    QStringList names{QStringLiteral("*")};
    for (const Ubus::UciSection &section : m_config.sections) {
        if (section.type != QLatin1String("zone"))
            continue;
        const QString name = section.option(QStringLiteral("name"));
        if (!name.isEmpty() && !names.contains(name))
            names.append(name);
    }
    return names;
}

// fw4 derives the chain from src/dest on a rule (fw4.uc, parse_rule); there is
// no chain option to store, so the editor computes the WinBox row.
QString ConfigEditor::chainFor(const Ubus::UciSection &section) const
{
    const bool hasSrc = section.hasOption(QStringLiteral("src"));
    const bool hasDest = section.hasOption(QStringLiteral("dest"));
    if (hasSrc)
        return hasDest ? QStringLiteral("forward") : QStringLiteral("input");
    return QStringLiteral("output");
}

void ConfigEditor::onInterfacesReceived(const QList<Ubus::InterfaceInfo> &interfaces)
{
    QStringList names;
    for (const Ubus::InterfaceInfo &info : interfaces) {
        if (!info.name.isEmpty())
            names.append(info.name);
        if (!info.device.isEmpty())
            names.append(info.device);
        if (!info.l3Device.isEmpty())
            names.append(info.l3Device);
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    if (names == m_interfaceNames)
        return;
    m_interfaceNames = names;
    rebuildFields();
}

void ConfigEditor::rebuildPending()
{
    m_pending.clear();
    for (auto sectionIt = m_staged.constBegin(); sectionIt != m_staged.constEnd(); ++sectionIt) {
        const Ubus::UciSection *section = m_config.section(sectionIt.key());
        if (!section)
            continue;
        for (auto optionIt = sectionIt->constBegin(); optionIt != sectionIt->constEnd(); ++optionIt) {
            const QString committed = effectiveValue(*section, optionIt.key());
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
