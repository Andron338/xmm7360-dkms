#include "xmm_nm.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include <NetworkManager.h>

/*
 * Modern libnm removed all _sync() variants.  We replicate synchronous
 * behaviour by spinning the default GMainContext until the async callback
 * fires — the standard pattern for non-GLib-app consumers of libnm.
 */

/* ── Async→sync bridge ───────────────────────────────────────────────────── */

typedef struct {
    gboolean  done;
    GError   *error;
    gpointer  result;
} nm_sync_t;

static void spin_until_done(nm_sync_t *s) {
    while (!s->done)
        g_main_context_iteration(NULL, TRUE);
}

static void cb_client_new(GObject *src, GAsyncResult *res, gpointer ud) {
    nm_sync_t *s = ud;
    (void)src;
    s->result = nm_client_new_finish(res, &s->error);
    s->done   = TRUE;
}

static void cb_activate(GObject *src, GAsyncResult *res, gpointer ud) {
    nm_sync_t *s = ud;
    s->result = nm_client_activate_connection_finish(NM_CLIENT(src), res, &s->error);
    s->done   = TRUE;
}

static void cb_commit(GObject *src, GAsyncResult *res, gpointer ud) {
    nm_sync_t *s = ud;
    gboolean ok  = nm_remote_connection_commit_changes_finish(
                       NM_REMOTE_CONNECTION(src), res, &s->error);
    s->result = GINT_TO_POINTER(ok);
    s->done   = TRUE;
}

