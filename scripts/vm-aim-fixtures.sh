#!/usr/bin/env bash
# Aim-surface fixtures on a dev VM: the lab VM has no MBIM modem, so the
# modemaim rpcd plugin answers empty metrics and the aim view has nothing to
# hold a peak on. This installs a stand-in /usr/bin/mbimcli that answers the four
# queries the plugin makes, with numbers that move every second, plus the
# curated eNB -> ACMA site row that lets `modemaim status` map the fake serving
# cell onto a real site in the extract.
#
# Honest scope: the RF numbers are a fixture, not a measurement. Everything
# below the mbimcli boundary is the app's real rpcd script, real JSON shapes and
# the real ACMA extract.
#
#   SSHP=10022 scripts/vm-aim-fixtures.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SSHP=${SSHP:-10022}
# The nearest ACMA site to the bench origin (-33.0, 151.6): 1.35 km, bearing
# 30 deg. Its eNB id is invented; the ECI the stub reports is enb << 8 | pci.
SITE=${SITE:-10021260}
ENB=${ENB:-546000}
SSHOPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
         -o ConnectTimeout=5 -p "$SSHP")

ssh "${SSHOPTS[@]}" root@127.0.0.1 'sh -s' <<REMOTE
set -e
mkdir -p /data/modemaim

# The extract has to know this site for the client to plot the serving cell.
grep -q "\"site\":\"${SITE}\"" /usr/share/modemaim/towers.json \
    || { echo "site ${SITE} is not in the extract on this VM" >&2; exit 1; }

printf '%s,${SITE},confirmed,ta\n' "${ENB}" > /data/modemaim/enb-acma.csv

if [ -e /usr/bin/mbimcli ] && ! grep -q 'm17 fixture' /usr/bin/mbimcli 2>/dev/null; then
    cp /usr/bin/mbimcli /usr/bin/mbimcli.real-m17
fi

cat > /usr/bin/mbimcli <<'STUB'
#!/bin/sh
# m17 fixture: MBIM stand-in for a lab VM with no modem. One LTE cell, metrics
# that swing over a minute so a peak-hold display has something to hold.
ENB=${ENB:-546000}
PCI=42
T=\$(date +%s)
RSRP=\$(( -98 + (T * 7) % 13 ))
RSRQ=\$(( -13 + (T * 3) % 7 ))
SINR=\$(( 6 + (T * 5) % 11 ))
RSSI=\$(( RSRP - 30 ))
ECI=\$(( ENB * 256 + PCI ))
sleep 0.05
case "\$*" in
    *--atds-query-signal*)
        cat <<EOF
    RSSI: \${RSSI} dBm
    RSRP: \${RSRP} dBm
    RSRQ: \${RSRQ} dBm
    RSSNR: \${SINR} dB
EOF
        ;;
    *--ms-query-base-stations-info*)
        cat <<EOF
LTE Serving cell:
        Provider id: 50502
        Cell id: \${ECI}
        TAC: 1234
        Physical cell id: \${PCI}
        EARFCN: 6400
        RSRP: \${RSRP} dBm
        RSRQ: \${RSRQ} dBm
        Timing advance: 12
Neighboring LTE cells:
        Neighboring cell:
                Physical cell id: 43
                EARFCN: 6400
                RSRP: \$(( RSRP - 9 )) dBm
                RSRQ: \$(( RSRQ - 2 )) dBm
EOF
        ;;
    *--query-registration-state*)
        cat <<EOF
Register state: 'registered'
Register mode: 'automatic'
Provider name: 'OPTUS'
Available data classes: 'lte'
EOF
        ;;
    *--query-packet-service-state*)
        cat <<EOF
Packet service state: 'attached'
EOF
        ;;
    *)
        echo "mbimcli: unsupported query" >&2
        exit 1
        ;;
esac
STUB
chmod 755 /usr/bin/mbimcli

echo "fixture: site ${SITE}, eNB ${ENB}, mbimcli stand-in installed"
REMOTE

echo "--- modemaim status after fixtures"
ssh "${SSHOPTS[@]}" root@127.0.0.1 \
    'ubus call modemaim signal; ubus call modemaim status' | head -40
