// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ConfigModels.h"

#include <QStringList>

namespace {

QString kindName(Config::FieldKind kind)
{
    switch (kind) {
    case Config::FieldKind::Integer:
        return QStringLiteral("integer");
    case Config::FieldKind::Boolean:
        return QStringLiteral("boolean");
    case Config::FieldKind::Choice:
        return QStringLiteral("choice");
    case Config::FieldKind::InterfaceRef:
        return QStringLiteral("interface-ref");
    case Config::FieldKind::ZoneRef:
        return QStringLiteral("zone-ref");
    case Config::FieldKind::Chain:
        return QStringLiteral("chain");
    case Config::FieldKind::Text:
        break;
    }
    return QStringLiteral("text");
}

} // namespace

ConfigDomainsModel::ConfigDomainsModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_domains(Config::domains())
{
}

int ConfigDomainsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_domains.size());
}

QVariant ConfigDomainsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_domains.size())
        return {};
    const Config::DomainSpec &spec = m_domains.at(index.row());
    switch (role) {
    case DomainIdRole:
        return spec.id;
    case TitleRole:
        return spec.title;
    case ConfigRole:
        return spec.config;
    case SectionTypeRole:
        return spec.sectionType;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConfigDomainsModel::roleNames() const
{
    return {{DomainIdRole, "domainId"},
            {TitleRole, "title"},
            {ConfigRole, "config"},
            {SectionTypeRole, "sectionType"}};
}

int ConfigDomainsModel::rowFor(const QString &domainId) const
{
    for (int row = 0; row < m_domains.size(); ++row) {
        if (m_domains.at(row).id == domainId)
            return row;
    }
    return -1;
}

ConfigSectionsModel::ConfigSectionsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ConfigSectionsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ConfigSectionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case NameRole:
        return row.name;
    case TypeRole:
        return row.type;
    case SummaryRole:
        return row.summary;
    case DetailRole:
        return row.detail;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConfigSectionsModel::roleNames() const
{
    return {{NameRole, "name"},
            {TypeRole, "type"},
            {SummaryRole, "summary"},
            {DetailRole, "detail"}};
}

void ConfigSectionsModel::setRows(const QList<Row> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void ConfigSectionsModel::setSections(const QList<Ubus::UciSection> &sections,
                                      const Config::DomainSpec &spec)
{
    QList<Row> rows;
    for (const Ubus::UciSection &section : sections) {
        const QStringList summaryOptions = spec.summaryOptionsFor(section.type);
        QStringList parts;
        for (const QString &option : summaryOptions) {
            const QString value = section.option(option);
            if (!value.isEmpty())
                parts.append(value);
        }
        rows.append({section.name, section.type, parts.join(QStringLiteral(" · ")),
                     QStringLiteral("%1 · %2").arg(section.type, section.name)});
    }
    setRows(rows);
}

void ConfigSectionsModel::setFrrBlocks(const QList<Routing::FrrBlock> &blocks)
{
    QList<Row> rows;
    for (int index = 0; index < blocks.size(); ++index) {
        const Routing::FrrBlock &block = blocks.at(index);
        QStringList summary;
        for (const Routing::FrrStatement &statement : block.statements) {
            if (summary.size() >= 2)
                break;
            if (statement.duplicate)
                continue;
            summary.append(QStringLiteral("%1 %2").arg(statement.keyword, statement.value).trimmed());
        }
        rows.append({QString::number(index), block.type, summary.join(QStringLiteral(" · ")),
                     block.header});
    }
    setRows(rows);
}

int ConfigSectionsModel::rowFor(const QString &name) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).name == name)
            return row;
    }
    return -1;
}

ConfigFieldsModel::ConfigFieldsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ConfigFieldsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ConfigFieldsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case OptionRole:
        return row.spec.option;
    case LabelRole:
        return row.spec.label;
    case KindRole:
        return kindName(row.spec.kind);
    case SuffixRole:
        return row.spec.suffix;
    case ChoicesRole:
        return row.spec.choices;
    case ValueRole:
        return row.value;
    case OriginalRole:
        return row.original;
    case ChangedRole:
        return row.value != row.original;
    case ReadOnlyRole:
        return row.spec.readOnly;
    default:
        return {};
    }
}

QHash<int, QByteArray> ConfigFieldsModel::roleNames() const
{
    return {{OptionRole, "option"},     {LabelRole, "label"},
            {KindRole, "kind"},         {SuffixRole, "suffix"},
            {ChoicesRole, "choices"},   {ValueRole, "value"},
            {OriginalRole, "original"}, {ChangedRole, "changed"},
            {ReadOnlyRole, "readOnly"}};
}

void ConfigFieldsModel::setRows(const QList<Config::FieldSpec> &specs,
                                const QMap<QString, QString> &originals,
                                const QMap<QString, QString> &staged)
{
    beginResetModel();
    m_rows.clear();
    for (const Config::FieldSpec &spec : specs) {
        const QString original = originals.value(spec.option);
        const QString value = staged.contains(spec.option) ? staged.value(spec.option) : original;
        m_rows.append({spec, original, value});
    }
    endResetModel();
}

PendingChangesModel::PendingChangesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PendingChangesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PendingChangesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Change &change = m_rows.at(index.row());
    switch (role) {
    case ConfigRole:
        return change.config;
    case SectionRole:
        return change.section;
    case OptionRole:
        return change.option;
    case OldValueRole:
        return change.oldValue;
    case NewValueRole:
        return change.newValue;
    case TextRole:
        return textAt(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> PendingChangesModel::roleNames() const
{
    // "description", not "text": a QML Text delegate declaring a required
    // property named `text` would shadow Text.text and render nothing.
    return {{ConfigRole, "config"},     {SectionRole, "section"},
            {OptionRole, "option"},     {OldValueRole, "oldValue"},
            {NewValueRole, "newValue"}, {TextRole, "description"}};
}

void PendingChangesModel::setChanges(const QList<Change> &changes)
{
    beginResetModel();
    m_rows = changes;
    endResetModel();
}

QString PendingChangesModel::textAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    const Change &change = m_rows.at(row);
    return QStringLiteral("%1.%2  %3 → %4")
        .arg(change.section, change.option, change.oldValue, change.newValue);
}
