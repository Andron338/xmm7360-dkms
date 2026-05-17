#pragma once
/*
 * xmm_proto.h — XMM7360 wire-format pack/unpack engine
 *
 * The modem uses a simple ASN.1-inspired encoding:
 *   Integer types:  \x02\x<len> <big-endian value>   (B=1B, H=2B, L=4B)
 *   Byte strings:   \x55 <valid-len> <count-asn4> <padding-asn4> <data> <zeros>
 *   Wide strings:   \x56 (2-byte elems) / \x57 (4-byte elems) — not needed here
 *
 * The valid-len field uses BER long-form encoding when >= 128:
 *   0x80|n  b0 b1 … b(n-1)   where value = b0 | (b1<<8) | … (little-endian)
 */

#include <stdint.h>
#include <stddef.h>

/* ── Growable byte buffer ─────────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} xmm_buf_t;

void xmm_buf_init(xmm_buf_t *b);
void xmm_buf_free(xmm_buf_t *b);
int  xmm_buf_append(xmm_buf_t *b, const void *data, size_t len);
int  xmm_buf_append_byte(xmm_buf_t *b, uint8_t byte);

/* ── Pack primitives ──────────────────────────────────────────────────────── */

/* \x02\x01 val  — maps to Python pack 'B' */
int pack_u8(xmm_buf_t *b, uint8_t val);

/* \x02\x02 val_BE  — maps to Python pack 'H' */
int pack_u16(xmm_buf_t *b, uint16_t val);

/* \x02\x04 val_BE  — maps to Python pack 'L' / asn_int4() */
int pack_u32(xmm_buf_t *b, uint32_t val);

/*
 * Byte-string field (type 0x55) — maps to Python pack 's<N>'.
 * data      : payload bytes (may be NULL when valid_len==0)
 * valid_len : bytes of real data
 * max_len   : field capacity (count = max_len, padding = max_len - valid_len)
 */
int pack_str(xmm_buf_t *b, const uint8_t *data, size_t valid_len, size_t max_len);

/* Convenience: pack a zero-filled string field */
int pack_str_zeros(xmm_buf_t *b, size_t valid_len, size_t max_len);

/* ── Unpack reader ────────────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} xmm_reader_t;

void xmm_reader_init(xmm_reader_t *r, const uint8_t *data, size_t len);

/* Read one ASN integer (type 0x02); returns -1 on error */
int unpack_u32(xmm_reader_t *r, uint32_t *out);

/*
 * Read one string field (type 0x55/0x56/0x57).
 * *out points into r->data (not a copy); caller must not free it.
 * *out_len is the valid byte count (already adjusted for elem width).
 */
int unpack_str(xmm_reader_t *r, const uint8_t **out, size_t *out_len);

/* Skip n consecutive ASN integers */
int unpack_skip_u32(xmm_reader_t *r, int n);
