#!/usr/bin/env bash
# Install the REAL custom LuCI apps into the dev VM, replacing the milestone-6
# stand-ins installed by scripts/vm-mock-apps.sh:
#
#   luci-app-rist2rist  a real apk built from the package's own Makefile in the
#                       25.12.6 SDK: /usr/bin/rist2rist (librist + mbedtls linked
#                       in), its procd init, its UCI config, its rpcd object and
#                       ACL. The bridge then runs for real on UDP 5000.
#   nettime         a real apk: /usr/bin/nettime (GstNetTime provider)
#                       and its procd init; it serves CLOCK_REALTIME on UDP 5637.
#   luci-app-modemaim   deployed through the app's OWN path
#                       (cellular/router/deploy-modemaim.sh) plus the package's
#                       UCI config. The app ships no Makefile, so there is
#                       nothing to build; the rpcd plugin, its ACL and the real
#                       ACMA extract are what the view binds to.
#
# The mock script is untouched. To go back:
#   apk del luci-app-rist2rist nettime && scripts/vm-mock-apps.sh
#
#   SSHP=10022 scripts/vm-real-apps.sh
#   RIST_APK=...  NETTIME_APK=...  CELLULAR=... to override source locations.
set -euo pipefail
cd "$(dirname "$0")/.."

SSHP=${SSHP:-10022}
SDK26=${SDK26:-/home/pat/code/openwrt/openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl.Linux-x86_64}
# nettime was last built in the 25.12.4 SDK; its source has not changed
# since, so that apk is kept in ~/code/openwrt/artifacts (see docs/plugins.md).
ARTIFACTS=${ARTIFACTS:-/home/pat/code/openwrt/artifacts}
RIST_APK=${RIST_APK:-}
NETTIME_APK=${NETTIME_APK:-}
if [ -z "$RIST_APK" ]; then
    RIST_APK=$(ls "$SDK26"/bin/packages/x86_64/base/luci-app-rist2rist-*.apk 2>/dev/null | head -n 1 || true)
fi
if [ -z "$NETTIME_APK" ]; then
    NETTIME_APK=$(ls "$SDK26"/bin/packages/x86_64/base/nettime-*.apk 2>/dev/null | head -n 1 || true)
    [ -n "$NETTIME_APK" ] || NETTIME_APK=$(ls "$ARTIFACTS"/nettime-*.apk 2>/dev/null | head -n 1 || true)
fi
CELLULAR=${CELLULAR:-/home/pat/workspace/cellular}

SSHOPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
         -o ConnectTimeout=5 -p "$SSHP")
VM="root@127.0.0.1"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

ssh_vm() { ssh "${SSHOPTS[@]}" "$VM" "$@"; }
# The VM's dropbear has no sftp-server, so scp is not available.
put() { ssh_vm "cat > '$2'" < "$1"; }

[ -n "${RIST_APK:-}" ] && [ -f "$RIST_APK" ] || { echo "no rist2rist apk (RIST_APK)" >&2; exit 1; }
[ -n "${NETTIME_APK:-}" ] && [ -f "$NETTIME_APK" ] || { echo "no nettime apk (NETTIME_APK)" >&2; exit 1; }
echo "apps: $(basename "$RIST_APK"), $(basename "$NETTIME_APK")"

# --- 1. stop the mocks that the packages replace ---------------------------
ssh_vm '/etc/init.d/rist2rist stop 2>/dev/null; /etc/init.d/nettime stop 2>/dev/null; true'

# --- 2. packages -----------------------------------------------------------
put "$RIST_APK" /tmp/rist2rist.apk
put "$NETTIME_APK" /tmp/nettime.apk

echo "--- apk add"
# --force-overwrite: the mock script's files at these paths belong to no
# package. apk still keeps its own copy of a conffile it does not own as
# <file>.apk-new, so those are adopted below — otherwise the mock's init and
# config would keep winning.
ssh_vm 'apk add --allow-untrusted --force-overwrite /tmp/rist2rist.apk /tmp/nettime.apk' \
    2>&1 | grep -vE '^WARNING: opening from cache' || true

echo "--- adopt the packaged conffiles"
ssh_vm '
for f in $(find /etc -name "*.apk-new"); do mv -f "$f" "${f%.apk-new}"; echo "  ${f%.apk-new}"; done
chmod +x /etc/init.d/rist2rist /etc/init.d/nettime 2>/dev/null || true
'

