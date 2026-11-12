// SPDX-FileCopyrightText: 2026 the router-console authors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DomainRegistry.h"

namespace Config {

QString DomainSpec::sourceLabel() const
{
    if (kind == Kind::FrrConfig)
        return filePath;
    return QStringLiteral("/etc/config/%1").arg(config);
}

QStringList DomainSpec::sectionTypes() const
{
    if (!sections.isEmpty()) {
        QStringList types;
        types.reserve(sections.size());
        for (const SectionSpec &section : sections)
            types.append(section.type);
        return types;
    }
    return sectionType.isEmpty() ? QStringList() : QStringList{sectionType};
}

const SectionSpec *DomainSpec::sectionFor(const QString &type) const
{
    for (const SectionSpec &section : sections) {
        if (section.type == type)
            return &section;
    }
    return nullptr;
}

QStringList DomainSpec::summaryOptionsFor(const QString &type) const
{
    if (const SectionSpec *section = sectionFor(type))
        return section->summaryOptions;
    return summaryOptions;
}

QList<FieldSpec> DomainSpec::fieldsFor(const QString &type) const
{
    if (const SectionSpec *section = sectionFor(type))
        return section->fields;
    return fields;
}

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
        {QStringLiteral("interface"), QStringLiteral("Interface"), FieldKind::InterfaceRef, {}, {},
         false},
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

// /etc/config/network on the VM holds five `config interface` sections
// (loopback, lan, wan, wan6, lan2) whose options are device, proto, ipaddr and
// ip6assign; the proto values in the file are static, dhcp and dhcpv6.
DomainSpec networkDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("network");
    spec.title = QStringLiteral("Network");
    spec.config = QStringLiteral("network");
    spec.sectionType = QStringLiteral("interface");
    spec.summaryOptions = {QStringLiteral("device"), QStringLiteral("proto"),
                           QStringLiteral("ipaddr")};
    spec.fields = {
        {QStringLiteral("device"), QStringLiteral("Device"), FieldKind::InterfaceRef, {}, {},
         false},
        {QStringLiteral("proto"), QStringLiteral("Protocol"), FieldKind::Choice, {},
         {QStringLiteral("static"), QStringLiteral("dhcp"), QStringLiteral("dhcpv6")}, false},
        {QStringLiteral("ipaddr"), QStringLiteral("IPv4 address"), FieldKind::Text,
         QStringLiteral("addr/prefix"), {}, false},
        {QStringLiteral("ip6assign"), QStringLiteral("IPv6 prefix length"), FieldKind::Integer,
         QStringLiteral("bits"), {}, false},
    };
    return spec;
}

