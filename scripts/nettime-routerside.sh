#!/usr/bin/env bash
# The router-side name of the nettime package. Its apk, its procd service and
# its rpcd object are all published under this one name, so every VM script
# that has to address the real service reads it from here.
NETTIME_ROUTER_OBJECT=nettime
NETTIME_ROUTER_APK_PREFIX="$NETTIME_ROUTER_OBJECT"
