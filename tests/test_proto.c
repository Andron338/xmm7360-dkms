/*
 * tests/test_proto.c — unit tests for xmm_proto.c
 *
 * Tests the ASN.1-inspired wire-format encoder/decoder used to build
 * and parse all RPC messages. No hardware required.
 *
 * Run: make -C tests test_proto && ./tests/test_proto
 */

#include "test_framework.h"
#include "../tool/xmm_proto.h"

/* ── xmm_buf_t ────────────────────────────────────────────────────────────── */

static void test_buf_init_zero(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(b.len, 0u);
    ASSERT_EQ(b.cap, 0u);
    ASSERT_NULL(b.data);
    xmm_buf_free(&b);
}

static void test_buf_append_grows(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);

    uint8_t chunk[64];
    memset(chunk, 0xAB, sizeof(chunk));

    ASSERT_EQ(xmm_buf_append(&b, chunk, sizeof(chunk)), 0);
    ASSERT_EQ(b.len, 64u);
    ASSERT_NOT_NULL(b.data);
    ASSERT_MEM_EQ(b.data, chunk, 64);

    xmm_buf_free(&b);
}

static void test_buf_append_byte(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);

    for (int i = 0; i < 256; i++)
        ASSERT_EQ(xmm_buf_append_byte(&b, (uint8_t)i), 0);

    ASSERT_EQ(b.len, 256u);
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(b.data[i], (uint8_t)i);

    xmm_buf_free(&b);
}

static void test_buf_free_idempotent(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    xmm_buf_append_byte(&b, 1);
    xmm_buf_free(&b);
    /* After free, data is NULL and lengths are reset */
    ASSERT_EQ(b.len, 0u);
}

/* ── pack_u8 ──────────────────────────────────────────────────────────────── */

static void test_pack_u8_encoding(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_u8(&b, 0x42), 0);
    /* ASN type 0x02, length 0x01, value */
    ASSERT_EQ(b.len, 3u);
    ASSERT_EQ(b.data[0], 0x02);  /* ASN INTEGER tag */
    ASSERT_EQ(b.data[1], 0x01);  /* length = 1 byte */
    ASSERT_EQ(b.data[2], 0x42);  /* value */

    xmm_buf_free(&b);
}

static void test_pack_u8_zero(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u8(&b, 0), 0);
    ASSERT_EQ(b.data[2], 0x00);
    xmm_buf_free(&b);
}

static void test_pack_u8_max(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u8(&b, 0xFF), 0);
    ASSERT_EQ(b.data[2], 0xFF);
    xmm_buf_free(&b);
}

/* ── pack_u16 ─────────────────────────────────────────────────────────────── */

static void test_pack_u16_encoding(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_u16(&b, 0x1234), 0);
    /* ASN type 0x02, length 0x02, value big-endian */
    ASSERT_EQ(b.len, 4u);
    ASSERT_EQ(b.data[0], 0x02);
    ASSERT_EQ(b.data[1], 0x02);
    ASSERT_EQ(b.data[2], 0x12);  /* high byte */
    ASSERT_EQ(b.data[3], 0x34);  /* low byte */

    xmm_buf_free(&b);
}

static void test_pack_u16_zero(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u16(&b, 0), 0);
    ASSERT_EQ(b.data[2], 0x00);
    ASSERT_EQ(b.data[3], 0x00);
    xmm_buf_free(&b);
}

static void test_pack_u16_max(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u16(&b, 0xFFFF), 0);
    ASSERT_EQ(b.data[2], 0xFF);
    ASSERT_EQ(b.data[3], 0xFF);
    xmm_buf_free(&b);
}

/* ── pack_u32 ─────────────────────────────────────────────────────────────── */

static void test_pack_u32_encoding(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_u32(&b, 0x12345678), 0);
    /* ASN type 0x02, length 0x04, value big-endian */
    ASSERT_EQ(b.len, 6u);
    ASSERT_EQ(b.data[0], 0x02);
    ASSERT_EQ(b.data[1], 0x04);
    ASSERT_EQ(b.data[2], 0x12);
    ASSERT_EQ(b.data[3], 0x34);
    ASSERT_EQ(b.data[4], 0x56);
    ASSERT_EQ(b.data[5], 0x78);

    xmm_buf_free(&b);
}

static void test_pack_u32_zero(void)
{
    /* Default RPC body used when no args — must be exactly 6 bytes of zeros */
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u32(&b, 0), 0);
    ASSERT_EQ(b.len, 6u);
    uint8_t expected[] = {0x02, 0x04, 0x00, 0x00, 0x00, 0x00};
    ASSERT_MEM_EQ(b.data, expected, 6);
    xmm_buf_free(&b);
}

static void test_pack_u32_max(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u32(&b, 0xFFFFFFFF), 0);
    ASSERT_EQ(b.data[2], 0xFF);
    ASSERT_EQ(b.data[5], 0xFF);
    xmm_buf_free(&b);
}

static void test_pack_u32_enable_flag(void)
{
    /* pack_u32(1) = signal enable — regression: was incorrectly pack_u32(0) */
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u32(&b, 1), 0);
    ASSERT_EQ(b.data[5], 0x01);
    xmm_buf_free(&b);
}

/* ── pack/unpack round-trip ───────────────────────────────────────────────── */

static void test_u32_roundtrip(void)
{
    const uint32_t values[] = {0, 1, 0x42, 0xDEADBEEF, 0xFFFFFFFF, 0x00010000};

    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        xmm_buf_t b;
        xmm_buf_init(&b);
        ASSERT_EQ(pack_u32(&b, values[i]), 0);

        xmm_reader_t r;
        xmm_reader_init(&r, b.data, b.len);
        uint32_t out = 0;
        ASSERT_EQ(unpack_u32(&r, &out), 0);
        ASSERT_EQ(out, values[i]);

        xmm_buf_free(&b);
    }
}

