enable_testing()

add_executable(connect-smoke connect_smoke.cpp)
target_link_libraries(connect-smoke PRIVATE router-console-core)

# Runs against the dev VM (docs/dev-vm-recipe.md); skipped when it is not up.
add_test(NAME connect-smoke
         COMMAND connect-smoke --host 127.0.0.1:10443 --user root --password root --accept-new-cert)
set_tests_properties(connect-smoke PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 40)

# Star check for the graphs half: per-interface byte counters must grow across
# >= 5 consecutive 1 Hz samples while traffic runs inside the VM.
add_executable(interfaces-traffic interfaces_traffic.cpp)
target_link_libraries(interfaces-traffic PRIVATE router-console-core)

add_test(NAME interfaces-traffic
         COMMAND interfaces-traffic --host 127.0.0.1:10443 --user root --password root
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
         COMMAND uci-sqm-staged --host 127.0.0.1:10443 --user root --password root
                 --accept-new-cert --domain sqm --section eth1 --option download)
set_tests_properties(uci-sqm-staged PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 120)

# Same flow with `uci apply` followed by `system reboot`: the committed value has
# to survive a real VM boot. Labelled vm-boot because it reboots the VM.
add_test(NAME uci-sqm-staged-reboot
         COMMAND uci-sqm-staged --host 127.0.0.1:10443 --user root --password root
                 --accept-new-cert --domain sqm --section eth1 --option download --reboot)
set_tests_properties(uci-sqm-staged-reboot PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 420
                     LABELS vm-boot)
