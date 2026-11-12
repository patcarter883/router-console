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
