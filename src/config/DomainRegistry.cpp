// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DomainRegistry.h"

namespace Config {

namespace {

// Option names, section type and the choice lists below are read from the dev
// VM's own state: /etc/config/sqm (`config queue 'eth1'`), the qdiscs sqm
// verified on this kernel (/var/run/sqm/available_qdiscs: cake, fq_codel) and
// the QoS scripts shipped in /usr/lib/sqm/*.qos.
DomainSpec sqmDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("sqm");
    spec.title = QStringLiteral("SQM");
    spec.config = QStringLiteral("sqm");
    spec.sectionType = QStringLiteral("queue");
    spec.summaryOptions = {QStringLiteral("interface"), QStringLiteral("qdisc"),
                           QStringLiteral("download"), QStringLiteral("upload")};
    spec.fields = {
        {QStringLiteral("enabled"), QStringLiteral("Enabled"), FieldKind::Boolean, {}, {}, false},
        {QStringLiteral("interface"), QStringLiteral("Interface"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("download"), QStringLiteral("Download"), FieldKind::Integer,
         QStringLiteral("kbit/s"), {}, false},
        {QStringLiteral("upload"), QStringLiteral("Upload"), FieldKind::Integer,
         QStringLiteral("kbit/s"), {}, false},
        {QStringLiteral("qdisc"), QStringLiteral("Queue discipline"), FieldKind::Choice, {},
         {QStringLiteral("cake"), QStringLiteral("fq_codel")}, false},
        {QStringLiteral("script"), QStringLiteral("QoS script"), FieldKind::Choice, {},
         {QStringLiteral("piece_of_cake.qos"), QStringLiteral("layer_cake.qos"),
          QStringLiteral("simple.qos"), QStringLiteral("simplest.qos"),
          QStringLiteral("simplest_tbf.qos")},
         false},
        {QStringLiteral("ingress_ecn"), QStringLiteral("Ingress ECN"), FieldKind::Choice, {},
         {QStringLiteral("ECN"), QStringLiteral("NOECN")}, false},
        {QStringLiteral("egress_ecn"), QStringLiteral("Egress ECN"), FieldKind::Choice, {},
         {QStringLiteral("ECN"), QStringLiteral("NOECN")}, false},
        {QStringLiteral("linklayer"), QStringLiteral("Link layer"), FieldKind::Choice, {},
         {QStringLiteral("none"), QStringLiteral("atm"), QStringLiteral("ethernet")}, false},
        {QStringLiteral("use_mq"), QStringLiteral("Multi-queue (mq)"), FieldKind::Boolean, {}, {},
         false},
    };
    return spec;
}

// Second domain, to keep the registry honest: /etc/config/system has one
// anonymous `system` section (cfg01e48a on the VM).
DomainSpec systemDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("system");
    spec.title = QStringLiteral("System");
    spec.config = QStringLiteral("system");
    spec.sectionType = QStringLiteral("system");
    spec.summaryOptions = {QStringLiteral("hostname"), QStringLiteral("timezone")};
    spec.fields = {
        {QStringLiteral("hostname"), QStringLiteral("Hostname"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("timezone"), QStringLiteral("Timezone"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("zonename"), QStringLiteral("Zone name"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("log_size"), QStringLiteral("Log size"), FieldKind::Integer,
         QStringLiteral("KiB"), {}, false},
    };
    return spec;
}

} // namespace

const QList<DomainSpec> &domains()
{
    static const QList<DomainSpec> registry{sqmDomain(), systemDomain()};
    return registry;
}

const DomainSpec *domain(const QString &id)
{
    for (const DomainSpec &spec : domains()) {
        if (spec.id == id)
            return &spec;
    }
    return nullptr;
}

} // namespace Config
