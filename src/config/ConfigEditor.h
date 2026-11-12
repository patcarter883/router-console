// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include "ConfigModels.h"
#include "DomainRegistry.h"
#include "protocol/UbusTypes.h"

class UbusClient;

// One open domain editor: reads the package, stages option edits locally, shows
// the exact staged differences and commits them with uci set -> apply ->
// confirm. Nothing reaches the router until apply(); revert() discards both the
// local staging and anything staged on the router for the package.
//
// Lives on the UI thread; every router call is queued onto the ubus client's
// thread and comes back as a typed result.
class ConfigEditor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString domainId READ domainId CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString config READ config CONSTANT)
    Q_PROPERTY(bool loaded READ isLoaded NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QString currentSection READ currentSection NOTIFY sectionChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)
    Q_PROPERTY(QAbstractItemModel *sections READ sections CONSTANT)
    Q_PROPERTY(QAbstractItemModel *fields READ fields CONSTANT)
    Q_PROPERTY(QAbstractItemModel *pendingChanges READ pendingChanges CONSTANT)

public:
    ConfigEditor(UbusClient *client, const Config::DomainSpec &spec, QObject *parent = nullptr);

    QString domainId() const { return m_spec.id; }
    QString title() const { return m_spec.title; }
    QString config() const { return m_spec.config; }
    bool isLoaded() const { return m_loaded; }
    bool isBusy() const { return m_busy; }
    QString statusText() const { return m_status; }
    QString errorText() const { return m_error; }
    QString currentSection() const { return m_section; }
    int pendingCount() const { return int(m_pending.size()); }
    QAbstractItemModel *sections() { return &m_sections; }
    QAbstractItemModel *fields() { return &m_fields; }
    QAbstractItemModel *pendingChanges() { return &m_pendingModel; }

    // Staged value of an option of the current section (falls back to committed).
    Q_INVOKABLE QString value(const QString &option) const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE void selectSection(const QString &name);
    Q_INVOKABLE void stageValue(const QString &option, const QString &value);
    Q_INVOKABLE void apply();
    Q_INVOKABLE void revert();

signals:
    void stateChanged();
    void sectionChanged();
    void pendingChanged();
    // Emitted after a successful apply: the router now holds these values.
    void applied(int changeCount);
    void reverted();

private:
    void onConfigReceived(const Ubus::UciConfig &config);
    void onConfigFailed(const Ubus::Error &error);
    void onApplied(int changeCount);
    void onApplyFailed(const Ubus::Error &error);
    void onReverted(const QString &config);
    void onRevertFailed(const Ubus::Error &error);

    void rebuildFields();
    void rebuildPending();
    void setBusy(bool busy, const QString &status);
    void setError(const QString &error);
    const Ubus::UciSection *currentUciSection() const;

    Config::DomainSpec m_spec;
    UbusClient *m_client = nullptr;
    Ubus::UciConfig m_config;
    QString m_section;
    // section -> option -> staged value
    QMap<QString, QMap<QString, QString>> m_staged;
    QList<PendingChangesModel::Change> m_pending;

    ConfigSectionsModel m_sections;
    ConfigFieldsModel m_fields;
    PendingChangesModel m_pendingModel;

    QString m_status;
    QString m_error;
    bool m_busy = false;
    // True once a read has answered, including a package the router does not
    // have: the domain loaded, it just holds no sections.
    bool m_loaded = false;
    // The last read answered NotFound: the package does not exist on this router.
    bool m_absent = false;
};
