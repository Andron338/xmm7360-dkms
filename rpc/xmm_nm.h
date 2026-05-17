#pragma once
/*
 * xmm_nm.h — NetworkManager connection management via libnm
 *
 * Mirrors xm_dbus.py: finds or creates an "xmm7360" connection,
 * updates its IP/gateway/DNS, and activates it on wwan0.
 *
 * Requires: libnm (pkg-config: libnm)
 */

/*
 * Configure NetworkManager for the XMM7360 modem.
 *
 * ip_str   : dotted-quad IP address string
 * dns_v4   : array of IPv4 DNS strings
 * ndns_v4  : count of entries in dns_v4
 * metric   : route priority passed as dns-priority
 *
 * Returns 0 on success, -1 on error.
 */
int xmm_nm_setup(const char  *ip_str,
                 const char   dns_v4[][16],
                 int          ndns_v4,
                 int          metric);