// /etc/config/wireless does not exist on the dev VM: no wireless package is
// installed, so uci answers "not found" and the editor shows an empty domain.
// The option names are the UCI wifi-iface schema OpenWrt writes when a radio is
// present (the file the wireless stack generates), not values read from this VM.
DomainSpec wirelessDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("wireless");
    spec.title = QStringLiteral("Wireless");
    spec.config = QStringLiteral("wireless");
    spec.sectionType = QStringLiteral("wifi-iface");
    spec.summaryOptions = {QStringLiteral("device"), QStringLiteral("mode"),
                           QStringLiteral("ssid")};
    spec.fields = {
        {QStringLiteral("device"), QStringLiteral("Radio"), FieldKind::Text,
         QStringLiteral("radio0"), {}, false},
        {QStringLiteral("mode"), QStringLiteral("Mode"), FieldKind::Choice, {},
         {QStringLiteral("ap"), QStringLiteral("sta"), QStringLiteral("mesh")}, false},
        {QStringLiteral("ssid"), QStringLiteral("SSID"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("network"), QStringLiteral("Attach to network"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("encryption"), QStringLiteral("Encryption"), FieldKind::Choice, {},
         {QStringLiteral("none"), QStringLiteral("psk2"), QStringLiteral("sae"),
          QStringLiteral("psk-mixed")},
         false},
        {QStringLiteral("key"), QStringLiteral("Key"), FieldKind::Text, {}, {}, false},
    };
    return spec;
}

// /etc/config/firewall has two `config zone` sections (cfg02dc81 = lan,
// cfg03dc81 = wan), nine `config rule` sections and no redirect on this VM.
// Zone/rule/redirect are three section types of one domain: the editor shows
// them in one list and picks the field set from the section's type. The rule
// and redirect field names and target values are fw4's own (parse_rule /
// parse_redirect in /usr/share/ucode/fw4.uc); there is no `chain` option, so
// the WinBox chain row is derived from src/dest and read-only.
DomainSpec firewallDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("firewall");
    spec.title = QStringLiteral("Firewall");
    spec.config = QStringLiteral("firewall");
    spec.sectionType = QStringLiteral("zone");
    spec.summaryOptions = {QStringLiteral("name"), QStringLiteral("input"),
                           QStringLiteral("forward")};
    spec.fields = {
        {QStringLiteral("name"), QStringLiteral("Zone"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("input"), QStringLiteral("Input"), FieldKind::Choice, {},
         {QStringLiteral("ACCEPT"), QStringLiteral("REJECT"), QStringLiteral("DROP")}, false},
        {QStringLiteral("output"), QStringLiteral("Output"), FieldKind::Choice, {},
         {QStringLiteral("ACCEPT"), QStringLiteral("REJECT"), QStringLiteral("DROP")}, false},
        {QStringLiteral("forward"), QStringLiteral("Forward"), FieldKind::Choice, {},
         {QStringLiteral("ACCEPT"), QStringLiteral("REJECT"), QStringLiteral("DROP")}, false},
        {QStringLiteral("masq"), QStringLiteral("Masquerade"), FieldKind::Boolean, {}, {}, false},
        {QStringLiteral("mtu_fix"), QStringLiteral("MTU fix"), FieldKind::Boolean, {}, {}, false},
        {QStringLiteral("network"), QStringLiteral("Interfaces"), FieldKind::InterfaceRef, {}, {},
         false, {}, true},
    };

    SectionSpec zones;
    zones.type = QStringLiteral("zone");
    zones.summaryOptions = spec.summaryOptions;
    zones.fields = spec.fields;

    SectionSpec rules;
    rules.type = QStringLiteral("rule");
    rules.summaryOptions = {QStringLiteral("name"), QStringLiteral("target"),
                            QStringLiteral("proto"), QStringLiteral("dest_port")};
    rules.fields = {
        {QStringLiteral("name"), QStringLiteral("Rule name"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("enabled"), QStringLiteral("Enabled"), FieldKind::Boolean, {}, {},
         false, QStringLiteral("1")},
        {QStringLiteral("chain"), QStringLiteral("Chain (WinBox view)"), FieldKind::Chain, {},
         {QStringLiteral("input"), QStringLiteral("forward"), QStringLiteral("output")}, true},
        {QStringLiteral("target"), QStringLiteral("Action"), FieldKind::Choice, {},
         {QStringLiteral("ACCEPT"), QStringLiteral("DROP"), QStringLiteral("REJECT")}, false},
        {QStringLiteral("proto"), QStringLiteral("Protocol"), FieldKind::Choice, {},
         {QStringLiteral("tcp"), QStringLiteral("udp"), QStringLiteral("tcpudp"),
          QStringLiteral("icmp"), QStringLiteral("icmpv6"), QStringLiteral("igmp"),
          QStringLiteral("esp"), QStringLiteral("ah"), QStringLiteral("all")},
         false},
        {QStringLiteral("src"), QStringLiteral("Source zone"), FieldKind::ZoneRef, {}, {}, false},
        {QStringLiteral("dest"), QStringLiteral("Destination zone"), FieldKind::ZoneRef, {}, {},
         false},
        {QStringLiteral("src_ip"), QStringLiteral("Source address"), FieldKind::Text, {}, {},
         false, {}, true},
        {QStringLiteral("dest_ip"), QStringLiteral("Destination address"), FieldKind::Text, {}, {},
         false, {}, true},
        {QStringLiteral("src_port"), QStringLiteral("Source port"), FieldKind::Text, {}, {}, false,
         {}, true},
        {QStringLiteral("dest_port"), QStringLiteral("Destination port"), FieldKind::Text, {}, {},
         false, {}, true},
        {QStringLiteral("icmp_type"), QStringLiteral("ICMP type"), FieldKind::Text, {}, {}, false,
         {}, true},
        {QStringLiteral("family"), QStringLiteral("Family"), FieldKind::Choice, {},
         {QStringLiteral("any"), QStringLiteral("ipv4"), QStringLiteral("ipv6")}, false},
        {QStringLiteral("limit"), QStringLiteral("Rate limit"), FieldKind::Text, {}, {}, false},
    };

    SectionSpec redirects;
    redirects.type = QStringLiteral("redirect");
    redirects.summaryOptions = {QStringLiteral("name"), QStringLiteral("target"),
                                QStringLiteral("proto"), QStringLiteral("src_dport"),
                                QStringLiteral("dest_ip"), QStringLiteral("dest_port")};
    redirects.fields = {
        {QStringLiteral("name"), QStringLiteral("Redirect name"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("enabled"), QStringLiteral("Enabled"), FieldKind::Boolean, {}, {},
         false, QStringLiteral("1")},
        {QStringLiteral("target"), QStringLiteral("Action"), FieldKind::Choice, {},
         {QStringLiteral("DNAT"), QStringLiteral("SNAT")}, false},
        {QStringLiteral("proto"), QStringLiteral("Protocol"), FieldKind::Choice, {},
         {QStringLiteral("tcp"), QStringLiteral("udp"), QStringLiteral("tcpudp")}, false},
        {QStringLiteral("src"), QStringLiteral("Source zone"), FieldKind::ZoneRef, {}, {}, false},
        {QStringLiteral("dest"), QStringLiteral("Destination zone"), FieldKind::ZoneRef, {}, {},
         false},
        {QStringLiteral("src_dport"), QStringLiteral("External port"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("dest_ip"), QStringLiteral("Internal address"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("dest_port"), QStringLiteral("Internal port"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("src_ip"), QStringLiteral("Source address"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("src_port"), QStringLiteral("Source port"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("family"), QStringLiteral("Family"), FieldKind::Choice, {},
         {QStringLiteral("any"), QStringLiteral("ipv4"), QStringLiteral("ipv6")}, false},
        {QStringLiteral("reflection"), QStringLiteral("NAT reflection"), FieldKind::Boolean, {}, {},
         false},
    };

    spec.sections = {zones, rules, redirects};
    return spec;
}

// /etc/config/dhcp on the VM has two `config dhcp` sections (lan, wan) with
// interface, start, limit, leasetime, dhcpv4, dhcpv6, ra and ignore. The pool
// states are the values odhcpd/dnsmasq accept for those options.
DomainSpec dhcpDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("dhcp");
    spec.title = QStringLiteral("DHCP / DNS");
    spec.config = QStringLiteral("dhcp");
    spec.sectionType = QStringLiteral("dhcp");
    spec.summaryOptions = {QStringLiteral("interface"), QStringLiteral("start"),
                           QStringLiteral("limit"), QStringLiteral("leasetime")};
    spec.fields = {
        {QStringLiteral("interface"), QStringLiteral("Interface"), FieldKind::InterfaceRef, {}, {},
         false},
        {QStringLiteral("start"), QStringLiteral("Pool start"), FieldKind::Integer, {}, {}, false},
        {QStringLiteral("limit"), QStringLiteral("Pool size"), FieldKind::Integer, {}, {}, false},
        {QStringLiteral("leasetime"), QStringLiteral("Lease time"), FieldKind::Text,
         QStringLiteral("h"), {}, false},
        {QStringLiteral("dhcpv4"), QStringLiteral("DHCPv4"), FieldKind::Choice, {},
         {QStringLiteral("server"), QStringLiteral("relay"), QStringLiteral("disabled")}, false},
        {QStringLiteral("dhcpv6"), QStringLiteral("DHCPv6"), FieldKind::Choice, {},
         {QStringLiteral("server"), QStringLiteral("relay"), QStringLiteral("disabled")}, false},
        {QStringLiteral("ra"), QStringLiteral("Router advertisements"), FieldKind::Choice, {},
         {QStringLiteral("server"), QStringLiteral("relay"), QStringLiteral("disabled")}, false},
        {QStringLiteral("ignore"), QStringLiteral("Ignore this interface"), FieldKind::Boolean,
         {}, {}, false},
    };
    return spec;
}

// frr.conf is a text grammar, not UCI: /etc/frr/frr.conf holds every daemon's
// config in one file (measured on FRR 10.4.1, docs/frr-routing.md). The fields
// here are the statements the editor offers even when the block does not carry
// them yet; every other statement in a block is listed from the file itself.
// `option` is the statement prefix, so the editor rewrites "<prefix> <value>".
DomainSpec frrDomain()
{
    DomainSpec spec;
    spec.id = QStringLiteral("frr");
    spec.title = QStringLiteral("Routing (FRR)");
    spec.config = QStringLiteral("frr");
    spec.kind = DomainSpec::Kind::FrrConfig;
    spec.filePath = QStringLiteral("/etc/frr/frr.conf");
    spec.sectionType = QStringLiteral("router ospf");

    SectionSpec global;
    global.type = QStringLiteral("global");
    global.summaryOptions = {QStringLiteral("hostname")};
    global.fields = {
        {QStringLiteral("hostname"), QStringLiteral("Hostname"), FieldKind::Text, {}, {}, false},
        {QStringLiteral("log syslog"), QStringLiteral("Syslog level"), FieldKind::Choice, {},
         {QStringLiteral("informational"), QStringLiteral("notifications"), QStringLiteral("debugging")},
         false},
    };

    SectionSpec ospf;
    ospf.type = QStringLiteral("router ospf");
    ospf.summaryOptions = {QStringLiteral("ospf router-id")};
    ospf.fields = {
        {QStringLiteral("ospf router-id"), QStringLiteral("Router ID"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("ospf log-adjacency-changes"),
         QStringLiteral("Log adjacency changes"), FieldKind::Text, {}, {}, false},
    };

    SectionSpec bgp;
    bgp.type = QStringLiteral("router bgp");
    bgp.summaryOptions = {QStringLiteral("bgp router-id")};
    bgp.fields = {
        {QStringLiteral("bgp router-id"), QStringLiteral("Router ID"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("network"), QStringLiteral("Advertised network"), FieldKind::Text,
         QStringLiteral("/len"), {}, false},
    };

    SectionSpec interface;
    interface.type = QStringLiteral("interface");
    interface.summaryOptions = {QStringLiteral("ip ospf area")};
    interface.fields = {
        {QStringLiteral("ip ospf area"), QStringLiteral("OSPF area"), FieldKind::Text, {}, {},
         false},
        {QStringLiteral("ip ospf cost"), QStringLiteral("OSPF cost"), FieldKind::Integer, {}, {},
         false},
        {QStringLiteral("ip ospf priority"), QStringLiteral("OSPF priority"), FieldKind::Integer,
         {}, {}, false},
        {QStringLiteral("ip ospf network"), QStringLiteral("Network type"), FieldKind::Choice, {},
         {QStringLiteral("point-to-point"), QStringLiteral("broadcast"),
          QStringLiteral("non-broadcast"), QStringLiteral("point-to-multipoint")},
         false},
    };

    SectionSpec line;
    line.type = QStringLiteral("line");

    spec.sections = {global, ospf, bgp, interface, line};
    return spec;
}

} // namespace

const QList<DomainSpec> &domains()
{
    static const QList<DomainSpec> registry{sqmDomain(),  systemDomain(), networkDomain(),
                                            wirelessDomain(), firewallDomain(),
                                            dhcpDomain(), frrDomain()};
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