static void test_multi_u32_roundtrip(void)
{
    /* Pack multiple values, unpack in sequence */
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_u32(&b, 0xAABBCCDD), 0);
    ASSERT_EQ(pack_u32(&b, 0x11223344), 0);
    ASSERT_EQ(pack_u32(&b, 0x00000005), 0);

    xmm_reader_t r;
    xmm_reader_init(&r, b.data, b.len);

    uint32_t v1 = 0, v2 = 0, v3 = 0;
    ASSERT_EQ(unpack_u32(&r, &v1), 0);
    ASSERT_EQ(unpack_u32(&r, &v2), 0);
    ASSERT_EQ(unpack_u32(&r, &v3), 0);

    ASSERT_EQ(v1, 0xAABBCCDDu);
    ASSERT_EQ(v2, 0x11223344u);
    ASSERT_EQ(v3, 0x00000005u);

    xmm_buf_free(&b);
}

static void test_unpack_past_end_fails(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_u32(&b, 42), 0);

    xmm_reader_t r;
    xmm_reader_init(&r, b.data, b.len);

    uint32_t v;
    ASSERT_EQ(unpack_u32(&r, &v), 0);   /* first: ok */
    ASSERT_NE(unpack_u32(&r, &v), 0);   /* second: past end, must fail */

    xmm_buf_free(&b);
}

static void test_unpack_skip(void)
{
    xmm_buf_t b;
    xmm_buf_init(&b);
    pack_u32(&b, 0xAAAAAAAA);
    pack_u32(&b, 0xBBBBBBBB);
    pack_u32(&b, 0xCCCCCCCC);

    xmm_reader_t r;
    xmm_reader_init(&r, b.data, b.len);
    ASSERT_EQ(unpack_skip_u32(&r, 2), 0);

    uint32_t v;
    ASSERT_EQ(unpack_u32(&r, &v), 0);
    ASSERT_EQ(v, 0xCCCCCCCCu);

    xmm_buf_free(&b);
}

/* ── pack_str_zeros ───────────────────────────────────────────────────────── */

static void test_pack_str_zeros_all_zero(void)
{
    /* Regression: NULL data with valid_len==0 must still write the field */
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_str_zeros(&b, 0, 32), 0);
    ASSERT(b.len > 0);  /* must have produced output */

    xmm_buf_free(&b);
}

static void test_pack_str_zeros_partial(void)
{
    /* valid_len < max_len: valid bytes + zero padding */
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_str_zeros(&b, 4, 16), 0);
    ASSERT(b.len > 0);
    xmm_buf_free(&b);
}

static void test_pack_str_roundtrip(void)
{
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    xmm_buf_t b;
    xmm_buf_init(&b);

    ASSERT_EQ(pack_str(&b, payload, sizeof(payload), 16), 0);

    xmm_reader_t r;
    xmm_reader_init(&r, b.data, b.len);

    const uint8_t *out;
    size_t out_len;
    ASSERT_EQ(unpack_str(&r, &out, &out_len), 0);
    ASSERT_EQ(out_len, sizeof(payload));
    ASSERT_MEM_EQ(out, payload, sizeof(payload));

    xmm_buf_free(&b);
}

static void test_pack_str_empty(void)
{
    /* Empty string field must still be encodable and decodable */
    xmm_buf_t b;
    xmm_buf_init(&b);
    ASSERT_EQ(pack_str(&b, NULL, 0, 8), 0);
    ASSERT(b.len > 0);

    xmm_reader_t r;
    xmm_reader_init(&r, b.data, b.len);
    const uint8_t *out;
    size_t out_len;
    ASSERT_EQ(unpack_str(&r, &out, &out_len), 0);
    ASSERT_EQ(out_len, 0u);

    xmm_buf_free(&b);
}

/* ── reader init ──────────────────────────────────────────────────────────── */

static void test_reader_init(void)
{
    uint8_t data[] = {0x01, 0x02, 0x03};
    xmm_reader_t r;
    xmm_reader_init(&r, data, sizeof(data));
    ASSERT_EQ(r.pos, 0u);
    ASSERT_EQ(r.len, 3u);
    ASSERT(r.data == data);
}

int main(void)
{
    printf("=== xmm_proto tests ===\n\n");

    RUN_TEST(test_buf_init_zero);
    RUN_TEST(test_buf_append_grows);
    RUN_TEST(test_buf_append_byte);
    RUN_TEST(test_buf_free_idempotent);

    RUN_TEST(test_pack_u8_encoding);
    RUN_TEST(test_pack_u8_zero);
    RUN_TEST(test_pack_u8_max);

    RUN_TEST(test_pack_u16_encoding);
    RUN_TEST(test_pack_u16_zero);
    RUN_TEST(test_pack_u16_max);

    RUN_TEST(test_pack_u32_encoding);
    RUN_TEST(test_pack_u32_zero);
    RUN_TEST(test_pack_u32_max);
    RUN_TEST(test_pack_u32_enable_flag);

    RUN_TEST(test_u32_roundtrip);
    RUN_TEST(test_multi_u32_roundtrip);
    RUN_TEST(test_unpack_past_end_fails);
    RUN_TEST(test_unpack_skip);

    RUN_TEST(test_pack_str_zeros_all_zero);
    RUN_TEST(test_pack_str_zeros_partial);
    RUN_TEST(test_pack_str_roundtrip);
    RUN_TEST(test_pack_str_empty);

    RUN_TEST(test_reader_init);

    TEST_SUMMARY();
}
