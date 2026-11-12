enable_testing()

# The VM tests drive whatever router answers here; a second instance (another
# worktree's private lab VM) is selected with -DRC_TEST_HOST=127.0.0.1:20443.
set(RC_TEST_HOST "127.0.0.1:10443" CACHE STRING "router host[:port] for the VM tests")
set(RC_TEST_SSH_PORT "10022" CACHE STRING "router ssh forward of the same VM")

add_executable(connect-smoke connect_smoke.cpp)
target_link_libraries(connect-smoke PRIVATE router-console-core)

# Runs against the dev VM (docs/dev-vm-recipe.md); skipped when it is not up.
add_test(NAME connect-smoke
         COMMAND connect-smoke --host ${RC_TEST_HOST} --user root --password root --accept-new-cert)
set_tests_properties(connect-smoke PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 40)

# Star check for the graphs half: per-interface byte counters must grow across
# >= 5 consecutive 1 Hz samples while traffic runs inside the VM.
add_executable(interfaces-traffic interfaces_traffic.cpp)
target_link_libraries(interfaces-traffic PRIVATE router-console-core)

add_test(NAME interfaces-traffic
         COMMAND interfaces-traffic --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --interface br-lan --samples 6)
set_tests_properties(interfaces-traffic PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 90)
add_executable(discovery-smoke discovery_smoke.cpp)
target_link_libraries(discovery-smoke PRIVATE router-console-core)
target_include_directories(discovery-smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Discovery on the lab segment (192.168.78.0/24, see docs/dev-vm-recipe.md);
# skipped when there is no multicast interface.
add_test(NAME discovery-smoke
         COMMAND discovery-smoke --hostname openwrt-dev --address 192.168.78.1 --timeout 10000)
set_tests_properties(discovery-smoke PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 30)

# The stated requirement: after the VM boots, a clean client start finds the
# router within 10 s. This one reboots the dev VM, so it carries the vm-boot
# label: `ctest -LE vm-boot` for the normal run, `ctest -L vm-boot` for this.
add_test(NAME discovery-after-boot
         COMMAND ${CMAKE_SOURCE_DIR}/tests/after-boot-discovery.sh $<TARGET_FILE:discovery-smoke>)
set_tests_properties(discovery-after-boot PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 300
                     LABELS vm-boot)

# Star check for the config half: staged SQM edit -> changes list -> apply ->
# value persisted -> stage + revert leaves the config unchanged.
add_executable(uci-sqm-staged uci_sqm_staged.cpp)
target_link_libraries(uci-sqm-staged PRIVATE router-console-core)

add_test(NAME uci-sqm-staged
         COMMAND uci-sqm-staged --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --domain sqm --section eth1 --option download)
set_tests_properties(uci-sqm-staged PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)

# Same flow with `uci apply` followed by `system reboot`: the committed value has
# to survive a real VM boot. Labelled vm-boot because it reboots the VM.
add_test(NAME uci-sqm-staged-reboot
         COMMAND uci-sqm-staged --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --domain sqm --section eth1 --option download --reboot)
set_tests_properties(uci-sqm-staged-reboot PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 420
                     LABELS vm-boot)

# Star check for requirement 3 (config across system, network, wireless,
# firewall, SQM, DHCP): one test per core domain. Each reads the package, proves
# the registry's fields name options the router really stores, stages one
# reversible change and checks the staged-changes list. network and firewall
# stop at stage + revert (an apply would rewrite the VM's live addressing and
# filtering); dhcp applies and restores its baseline; wireless is empty on this
# VM (no wireless stack) and must load as an empty domain.
add_executable(uci-domain-staged uci_domain_staged.cpp)
target_link_libraries(uci-domain-staged PRIVATE router-console-core)

foreach(domain network wireless firewall dhcp)
    add_test(NAME uci-${domain}-staged
             COMMAND uci-domain-staged --host ${RC_TEST_HOST} --user root --password root
                     --accept-new-cert --domain ${domain})
    set_tests_properties(uci-${domain}-staged PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)
endforeach()

# Firewall rules + NAT: the domain's second half. Stages one redirect section
# (uci add, never applied), one change on a real filter rule and one on the
# redirect, asserts the pending list, then reverts and proves the baseline.
add_executable(firewall-nat-staged firewall_nat_staged.cpp)
target_link_libraries(firewall-nat-staged PRIVATE router-console-core)

add_test(NAME firewall-nat-staged
         COMMAND firewall-nat-staged --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert)
set_tests_properties(firewall-nat-staged PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)

# --- Milestone 6: plugin interface ------------------------------------------
# Manifest contract, no router needed: the three app plugins plus the hello stub
# parse, declare the sources they bind to, and a broken manifest is rejected
# with its reason.
add_executable(plugin-loading plugin_loading.cpp)
target_link_libraries(plugin-loading PRIVATE router-console-core)

add_test(NAME plugin-loading
         COMMAND plugin-loading --dir ${CMAKE_SOURCE_DIR}/plugins)
set_tests_properties(plugin-loading PROPERTIES TIMEOUT 30)

# Requirement: a stub app view added entirely through the plugin interface
# appears in the client tree with zero diffs to core files. The stub is written
# to a temp plugin root and loaded by the same loader the tree model uses; the
# git check fails if the working tree carries uncommitted core changes, because
# then "zero diffs" cannot be asserted.
add_test(NAME plugin-stub
         COMMAND ${CMAKE_SOURCE_DIR}/tests/plugin-stub.sh $<TARGET_FILE:plugin-loading>
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(plugin-stub PROPERTIES TIMEOUT 60)

if(WIN32)
    # The stub proof drives a bash script; Windows CI proves the build, not
    # shell-based repo hygiene, so the check stays on the platforms that have
    # the shell it needs.
    set_tests_properties(plugin-stub PROPERTIES DISABLED TRUE)
endif()

# One data test per custom app, against the dev VM (fixtures installed by
# scripts/vm-mock-apps.sh). Each opens the plugin session the window opens and
# asserts the view model received the app's own data shape.
add_executable(plugin-data plugin_data.cpp)
target_link_libraries(plugin-data PRIVATE router-console-core)

foreach(plugin rist2rist modemaim nettime)
    add_test(NAME plugin-data-${plugin}
             COMMAND plugin-data --plugin ${plugin} --host ${RC_TEST_HOST} --user root
                     --password root --accept-new-cert)
    set_tests_properties(plugin-data-${plugin} PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)
endforeach()
# Tool tier: one check per window, all against the dev VM. Each exits 3 when the
# VM is not reachable, so a run without the VM skips instead of failing.
add_executable(tools-ping tools_ping.cpp)
target_link_libraries(tools-ping PRIVATE router-console-core)
add_test(NAME tools-ping
         COMMAND tools-ping --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --target 10.0.2.3 --count 3)
set_tests_properties(tools-ping PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)

add_executable(tools-traceroute tools_traceroute.cpp)
target_link_libraries(tools-traceroute PRIVATE router-console-core)
add_test(NAME tools-traceroute
         COMMAND tools-traceroute --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --target 10.0.2.3 --max-hops 6)
set_tests_properties(tools-traceroute PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)

add_executable(tools-portscan tools_portscan.cpp)
target_link_libraries(tools-portscan PRIVATE router-console-core)
add_test(NAME tools-portscan
         COMMAND tools-portscan --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --target 127.0.0.1 --ports 22,9999
                 --expect-open 22 --expect-closed 9999)
set_tests_properties(tools-portscan PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)

# The flagship: per-host attribution of a live ssh stream from the host.
add_executable(torch-traffic torch_traffic.cpp)
target_link_libraries(torch-traffic PRIVATE router-console-core)
add_test(NAME torch-traffic
         COMMAND torch-traffic --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --ssh-port ${RC_TEST_SSH_PORT}
                 --expect-host 192.168.77.2 --samples 6 --min-rate 100000)
set_tests_properties(torch-traffic PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 90)

add_executable(files-roundtrip files_roundtrip.cpp)
target_link_libraries(files-roundtrip PRIVATE router-console-core)
add_test(NAME files-roundtrip
         COMMAND files-roundtrip --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --list-dir /etc/config)
set_tests_properties(files-roundtrip PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 90)

# Sysupgrade wiring without flashing: a random 4 KB file at /tmp/firmware.bin
# must be rejected by rpc-sys upgrade_test.
add_executable(sysupgrade-check sysupgrade_check.cpp)
target_link_libraries(sysupgrade-check PRIVATE router-console-core)
add_test(NAME sysupgrade-check
         COMMAND sysupgrade-check --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert)
set_tests_properties(sysupgrade-check PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)

# Enables sqm for the duration and restores the baseline; daemon work, so the
# timeout allows the restart cycle.
add_executable(sqm-stats sqm_stats.cpp)
target_link_libraries(sqm-stats PRIVATE router-console-core)
add_test(NAME sqm-stats
         COMMAND sqm-stats --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --section eth1 --device eth1)
set_tests_properties(sqm-stats PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 180)

# --- Milestone 12: daily-driver trio ----------------------------------------
# Container, log-line and lease-JSON shapes without a router.
add_executable(m12-archive-codec m12_archive_codec.cpp)
target_link_libraries(m12-archive-codec PRIVATE router-console-core)
add_test(NAME m12-archive-codec COMMAND m12-archive-codec)
set_tests_properties(m12-archive-codec PROPERTIES TIMEOUT 30)

# Log viewer: `logread` window parsed, poller picks up a line the router writes,
# pause freezes the tail and the filter narrows rows.
add_executable(m12-log-view m12_log_view.cpp)
target_link_libraries(m12-log-view PRIVATE router-console-core)
add_test(NAME m12-log-view
         COMMAND m12-log-view --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --ssh-port ${RC_TEST_SSH_PORT} --min-entries 20)
set_tests_properties(m12-log-view PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 90)

# DHCP leases: live-lease surface answers, and the reservation that
# scripts/vm-m12-fixtures.sh installs is in the table with its address and mac.
add_executable(m12-dhcp-leases m12_dhcp_leases.cpp)
target_link_libraries(m12-dhcp-leases PRIVATE router-console-core)
add_test(NAME m12-dhcp-leases
         COMMAND m12-dhcp-leases --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert --reserve-hostname rc-m12-reserved
                 --reserve-address 192.168.77.240 --reserve-mac 52:54:00:12:34:56)
set_tests_properties(m12-dhcp-leases PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 60)

# Config backup: archive of /etc/config + manifest, and restore staged under
# /tmp/router-console-restore with a byte-identical round trip.
add_executable(m12-config-backup m12_config_backup.cpp)
target_link_libraries(m12-config-backup PRIVATE router-console-core)
add_test(NAME m12-config-backup
         COMMAND m12-config-backup --host ${RC_TEST_HOST} --user root --password root
                 --accept-new-cert)
set_tests_properties(m12-config-backup PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)
# --- Milestone 13: routing ---------------------------------------------------
# One check per routing surface. Each needs an FRR-enabled router (frr, ospf,
# bgpd, staticd installed; bgpd/ospfd=yes in /etc/frr/daemons), so a VM without
# the packages skips on the first assertion instead of pretending.
add_executable(routing-views routing_views.cpp)
target_link_libraries(routing-views PRIVATE router-console-core)

foreach(mode routes ospf bgp config)
    # Host comes from the configure-time RC_TEST_HOST (a private VM copy on other
    # forwards); user/password stay on the vm_fixture defaults.
    add_test(NAME routing-${mode}
             COMMAND routing-views --mode ${mode} --host ${RC_TEST_HOST} --accept-new-cert)
    set_tests_properties(routing-${mode} PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 180)
endforeach()

# --- Milestone 14: terminal --------------------------------------------------
# Command-at-a-time console over rpcd file exec: stdout, stderr and the exit
# status per command, plus the working directory and history the window carries.
add_executable(m14-terminal m14_terminal.cpp)
target_link_libraries(m14-terminal PRIVATE router-console-core)
add_test(NAME m14-terminal COMMAND m14-terminal --accept-new-cert)
set_tests_properties(m14-terminal PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)

# --- Milestone 15: MDI shell -------------------------------------------------
# Cascade/tile/clamp math plus the QSettings store: geometry written by one
# process has to come back in the next one, and a remembered rect must stay
# reachable on a smaller desktop. No router involved.
add_executable(m15-mdi-geometry m15_mdi_geometry.cpp)
target_link_libraries(m15-mdi-geometry PRIVATE router-console-core)
add_test(NAME m15-mdi-geometry COMMAND m15-mdi-geometry)
set_tests_properties(m15-mdi-geometry PROPERTIES TIMEOUT 30)

# The views must stay frame content rather than top-level windows, and the tree
# must stay the launcher.
add_test(NAME m15-mdi-structure
         COMMAND ${CMAKE_SOURCE_DIR}/tests/m15_mdi_structure.sh ${CMAKE_SOURCE_DIR})
set_tests_properties(m15-mdi-structure PROPERTIES TIMEOUT 30)
