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
        {QStringLiteral("device"), QStringLiteral("Device"), FieldKind::Text, {}, {}, false},
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
// cfg03dc81 = wan) carrying name, network, input, output, forward, masq and
// mtu_fix; ACCEPT/REJECT/DROP are the policy values the file uses.
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
    };
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
        {QStringLiteral("interface"), QStringLiteral("Interface"), FieldKind::Text, {}, {}, false},
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

} // namespace

const QList<DomainSpec> &domains()
{
    static const QList<DomainSpec> registry{sqmDomain(),  systemDomain(), networkDomain(),
                                            wirelessDomain(), firewallDomain(),
                                            dhcpDomain()};
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