# --- 3. rist2rist: a real, running bridge ----------------------------------
# The packaged config is enabled '0' with no destination, and the init returns
# before procd starts anything when there are no output URLs. One destination in
# TEST-NET-3 gives the daemon something to emit; nothing answers there.
echo "--- rist2rist config + start"
ssh_vm '
uci -q revert rist2rist 2>/dev/null
while uci -q delete rist2rist.@destination[0]; do :; done
uci add rist2rist destination >/dev/null
uci set rist2rist.@destination[-1].address="rist://203.0.113.10:6000"
uci set rist2rist.main.enabled="1"
uci commit rist2rist
/etc/init.d/rist2rist enable >/dev/null 2>&1
/etc/init.d/rist2rist restart >/dev/null 2>&1
'
sleep 4

# --- 4. modemaim through its own deploy path -------------------------------
# deploy-modemaim.sh is the app's real installer: content-hashed view files, the
# rpcd plugin, its ACL, and the real ACMA extract (cellular/data/towers.json) at
# both /www/luci-static/resources/modemaim/towers.json and /usr/share/modemaim/.
# It talks to ssh without a port, so it runs with a PATH shim for the VM's
# forwarded port, and it writes into /usr/share/modemaim before creating it.
echo "--- modemaim: deploy-modemaim.sh"
ssh_vm 'mkdir -p /usr/share/modemaim /www/luci-static/resources/view/modemaim /www/luci-static/resources/modemaim /www/cgi-bin /data/modemaim'
mkdir -p "$TMP/bin"
cat > "$TMP/bin/ssh" <<EOF
#!/bin/sh
exec /usr/bin/ssh -o BatchMode=yes -o StrictHostKeyChecking=no \\
  -o UserKnownHostsFile=/dev/null -p $SSHP "\$@"
EOF
chmod +x "$TMP/bin/ssh"
PATH="$TMP/bin:$PATH" ROUTER="$VM" sh "$CELLULAR/router/deploy-modemaim.sh"

# The deploy script installs no UCI config; the app's own package file is it.
echo "--- modemaim config + origin"
put "$CELLULAR/luci-app-modemaim/root/etc/config/modemaim" /etc/config/modemaim
# A fixed origin inside the extract's bounding box, set through the app's own
# method: without it the view cannot compute distance/bearing, and a bench VM
# has no GPS to read.
ssh_vm 'ubus call modemaim setorigin "{\"lat\":-33.0,\"lon\":151.6}"'

# --- 5. nettime: the real provider, plus the status shim ---------------
echo "--- nettime service"
ssh_vm '/etc/init.d/nettime enable >/dev/null 2>&1; /etc/init.d/nettime restart >/dev/null 2>&1' || true

# The packaged app has no rpcd object: its surface is the UDP clock and procd.
# This shim serves the `nettime` object name the view declares, reporting
# only what the running daemon can be measured for (see the file's header).
put scripts/vm-fixtures/rpcd/nettime-shim /usr/libexec/rpcd/nettime
ssh_vm 'chmod +x /usr/libexec/rpcd/nettime; /etc/init.d/rpcd restart >/dev/null 2>&1'
sleep 3

# --- 6. raw surfaces -------------------------------------------------------
echo "--- rist2rist: procd (the LuCI page reads this same call)"
ssh_vm 'ubus call service list "{\"name\":\"rist2rist\"}" | head -c 700; echo'
echo "--- rist2rist: UCI"
ssh_vm 'ubus call uci get "{\"config\":\"rist2rist\"}" | head -c 400; echo'
echo "--- rist2rist: rpcd object refuses without the bridge's pair token"
ssh_vm 'ubus call rist2rist status 2>&1 || true'
echo "--- rist2rist / nettime: UDP sockets"
ssh_vm 'netstat -lnup | grep -E "rist2rist|nettime" || true'
echo "--- nettime: provider log"
ssh_vm 'logread -e "nettime: serving" | tail -2'
echo "--- nettime: status shim"
ssh_vm 'ubus call nettime status'
echo "--- modemaim: status (no modem -> absent signal, the real answer)"
ssh_vm 'ubus call modemaim status'
echo "--- modemaim: origin"
ssh_vm 'ubus call modemaim origin'
echo "--- modemaim: real ACMA extract over HTTP"
ssh_vm 'wget -q -O - http://127.0.0.1/luci-static/resources/modemaim/towers.json | head -c 200; echo'
echo
echo "back to the mocks: apk del luci-app-rist2rist nettime && scripts/vm-mock-apps.sh"
