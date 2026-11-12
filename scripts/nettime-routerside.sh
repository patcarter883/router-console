#!/usr/bin/env bash
# The router-side name of the video project's nettime package. Its apk, its
# procd service and its rpcd object are all published under this one name, so
# every VM script that has to address the real service reads it from here.
#
# Nothing else in this repository may spell it: the client's own plugin is
# `nettime`, and only the binding to the router-side service needs this name.
NETTIME_ROUTER_OBJECT=jxs-nettime
NETTIME_ROUTER_APK_PREFIX="$NETTIME_ROUTER_OBJECT"