static void cb_add(GObject *src, GAsyncResult *res, gpointer ud) {
    nm_sync_t *s = ud;
    s->result = nm_client_add_connection_finish(NM_CLIENT(src), res, &s->error);
    s->done   = TRUE;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

static int apply_ip4_settings(NMSettingIPConfig *s_ip4,
                               const char        *ip_str,
                               const char         dns_v4[][16],
                               int                ndns_v4,
                               int                metric)
{
    GError *err = NULL;

    nm_setting_ip_config_clear_addresses(s_ip4);
    nm_setting_ip_config_clear_dns(s_ip4);
    g_object_set(G_OBJECT(s_ip4),
                 NM_SETTING_IP_CONFIG_GATEWAY,     ip_str,
                 NM_SETTING_IP_CONFIG_METHOD,       NM_SETTING_IP4_CONFIG_METHOD_MANUAL,
                 NM_SETTING_IP_CONFIG_DNS_PRIORITY, metric,
                 NULL);

    NMIPAddress *addr = nm_ip_address_new(AF_INET, ip_str, 32, &err);
    if (!addr) {
        fprintf(stderr, "nm: nm_ip_address_new: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    nm_setting_ip_config_add_address(s_ip4, addr);
    nm_ip_address_unref(addr);

    for (int i = 0; i < ndns_v4; i++)
        nm_setting_ip_config_add_dns(s_ip4, dns_v4[i]);

    return 0;
}

static int do_activate(NMClient *client, NMConnection *conn) {
    NMDevice *dev = nm_client_get_device_by_iface(client, "wwan0");
    if (!dev) {
        fprintf(stderr, "nm: wwan0 not found in NetworkManager\n");
        return -1;
    }

    if (!nm_device_get_managed(dev)) {
        fprintf(stderr, "nm: wwan0 unmanaged — requesting management\n");
        GDBusProxy *pp = g_dbus_proxy_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
            "org.freedesktop.NetworkManager",
            nm_object_get_path(NM_OBJECT(dev)),
            "org.freedesktop.DBus.Properties",
            NULL, NULL);
        if (pp) {
            GError *e = NULL;
            g_dbus_proxy_call_sync(pp, "Set",
                g_variant_new("(ssv)",
                    "org.freedesktop.NetworkManager.Device",
                    "Managed",
                    g_variant_new_boolean(TRUE)),
                G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &e);
            if (e) { fprintf(stderr, "nm: Set Managed: %s\n", e->message); g_clear_error(&e); }
            g_object_unref(pp);
        }
    }

    nm_sync_t s = {0};
    nm_client_activate_connection_async(client, conn, dev, "/",
                                        NULL, cb_activate, &s);
    spin_until_done(&s);

    if (!s.result) {
        fprintf(stderr, "nm: ActivateConnection: %s\n",
                s.error ? s.error->message : "unknown");
        g_clear_error(&s.error);
        return -1;
    }
    g_object_unref(s.result);
    return 0;
}

/* ── Public entry point ───────────────────────────────────────────────────── */

int xmm_nm_setup(const char  *ip_str,
                 const char   dns_v4[][16],
                 int          ndns_v4,
                 int          metric)
{
    nm_sync_t sc = {0};
    nm_client_new_async(NULL, cb_client_new, &sc);
    spin_until_done(&sc);

    NMClient *client = sc.result;
    if (!client) {
        fprintf(stderr, "nm: nm_client_new: %s\n",
                sc.error ? sc.error->message : "?");
        g_clear_error(&sc.error);
        return -1;
    }

    NMRemoteConnection *existing = NULL;
    {
        const GPtrArray *conns = nm_client_get_connections(client);
        for (guint i = 0; i < conns->len; i++) {
            NMRemoteConnection *rc = conns->pdata[i];
            if (strcmp(nm_connection_get_id(NM_CONNECTION(rc)), "xmm7360") == 0) {
                existing = rc;
                break;
            }
        }
    }

    int rc = 0;

    if (existing) {
        fprintf(stderr, "nm: updating connection '%s'\n",
                nm_connection_get_uuid(NM_CONNECTION(existing)));

        NMSettingIPConfig *s_ip4 =
            nm_connection_get_setting_ip4_config(NM_CONNECTION(existing));
        if (!s_ip4) {
            fprintf(stderr, "nm: no IPv4 settings on existing connection\n");
            g_object_unref(client);
            return -1;
        }

        if (apply_ip4_settings(s_ip4, ip_str, dns_v4, ndns_v4, metric) < 0) {
            g_object_unref(client);
            return -1;
        }

        nm_sync_t sc2 = {0};
        nm_remote_connection_commit_changes_async(existing, TRUE, NULL,
                                                  cb_commit, &sc2);
        spin_until_done(&sc2);

        if (!GPOINTER_TO_INT(sc2.result)) {
            fprintf(stderr, "nm: commit_changes: %s\n",
                    sc2.error ? sc2.error->message : "?");
            g_clear_error(&sc2.error);
            g_object_unref(client);
            return -1;
        }

        rc = do_activate(client, NM_CONNECTION(existing));

    } else {
        fprintf(stderr, "nm: adding new connection 'xmm7360'\n");

        NMConnection *conn = nm_simple_connection_new();

        char   uuid_str[37];
        uuid_t uu;
        uuid_generate_random(uu);
        uuid_unparse_lower(uu, uuid_str);

        NMSetting *s_con = nm_setting_connection_new();
        g_object_set(s_con,
                     NM_SETTING_CONNECTION_TYPE,           "generic",
                     NM_SETTING_CONNECTION_UUID,           uuid_str,
                     NM_SETTING_CONNECTION_ID,             "xmm7360",
                     NM_SETTING_CONNECTION_INTERFACE_NAME, "wwan0",
                     NULL);
        nm_connection_add_setting(conn, s_con);

        NMSetting *s_ip4 = nm_setting_ip4_config_new();
        if (apply_ip4_settings(NM_SETTING_IP_CONFIG(s_ip4),
                               ip_str, dns_v4, ndns_v4, metric) < 0) {
            g_object_unref(conn);
            g_object_unref(client);
            return -1;
        }
        nm_connection_add_setting(conn, s_ip4);

        NMSetting *s_ip6 = nm_setting_ip6_config_new();
        g_object_set(s_ip6,
                     NM_SETTING_IP_CONFIG_METHOD,
                     NM_SETTING_IP6_CONFIG_METHOD_IGNORE, NULL);
        nm_connection_add_setting(conn, s_ip6);

        nm_sync_t sa = {0};
        nm_client_add_connection_async(client, conn, TRUE, NULL, cb_add, &sa);
        spin_until_done(&sa);
        g_object_unref(conn);

        if (!sa.result) {
            fprintf(stderr, "nm: AddConnection: %s\n",
                    sa.error ? sa.error->message : "?");
            g_clear_error(&sa.error);
            g_object_unref(client);
            return -1;
        }

        rc = do_activate(client, NM_CONNECTION((NMRemoteConnection *)sa.result));
        g_object_unref(sa.result);
    }

    g_object_unref(client);
    return rc;
}
