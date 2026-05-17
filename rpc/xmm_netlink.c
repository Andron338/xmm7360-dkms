#include "xmm_netlink.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if.h>   /* struct ifreq, IFNAMSIZ, IFF_UP */
#include <unistd.h>

/* ── Small rtnetlink helpers ─────────────────────────────────────────────── */

/* Append one rtattr to a netlink message buffer. */
static void addattr(struct nlmsghdr *nlh, int maxlen,
                    int type, const void *data, int len)
{
    int total = (int)nlh->nlmsg_len + RTA_SPACE(len);
    if (total > maxlen) return;   /* caller checked sizes; shouldn't happen */

    struct rtattr *rta = (struct rtattr *)
        ((uint8_t *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = RTA_LENGTH(len);
    if (data && len) memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_SPACE(len);
}

static void addattr32(struct nlmsghdr *nlh, int maxlen, int type, uint32_t val) {
    addattr(nlh, maxlen, type, &val, sizeof(val));
}

/* Open a NETLINK_ROUTE socket, returns fd or -1 */
static int nl_open(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) { perror("socket(AF_NETLINK)"); return -1; }

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(AF_NETLINK)"); close(fd); return -1;
    }
    return fd;
}

/* Send a netlink request and wait for NLMSG_ERROR (ack) */
static int nl_talk(int fd, struct nlmsghdr *nlh) {
    static uint32_t seq = 1;
    nlh->nlmsg_seq   = seq++;
    nlh->nlmsg_flags |= NLM_F_ACK;

    if (send(fd, nlh, nlh->nlmsg_len, 0) < 0) {
        perror("nl_talk: send"); return -1;
    }

    uint8_t buf[8192];
    while (1) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) { perror("nl_talk: recv"); return -1; }

        struct nlmsghdr *rh = (struct nlmsghdr *)buf;
        for (; NLMSG_OK(rh, (unsigned)n); rh = NLMSG_NEXT(rh, n)) {
            if (rh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *e = (struct nlmsgerr *)NLMSG_DATA(rh);
                if (e->error == 0) return 0;   /* success ack */
                errno = -e->error;
                return -1;
            }
            if (rh->nlmsg_type == NLMSG_DONE) return 0;
        }
    }
}

/* ── Get interface index ─────────────────────────────────────────────────── */

static int if_index(const char *ifname) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("socket(AF_INET)"); return -1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    int rc = ioctl(fd, SIOCGIFINDEX, &ifr);
    close(fd);
    if (rc < 0) { perror("SIOCGIFINDEX"); return -1; }
    return ifr.ifr_ifindex;
}

/* ── Bring interface UP ───────────────────────────────────────────────────── */

static int if_up(const char *ifname) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("SIOCGIFFLAGS"); close(fd); return -1;
    }
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("SIOCSIFFLAGS"); close(fd); return -1;
    }
    close(fd);
    return 0;
}

/* ── Flush all IPv4 addresses on an interface ───────────────────────────── */

