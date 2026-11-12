enable_testing()

add_executable(connect-smoke connect_smoke.cpp)
target_link_libraries(connect-smoke PRIVATE router-console-core)

# Runs against the dev VM (docs/dev-vm-recipe.md); skipped when it is not up.
add_test(NAME connect-smoke
         COMMAND connect-smoke --host 127.0.0.1:10443 --user root --password root --accept-new-cert)
set_tests_properties(connect-smoke PROPERTIES SKIP_RETURN_CODE 3 TIMEOUT 40)
