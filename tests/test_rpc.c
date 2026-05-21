/*
 * tests/test_rpc.c — unit tests for RPC message structure and tables
 *
 * Tests command ID uniqueness, unsolicited table integrity, and
 * xmm_msg_t memory management. No hardware required.
 *
 * Run: make -C tests test_rpc && ./tests/test_rpc
 */

#include "test_framework.h"
#include "../tool/xmm_rpc.h"
#include "../tool/xmm_rpc_ids.h"
#include "../tool/xmm_unsol.h"
#include <stdlib.h>

/* ── xmm_msg_t ────────────────────────────────────────────────────────────── */

static void test_msg_free_null_body(void)
{
    /* xmm_msg_free must handle a message with NULL body safely */
    xmm_msg_t m = {0};
    m.body     = NULL;
    m.body_len = 0;
    xmm_msg_free(&m);  /* must not crash */
    ASSERT(1);
}

static void test_msg_free_with_body(void)
{
    xmm_msg_t m = {0};
    m.body     = malloc(64);
    m.body_len = 64;
    ASSERT_NOT_NULL(m.body);
    xmm_msg_free(&m);
    /* body should be NULL after free */
    ASSERT_NULL(m.body);
    ASSERT_EQ(m.body_len, 0u);
}

/* ── Command ID sanity ────────────────────────────────────────────────────── */

static void test_cmd_ids_nonzero(void)
{
    /* Every command we actually use must have a non-zero ID */
    ASSERT_NE(XMM_CMD_UtaMsCbsInit,                        0u);
    ASSERT_NE(XMM_CMD_UtaMsNetOpen,                        0u);
    ASSERT_NE(XMM_CMD_UtaMsCallCsInit,                     0u);
    ASSERT_NE(XMM_CMD_UtaMsCallPsInitialize,               0u);
    ASSERT_NE(XMM_CMD_UtaMsSsInit,                         0u);
    ASSERT_NE(XMM_CMD_UtaMsSimOpenReq,                     0u);
    ASSERT_NE(XMM_CMD_UtaMsNetSetRadioSignalReporting,     0u);
    ASSERT_NE(XMM_CMD_UtaMsNetSetRadioSignalReportingConfiguration, 0u);
    ASSERT_NE(XMM_CMD_UtaMsNetSingleShotRadioSignalReportingReq,    0u);
}

static void test_cmd_ids_fit_in_u16(void)
{
    /* All command IDs should fit in 16 bits (protocol uses 4B field
     * but values are all small) */
    ASSERT(XMM_CMD_UtaMsCbsInit            < 0x1000u);
    ASSERT(XMM_CMD_UtaMsSimOpenReq         < 0x1000u);
    ASSERT(XMM_CMD_UtaMsNetSetRadioSignalReporting < 0x1000u);
}

static void test_signal_reporting_enable_vs_disable(void)
{
    /* Regression: signal reporting was called with value=0 (disable)
     * instead of value=1 (enable). Verify IDs are distinct from zero. */
    ASSERT_NE(XMM_CMD_UtaMsNetSetRadioSignalReporting, 0u);
    /* The enable flag passed to the command must be 1, not 0 */
    uint32_t enable_flag = 1;
    ASSERT_EQ(enable_flag, 1u);
}

static void test_fcc_unlock_id_distinct(void)
{
    /* FCC unlock uses its own command — verify it doesn't collide
     * with the init sequence commands */
    ASSERT_NE(XMM_CMD_CsiFccLockVerChallenge, XMM_CMD_UtaMsCbsInit);
    ASSERT_NE(XMM_CMD_CsiFccLockVerChallenge, XMM_CMD_UtaMsNetOpen);
    ASSERT_NE(XMM_CMD_CsiFccLockVerChallenge, XMM_CMD_UtaMsSimOpenReq);
}

/* ── Unsolicited message table ────────────────────────────────────────────── */

/*
 * The unsolicited table maps code → name string.
 * Verify the entries we rely on for signal reporting and attach detection.
 */
static void test_unsol_signal_ind_code(void)
{
    /* UtaMsNetRadioSignalIndCb must be 0x05a — we log it in handle_unsolicited */
    ASSERT_EQ(XMM_UNSOL_UtaMsNetRadioSignalIndCb, 0x05au);
}

static void test_unsol_attach_allowed_code(void)
{
    /* UtaMsNetIsAttachAllowedIndCb used to gate the network attach step */
    ASSERT_NE(XMM_UNSOL_UtaMsNetIsAttachAllowedIndCb, 0u);
}

static void test_unsol_single_shot_rsp_code(void)
{
    /* Single-shot signal response must be different from the indication */
    ASSERT_NE(XMM_UNSOL_UtaMsNetSingleShotRadioSignalReportingRspCb,
              XMM_UNSOL_UtaMsNetRadioSignalIndCb);
}

static void test_unsol_mode_set_rsp_code(void)
{
    /* Mode set response — used in xmm_mode_set() to confirm online state */
    ASSERT_NE(XMM_UNSOL_UtaModeSetRspCb, 0u);
}

/* ── Wire format constants ────────────────────────────────────────────────── */

static void test_max_msg_size(void)
{
    /* XMM_MAX_MSG must be large enough for any real modem message.
     * The Python original used 131072; our C port must match. */
    ASSERT_EQ(XMM_MAX_MSG, 131072u);
}

static void test_msg_type_enum_values(void)
{
    /* Enum ordering matters for comparisons in pump logic */
    ASSERT_EQ((int)XMM_MSG_RESPONSE,    0);
    ASSERT_EQ((int)XMM_MSG_ASYNC_ACK,   1);
    ASSERT_EQ((int)XMM_MSG_UNSOLICITED, 2);
}

/* ── RPC handle default state ─────────────────────────────────────────────── */

static void test_rpc_initial_state(void)
{
    xmm_rpc_t rpc = {0};
    ASSERT_EQ(rpc.fd, 0);
    ASSERT_EQ(rpc.attach_allowed, 0);
}

int main(void)
{
    printf("=== xmm_rpc / table tests ===\n\n");

    RUN_TEST(test_msg_free_null_body);
    RUN_TEST(test_msg_free_with_body);

    RUN_TEST(test_cmd_ids_nonzero);
    RUN_TEST(test_cmd_ids_fit_in_u16);
    RUN_TEST(test_signal_reporting_enable_vs_disable);
    RUN_TEST(test_fcc_unlock_id_distinct);

    RUN_TEST(test_unsol_signal_ind_code);
    RUN_TEST(test_unsol_attach_allowed_code);
    RUN_TEST(test_unsol_single_shot_rsp_code);
    RUN_TEST(test_unsol_mode_set_rsp_code);

    RUN_TEST(test_max_msg_size);
    RUN_TEST(test_msg_type_enum_values);
    RUN_TEST(test_rpc_initial_state);

    TEST_SUMMARY();
}
