#pragma once
/*
 * xmm_netlink.h — wwan0 interface configuration
 *
 * Mirrors what pyroute2 does in open_xdatachannel.py:
 *   1. Flush existing addresses on the interface.
 *   2. Bring the interface UP.
 *   3. Assign a /32 IPv4 address.
 *   4. Add a default route via that address (scope=link) at a given metric.
 */

#include <stdint.h>

/*
 * Tear down the interface: flush addresses, remove routes, bring DOWN.
 * Call this on disconnect before re-running configure on reconnect.
 * Returns 0 on success, -1 on error.
 */
int xmm_if_teardown(const char *ifname);

/*
 * Bring interface up, flush addresses, add /32 addr and default route.
 *
 * ifname      : e.g. "wwan0"
 * ip_str      : dotted-quad IP, used for both address and gateway
 * metric      : route priority (higher = lower priority)
 * add_route   : 0 to skip default route (--nodefaultroute)
 *
 * Returns 0 on success, -1 on error.
 */
int xmm_if_configure(const char *ifname,
                     const char *ip_str,
                     int         metric,
                     int         add_route);
