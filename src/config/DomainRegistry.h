// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Config domains are data, not pages: a domain names a UCI package and the
// section types to enumerate, and lists the options the editor exposes. Adding a
// domain is one entry in DomainRegistry.cpp; the editor, the tree and the
// staged-changes list are generic over it.
namespace Config {

enum class FieldKind {
    Text,
    Integer,
    Boolean,
    Choice,
    // The value names a network interface: the editor offers the live
    // network.interface.dump names and still accepts free text.
    InterfaceRef,
    // The value names a firewall zone: the editor offers the zone names the
    // loaded package itself declares (plus "*").
    ZoneRef,
    // WinBox's chain. fw4 has no such option: it derives input/forward/output
    // from src/dest, so this row is computed and read-only.
    Chain,
};

struct FieldSpec {
    QString option; // UCI option name, as stored in /etc/config/<package>
    QString label;
    FieldKind kind = FieldKind::Text;
    QString suffix;       // shown after the editor, e.g. "kbit/s"
    QStringList choices;  // FieldKind::Choice only
    bool readOnly = false;
    // Effective value when the section omits the option. fw4's `enabled`
    // defaults to 1, so an absent option is shown as enabled rather than blank.
    QString defaultValue;
    // UCI list option: the staged space-separated value is written as an array.
    bool list = false;
};

// One section type of a domain. A domain with several groups enumerates every
// group in one editor (firewall: zones, filter rules, NAT redirects).
struct SectionSpec {
    QString type;               // UCI section type
    QStringList summaryOptions; // options joined into the section list subtitle
    QList<FieldSpec> fields;
};

struct DomainSpec {
    QString id;          // stable key used by the tree and the editor
    QString title;       // tree label
    QString config;      // UCI package
    QString sectionType; // primary UCI section type
    QStringList summaryOptions; // primary group's subtitle options
    QList<FieldSpec> fields;    // primary group's fields
    QList<SectionSpec> sections; // empty: one sectionType; else every group

    // Section types the editor reads. Always non-empty for a valid domain.
    QStringList sectionTypes() const;
    // Group for `type`; nullptr when the domain has a single section type.
    const SectionSpec *sectionFor(const QString &type) const;
    QStringList summaryOptionsFor(const QString &type) const;
    QList<FieldSpec> fieldsFor(const QString &type) const;
};

const QList<DomainSpec> &domains();

// nullptr when the id is unknown.
const DomainSpec *domain(const QString &id);

} // namespace Config
