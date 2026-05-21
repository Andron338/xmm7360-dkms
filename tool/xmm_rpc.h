#pragma once
/*
 * xmm_rpc.h — XMM7360 RPC engine
 *
 * Wraps the character device (/dev/xmm0/rpc or /dev/wwan0xmmrpc0),
 * provides framed message send/receive, and exposes high-level operations
 * (FCC unlock, mode set, IP query) plus all required pack functions.
 *
 * Message framing (send):
 *   [4B LE total_len] [asn4(total_len)] [asn4(cmd)] [4B BE tid_word]
 *   [asn4(tid)]  ← only present when is_async=true
 *   [body]
 *
 * Message framing (receive):
 *   [4B LE total_len] [asn4(total_len)] [asn4(code)] [4B BE txid] [body]
 *
 * txid interpretation:
 *   0x11000100                        → sync response
 *   (txid & 0xFFFFFF00)==0x11000100,
 *     code >= 2000                    → async_ack  (discard, keep pumping)
 *     code <  2000                    → async response (body starts after 6B tid)
 *   anything else                     → unsolicited indication
 */

#include "xmm_proto.h"
#include <stdbool.h>

/* Maximum single device read — matches Python's 131072 */
#define XMM_MAX_MSG 131072

/* Max time to wait for an RPC reply before giving up. After suspend/
 * hibernation the modem may be unresponsive; without a bound the
 * read would block forever and wedge the init process. */
#define XMM_RPC_READ_TIMEOUT_MS 8000

/* ── Message ──────────────────────────────────────────────────────────────── */

typedef enum {
    XMM_MSG_RESPONSE,
    XMM_MSG_ASYNC_ACK,
    XMM_MSG_UNSOLICITED,
} xmm_msg_type_t;

typedef struct {
    xmm_msg_type_t type;
    uint32_t       code;
    uint32_t       txid;
    uint8_t       *body;      /* heap-allocated; call xmm_msg_free() */
    size_t         body_len;
} xmm_msg_t;

void xmm_msg_free(xmm_msg_t *m);

/* ── RPC handle ───────────────────────────────────────────────────────────── */

typedef struct {
    int  fd;
    bool attach_allowed;   /* set by UtaMsNetIsAttachAllowedIndCb */
} xmm_rpc_t;

/*
 * Open the RPC character device.
 * Tries /dev/xmm0/rpc then /dev/wwan0xmmrpc0.
 * Returns 0 on success, -1 on error (errno set).
 */
int  xmm_rpc_open(xmm_rpc_t *rpc, const char *device_path);
void xmm_rpc_close(xmm_rpc_t *rpc);

/*
 * Read and parse exactly one message from the device.
 * Handles unsolicited side-effects (attach_allowed flag).
 * Caller owns m->body and must call xmm_msg_free().
 */
int xmm_rpc_pump(xmm_rpc_t *rpc, xmm_msg_t *m);

/*
 * Send cmd+body and pump until a RESPONSE arrives.
 * body==NULL → default body (asn_int4(0), 6 bytes).
 * Caller owns out->body and must call xmm_msg_free().
 */
int xmm_rpc_execute(xmm_rpc_t       *rpc,
                    uint32_t         cmd,
                    const uint8_t   *body,
                    size_t           body_len,
                    bool             is_async,
                    xmm_msg_t       *out);

/* ── High-level operations ────────────────────────────────────────────────── */

/* Perform FCC unlock sequence (query → challenge → SHA-256 response). */
int xmm_fcc_unlock(xmm_rpc_t *rpc);

/*
 * Set modem mode (1 = online).
 * Pumps until UtaModeSetRspCb confirms the mode.
 */
int xmm_mode_set(xmm_rpc_t *rpc, uint32_t mode);

/*
 * Query IP address and DNS servers from modem.
 * ip_out: dotted-quad string, at least 16 bytes.
 * dns_v4/dns_v6: arrays of dotted-quad / colon-hex strings, 16/46 bytes each.
 * Returns 0 if a non-zero IP was found, -1 otherwise (retry caller).
 */
int xmm_get_ip(xmm_rpc_t *rpc,
               char  ip_out[16],
               char  dns_v4[][16], int *ndns_v4,
               char  dns_v6[][46], int *ndns_v6);

/*
 * Send the proper RPC disconnect sequence:
 *   UtaRPCPSConnectReleaseReq  — release RPC data channel
 *   UtaMsCallPsDeactivateReq   — deactivate PDP context (cause = 0)
 * Pumps briefly for DeactivateRspCb / DeactivateIndCb confirmation.
 * Always returns 0 — disconnect is best-effort; errors are logged.
 */
int xmm_rpc_disconnect(xmm_rpc_t *rpc);

/* ── Pack functions ───────────────────────────────────────────────────────── */

/*
 * All pack_* functions append into a caller-provided xmm_buf_t.
 * The buffer must have been initialised with xmm_buf_init().
 * Returns 0 on success, -1 on allocation failure.
 */

/* AttachApnConfigReq — 4-block APN configuration message */
int pack_attach_apn_config(xmm_buf_t *b, const char *apn);

/* UtaMsNetAttachReq */
int pack_net_attach(xmm_buf_t *b);

/* UtaMsCallPsGetNegIpAddrReq */
int pack_get_neg_ip_addr(xmm_buf_t *b);

/* UtaMsCallPsGetNegotiatedDnsReq */
int pack_get_neg_dns(xmm_buf_t *b);

/* UtaMsCallPsConnectReq */
int pack_ps_connect(xmm_buf_t *b);

/*
 * UtaRPCPsConnectToDatachannelReq
 * path defaults to "/sioscc/PCIE/IOSM/IPS/0" when NULL.
 */
int pack_connect_datachannel(xmm_buf_t *b, const char *path);

/* UtaModeSetReq — pack('LLL', 0, mode_tid, mode) */
int pack_mode_set(xmm_buf_t *b, uint32_t mode_tid, uint32_t mode);

/* CsiFccLockVerChallengeReq — pack('L', response) */
int pack_fcc_ver_challenge(xmm_buf_t *b, uint32_t response);