static int if_flush_addr(int nlfd, int ifidx) {
    /* Enumerate current addresses with RTM_GETADDR */
    struct {
        struct nlmsghdr nlh;
        struct ifaddrmsg ifa;
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.nlh.nlmsg_type  = RTM_GETADDR;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq   = 0;
    req.ifa.ifa_family  = AF_INET;
    req.ifa.ifa_index   = ifidx;

    if (send(nlfd, &req, req.nlh.nlmsg_len, 0) < 0) {
        perror("flush: send RTM_GETADDR"); return -1;
    }

    uint8_t buf[16384];
    while (1) {
        ssize_t n = recv(nlfd, buf, sizeof(buf), 0);
        if (n < 0) { perror("flush: recv"); return -1; }

        struct nlmsghdr *h = (struct nlmsghdr *)buf;
        int done = 0;
        for (; NLMSG_OK(h, (unsigned)n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE) { done = 1; break; }
            if (h->nlmsg_type != RTM_NEWADDR) continue;

            struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(h);
            if ((int)ifa->ifa_index != ifidx || ifa->ifa_family != AF_INET)
                continue;

            /* Delete this address */
            struct {
                struct nlmsghdr  nlh;
                struct ifaddrmsg ifa;
                uint8_t          attrs[256];
            } del;
            memset(&del, 0, sizeof(del));
            del.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
            del.nlh.nlmsg_type  = RTM_DELADDR;
            del.nlh.nlmsg_flags = NLM_F_REQUEST;
            del.ifa = *ifa;

            /* Copy all attributes verbatim */
            int alen = (int)IFA_PAYLOAD(h);
            struct rtattr *rta = IFA_RTA(ifa);
            for (; RTA_OK(rta, alen); rta = RTA_NEXT(rta, alen)) {
                addattr(&del.nlh, (int)sizeof(del),
                        rta->rta_type, RTA_DATA(rta), (int)RTA_PAYLOAD(rta));
            }

            nl_talk(nlfd, &del.nlh);   /* ignore per-addr errors */
        }
        if (done) break;
    }
    return 0;
}

/* ── Add /32 IPv4 address ─────────────────────────────────────────────────── */

static int if_add_addr(int nlfd, int ifidx, uint32_t addr_ne) {
    struct {
        struct nlmsghdr  nlh;
        struct ifaddrmsg ifa;
        uint8_t          attrs[128];
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.nlh.nlmsg_type  = RTM_NEWADDR;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;
    req.ifa.ifa_family   = AF_INET;
    req.ifa.ifa_prefixlen = 32;
    req.ifa.ifa_index    = ifidx;
    /* Bug fix: RT_SCOPE_HOST marks the address as loopback-only —
     * the kernel never selects it as a source for outgoing traffic.
     * RT_SCOPE_UNIVERSE (0) is correct for a routable interface addr. */
    req.ifa.ifa_scope    = RT_SCOPE_UNIVERSE;

    addattr(&req.nlh, (int)sizeof(req), IFA_LOCAL,   &addr_ne, 4);
    addattr(&req.nlh, (int)sizeof(req), IFA_ADDRESS, &addr_ne, 4);

    return nl_talk(nlfd, &req.nlh);
}

/* ── Add default route ────────────────────────────────────────────────────── */

static int route_add_default(int nlfd, int ifidx, uint32_t gw_ne, uint32_t metric) {
    struct {
        struct nlmsghdr nlh;
        struct rtmsg    rtm;
        uint8_t         attrs[256];
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type  = RTM_NEWROUTE;
    /* Bug fix: NLM_F_EXCL fails if the route already exists (e.g. lte up
     * run twice); use NLM_F_REPLACE so we overwrite stale entries.
     * Bug fix: RT_SCOPE_LINK + RTA_GATEWAY is rejected by some kernels;
     * RT_SCOPE_UNIVERSE is correct for a route with an explicit gateway. */
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;
    req.rtm.rtm_family   = AF_INET;
    req.rtm.rtm_dst_len  = 0;       /* default route */
    req.rtm.rtm_src_len  = 0;
    req.rtm.rtm_table    = RT_TABLE_MAIN;
    req.rtm.rtm_scope    = RT_SCOPE_UNIVERSE;
    req.rtm.rtm_type     = RTN_UNICAST;
    req.rtm.rtm_protocol = RTPROT_STATIC;

    addattr32(&req.nlh, (int)sizeof(req), RTA_OIF,      (uint32_t)ifidx);
    addattr(&req.nlh,   (int)sizeof(req), RTA_GATEWAY,  &gw_ne, 4);
    addattr32(&req.nlh, (int)sizeof(req), RTA_PRIORITY, metric);
    /* Preferred source: steer the kernel to pick wwan0's own IP */
    addattr(&req.nlh,   (int)sizeof(req), RTA_PREFSRC,  &gw_ne, 4);

    return nl_talk(nlfd, &req.nlh);
}

/* ── Public entry point ───────────────────────────────────────────────────── */

int xmm_if_configure(const char *ifname,
                     const char *ip_str,
                     int         metric,
                     int         add_route)
{
    uint32_t addr_ne;
    if (inet_pton(AF_INET, ip_str, &addr_ne) != 1) {
        fprintf(stderr, "xmm_if: invalid IP '%s'\n", ip_str);
        return -1;
    }

    int idx = if_index(ifname);
    if (idx < 0) return -1;

    int nlfd = nl_open();
    if (nlfd < 0) return -1;

    int rc = 0;

    if (if_flush_addr(nlfd, idx) < 0)
        fprintf(stderr, "xmm_if: warning: flush failed\n");  /* non-fatal */

    if (if_up(ifname) < 0) { rc = -1; goto out; }

    if (if_add_addr(nlfd, idx, addr_ne) < 0) {
        perror("xmm_if: add address"); rc = -1; goto out;
    }

    if (add_route) {
        if (route_add_default(nlfd, idx, addr_ne, (uint32_t)metric) < 0) {
            perror("xmm_if: add default route");
            fprintf(stderr, "xmm_if: no default route — internet will not work\n");
            /* non-fatal: interface is still up with IP assigned */
        } else {
            fprintf(stderr, "xmm_if: default route added via %s metric %d\n",
                    ip_str, metric);
        }
    }

out:
    close(nlfd);
    return rc;
}
