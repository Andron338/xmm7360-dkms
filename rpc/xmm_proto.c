#include "xmm_proto.h"
#include <stdlib.h>
#include <string.h>

/* ── Buffer ───────────────────────────────────────────────────────────────── */

void xmm_buf_init(xmm_buf_t *b) { b->data = NULL; b->len = b->cap = 0; }

void xmm_buf_free(xmm_buf_t *b) { free(b->data); xmm_buf_init(b); }

static int buf_reserve(xmm_buf_t *b, size_t extra) {
    size_t need = b->len + extra;
    if (need <= b->cap) return 0;
    size_t nc = b->cap ? b->cap * 2 : 128;
    while (nc < need) nc *= 2;
    uint8_t *nd = realloc(b->data, nc);
    if (!nd) return -1;
    b->data = nd; b->cap = nc;
    return 0;
}

int xmm_buf_append(xmm_buf_t *b, const void *data, size_t len) {
    if (buf_reserve(b, len) < 0) return -1;
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

int xmm_buf_append_byte(xmm_buf_t *b, uint8_t byte) {
    return xmm_buf_append(b, &byte, 1);
}

/* ── Pack ─────────────────────────────────────────────────────────────────── */

int pack_u8(xmm_buf_t *b, uint8_t val) {
    uint8_t tmp[3] = {0x02, 0x01, val};
    return xmm_buf_append(b, tmp, 3);
}

int pack_u16(xmm_buf_t *b, uint16_t val) {
    uint8_t tmp[4] = {0x02, 0x02, (uint8_t)(val >> 8), (uint8_t)(val & 0xff)};
    return xmm_buf_append(b, tmp, 4);
}

int pack_u32(xmm_buf_t *b, uint32_t val) {
    uint8_t tmp[6] = {
        0x02, 0x04,
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8),  (uint8_t)(val & 0xff)
    };
    return xmm_buf_append(b, tmp, 6);
}

int pack_str(xmm_buf_t *b, const uint8_t *data, size_t valid_len, size_t max_len) {
    /* type tag */
    xmm_buf_append_byte(b, 0x55);

    /* BER valid-length: single byte if < 128, else 0x80|n + n LE bytes */
    if (valid_len < 128) {
        xmm_buf_append_byte(b, (uint8_t)valid_len);
    } else {
        uint8_t le[8];
        int nb = 0;
        size_t rem = valid_len;
        while (rem > 0) { le[nb++] = rem & 0xff; rem >>= 8; }
        xmm_buf_append_byte(b, (uint8_t)(0x80 | nb));
        xmm_buf_append(b, le, nb);
    }

    /* count = max_len, padding = max_len - valid_len */
    pack_u32(b, (uint32_t)max_len);
    pack_u32(b, (uint32_t)(max_len - valid_len));

    /* payload */
    if (valid_len)
        xmm_buf_append(b, data, valid_len);   /* data is never NULL; use pack_str_zeros for zero fields */

    /* zero padding */
    static const uint8_t Z[512];   /* static zero block */
    size_t pad = max_len - valid_len;
    while (pad > 0) {
        size_t chunk = pad < sizeof(Z) ? pad : sizeof(Z);
        xmm_buf_append(b, Z, chunk);
        pad -= chunk;
    }
    return 0;
}

int pack_str_zeros(xmm_buf_t *b, size_t valid_len, size_t max_len) {
    /*
     * Do NOT pass NULL to pack_str: the NULL guard skips writing the
     * valid_len payload bytes, leaving the body valid_len bytes short
     * per field (907 B × 4 blocks = 3628 B missing, giving 1439 instead
     * of ~5067 — enough to make the modem silently ignore the packet).
     * Use a static zero buffer instead.
     * 300 bytes covers the largest zero-fill we request (s260 → 257 B).
     */
    static const uint8_t Z[300];   /* BSS → guaranteed zero-initialised */
    if (valid_len > sizeof(Z)) return -1;   /* should never happen */
    return pack_str(b, Z, valid_len, max_len);
}

/* ── Unpack ───────────────────────────────────────────────────────────────── */

void xmm_reader_init(xmm_reader_t *r, const uint8_t *data, size_t len) {
    r->data = data; r->len = len; r->pos = 0;
}

int unpack_u32(xmm_reader_t *r, uint32_t *out) {
    if (r->pos >= r->len || r->data[r->pos] != 0x02) return -1;
    r->pos++;
    if (r->pos >= r->len) return -1;
    uint8_t l = r->data[r->pos++];
    if (r->pos + l > r->len) return -1;
    uint32_t v = 0;
    for (int i = 0; i < l; i++) v = (v << 8) | r->data[r->pos++];
    *out = v;
    return 0;
}

int unpack_str(xmm_reader_t *r, const uint8_t **out, size_t *out_len) {
    if (r->pos >= r->len) return -1;
    uint8_t t = r->data[r->pos++];
    if (t != 0x55 && t != 0x56 && t != 0x57) return -1;

    if (r->pos >= r->len) return -1;
    uint8_t vb = r->data[r->pos++];

    size_t valid = 0;
    if (vb & 0x80) {
        int nb = vb & 0x0f;
        for (int i = 0; i < nb; i++) {
            if (r->pos >= r->len) return -1;
            valid |= (size_t)r->data[r->pos++] << (i * 8);
        }
    } else {
        valid = vb;
    }
    /* adjust for element width */
    if (t == 0x56) valid <<= 1;
    else if (t == 0x57) valid <<= 2;

    uint32_t count, padding;
    if (unpack_u32(r, &count) < 0) return -1;
    if (unpack_u32(r, &padding) < 0) return -1;
    if (r->pos + valid + padding > r->len) return -1;

    *out = r->data + r->pos;
    *out_len = valid;
    r->pos += valid + padding;
    return 0;
}

int unpack_skip_u32(xmm_reader_t *r, int n) {
    uint32_t d;
    for (int i = 0; i < n; i++)
        if (unpack_u32(r, &d) < 0) return -1;
    return 0;
}
