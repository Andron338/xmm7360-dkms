/*
 * xmm7360-watch.c — ModemManager state monitor for XMM7360
 *
 * Watches org.freedesktop.ModemManager1.Modem PropertiesChanged signals
 * via GLib/GDBus. When the modem state drops from "connected" to anything
 * lower, triggers xmm7360-recovery.service via systemctl.
 *
 * This is far more reliable than the NM dispatcher for detecting disconnect
 * because it uses MM's own D-Bus signalling rather than interface events.
 *
 * When MM 1.26 ships in Arch (which handles the full RPC lifecycle natively),
 * this daemon becomes unnecessary and can be removed from the package.
 *
 * Build:  cc xmm7360-watch.c -o xmm7360-watch $(pkg-config --cflags --libs gio-2.0)
 */

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MM_DBUS_NAME      "org.freedesktop.ModemManager1"
#define MM_DBUS_PATH      "/org/freedesktop/ModemManager1"
#define MM_MODEM_IFACE    "org.freedesktop.ModemManager1.Modem"
#define MM_MANAGER_IFACE  "org.freedesktop.ModemManager1"
#define PROPS_IFACE       "org.freedesktop.DBus.Properties"

/* MM_MODEM_STATE_CONNECTED = 11 */
#define MM_MODEM_STATE_CONNECTED 11

typedef struct {
    GDBusConnection *conn;
    GHashTable      *modem_states; /* object_path → last known state */
} WatchCtx;

static void trigger_recovery(void)
{
    g_message("Triggering xmm7360-recovery.service");
    int ret = system("systemctl start --no-block xmm7360-recovery.service");
    if (ret != 0)
        g_warning("systemctl start failed: %d", ret);
}

static void on_properties_changed(GDBusConnection  *conn,
                                   const gchar      *sender,
                                   const gchar      *object_path,
                                   const gchar      *interface_name,
                                   const gchar      *signal_name,
                                   GVariant         *parameters,
                                   gpointer          user_data)
{
    WatchCtx    *ctx = user_data;
    const gchar *iface;
    GVariant    *changed;
    GVariant    *state_v;
    gint32       new_state;
    gpointer     prev_state_p;
    gint32       prev_state;

    (void)conn; (void)sender; (void)interface_name; (void)signal_name;

    g_variant_get(parameters, "(&s@a{sv}@as)", &iface, &changed, NULL);

    if (g_strcmp0(iface, MM_MODEM_IFACE) != 0)
        goto out;

    state_v = g_variant_lookup_value(changed, "State", G_VARIANT_TYPE_INT32);
    if (!state_v)
        goto out;

    new_state = g_variant_get_int32(state_v);
    g_variant_unref(state_v);

    prev_state_p = g_hash_table_lookup(ctx->modem_states, object_path);
    prev_state   = prev_state_p ? GPOINTER_TO_INT(prev_state_p) : 0;

    g_message("Modem %s state: %d → %d", object_path, prev_state, new_state);

    if (prev_state >= MM_MODEM_STATE_CONNECTED &&
        new_state  <  MM_MODEM_STATE_CONNECTED) {
        g_message("Modem disconnected — scheduling recovery");
        trigger_recovery();
    }

    g_hash_table_insert(ctx->modem_states,
                        g_strdup(object_path),
                        GINT_TO_POINTER(new_state));

out:
    g_variant_unref(changed);
}

/* Enumerate existing modems and store their current state */
static void seed_existing_modems(WatchCtx *ctx)
{
    GError   *err   = NULL;
    GVariant *reply = g_dbus_connection_call_sync(
        ctx->conn,
        MM_DBUS_NAME,
        MM_DBUS_PATH,
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE,
        5000, NULL, &err);

    if (!reply) {
        g_warning("GetManagedObjects failed: %s", err ? err->message : "?");
        g_clear_error(&err);
        return;
    }

    GVariant *objects = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, objects);

    const gchar *path;
    GVariant    *ifaces;
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant *mm_iface = g_variant_lookup_value(ifaces, MM_MODEM_IFACE, NULL);
        if (mm_iface) {
            GVariant *state_v = g_variant_lookup_value(mm_iface, "State",
                                                       G_VARIANT_TYPE_INT32);
            if (state_v) {
                gint32 state = g_variant_get_int32(state_v);
                g_message("Found existing modem %s in state %d", path, state);
                g_hash_table_insert(ctx->modem_states,
                                    g_strdup(path),
                                    GINT_TO_POINTER(state));
                g_variant_unref(state_v);
            }
            g_variant_unref(mm_iface);
        }
        g_variant_unref(ifaces);
    }

    g_variant_unref(objects);
    g_variant_unref(reply);
}

int main(void)
{
    GError   *err = NULL;
    GMainLoop *loop;
    WatchCtx  ctx = {0};

    g_message("xmm7360-watch starting");

    ctx.conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!ctx.conn) {
        g_critical("Cannot connect to system D-Bus: %s", err ? err->message : "?");
        return 1;
    }

    ctx.modem_states = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);

    /* Subscribe to PropertiesChanged on all MM modem objects */
    g_dbus_connection_signal_subscribe(
        ctx.conn,
        MM_DBUS_NAME,                /* sender */
        PROPS_IFACE,                 /* interface */
        "PropertiesChanged",         /* member */
        NULL,                        /* object path — NULL = all */
        MM_MODEM_IFACE,              /* arg0 filter: only modem properties */
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_properties_changed,
        &ctx, NULL);

    seed_existing_modems(&ctx);

    loop = g_main_loop_new(NULL, FALSE);
    g_message("Watching for modem state changes...");
    g_main_loop_run(loop);

    return 0;
}
