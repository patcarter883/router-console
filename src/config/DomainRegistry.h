// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Config domains are data, not pages: a domain names a UCI package and the
// section type to enumerate, and lists the options the editor exposes. Adding a
// domain is one entry in DomainRegistry.cpp; the editor, the tree and the
// staged-changes list are generic over it.
namespace Config {

enum class FieldKind {
    Text,
    Integer,
    Boolean,
    Choice,
};

struct FieldSpec {
    QString option; // UCI option name, as stored in /etc/config/<package>
    QString label;
    FieldKind kind = FieldKind::Text;
    QString suffix;       // shown after the editor, e.g. "kbit/s"
    QStringList choices;  // FieldKind::Choice only
    bool readOnly = false;
};

struct DomainSpec {
    QString id;          // stable key used by the tree and the editor
    QString title;       // tree label
    QString config;      // UCI package
    QString sectionType; // UCI section type to enumerate
    QStringList summaryOptions; // options joined into the section list subtitle
    QList<FieldSpec> fields;
};

const QList<DomainSpec> &domains();

// nullptr when the id is unknown.
const DomainSpec *domain(const QString &id);

} // namespace Config
