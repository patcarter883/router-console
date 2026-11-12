// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

#include "DomainRegistry.h"
#include "protocol/UbusTypes.h"

// View models for the config editor. They hold no state of their own beyond the
// rows the ConfigEditor feeds them, so the staged/committed distinction lives in
// one place (ConfigEditor).

// The tree entries: one row per registry domain.
class ConfigDomainsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DomainIdRole = Qt::UserRole + 1,
        TitleRole,
        ConfigRole,
        SectionTypeRole,
    };

    explicit ConfigDomainsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowFor(const QString &domainId) const;

private:
    QList<Config::DomainSpec> m_domains;
};

// Sections of the open domain, with the registry's summary options joined.
class ConfigSectionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        TypeRole,
        SummaryRole,
    };

    explicit ConfigSectionsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSections(const QList<Ubus::UciSection> &sections, const QStringList &summaryOptions);
    Q_INVOKABLE int rowFor(const QString &name) const;

private:
    struct Row {
        QString name;
        QString type;
        QString summary;
    };

    QList<Row> m_rows;
};

// One editable option of the selected section: committed value, staged value.
class ConfigFieldsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        OptionRole = Qt::UserRole + 1,
        LabelRole,
        KindRole,
        SuffixRole,
        ChoicesRole,
        ValueRole,
        OriginalRole,
        ChangedRole,
        ReadOnlyRole,
    };

    explicit ConfigFieldsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<Config::FieldSpec> &specs,
                 const QMap<QString, QString> &originals,
                 const QMap<QString, QString> &staged);

private:
    struct Row {
        Config::FieldSpec spec;
        QString original;
        QString value;
    };

    QList<Row> m_rows;
};

// The staged-changes list: one row per option whose staged value differs from
// the committed one.
class PendingChangesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ConfigRole = Qt::UserRole + 1,
        SectionRole,
        OptionRole,
        OldValueRole,
        NewValueRole,
        TextRole,
    };

    struct Change {
        QString config;
        QString section;
        QString option;
        QString oldValue;
        QString newValue;

        bool operator==(const Change &other) const
        {
            return config == other.config && section == other.section
                   && option == other.option && oldValue == other.oldValue
                   && newValue == other.newValue;
        }
    };

    explicit PendingChangesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setChanges(const QList<Change> &changes);
    Q_INVOKABLE QString textAt(int row) const;

private:
    QList<Change> m_rows;
};
