#include "pota_metadata.h"
#include "pota_direct_ab.h"

#include <stdio.h>
#include <string.h>

static pota_metadata_t make_metadata(uint32_t sequence)
{
    pota_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = POTA_METADATA_MAGIC;
    metadata.version = POTA_METADATA_VERSION;
    metadata.sequence = sequence;
    metadata.active_slot = (uint32_t)POTA_SLOT_A;
    metadata.confirmed_slot = (uint32_t)POTA_SLOT_A;
    metadata.boot_mode = (uint32_t)POTA_BOOT_MODE_COPY_TO_ACTIVE;
    metadata.boot_capabilities = POTA_BOOT_CAP_COPY_TO_ACTIVE;
    pota_metadata_update_crc(&metadata);
    return metadata;
}

static int expect_true(const char *name, bool value)
{
    if (!value) {
        (void)printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

static int expect_false(const char *name, bool value)
{
    if (value) {
        (void)printf("%s: expected false\n", name);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu, got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    pota_metadata_t copies[3];
    copies[0] = make_metadata(1u);
    copies[1] = make_metadata(3u);
    copies[2] = make_metadata(2u);

    failed += expect_true("valid metadata", pota_metadata_is_valid(&copies[0]));

    pota_metadata_t mutation_seed = copies[0];
    uint8_t *mutation_bytes = (uint8_t *)&mutation_seed;
    for (size_t offset = 0u; offset < sizeof(mutation_seed); offset++) {
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            mutation_bytes[offset] ^= (uint8_t)(1u << bit);
            if (pota_metadata_is_valid(&mutation_seed)) {
                (void)printf("single-bit metadata mutation accepted: offset=%lu bit=%lu\n",
                             (unsigned long)offset,
                             (unsigned long)bit);
                failed++;
            }
            mutation_bytes[offset] ^= (uint8_t)(1u << bit);
        }
    }

    pota_metadata_t corrupted = copies[1];
    corrupted.active_slot = (uint32_t)POTA_SLOT_B;
    failed += expect_false("corrupted metadata", pota_metadata_is_valid(&corrupted));

    pota_metadata_t ext_corrupted = copies[1];
    ext_corrupted.copy_txn_state = (uint32_t)POTA_COPY_TXN_FAILED + 1u;
    ext_corrupted.metadata_ext_crc32 = pota_metadata_ext_crc32(&ext_corrupted);
    ext_corrupted.metadata_ab_crc32 = pota_metadata_ab_crc32(&ext_corrupted);
    failed += expect_false("invalid copy transaction state",
                           pota_metadata_is_valid(&ext_corrupted));

    pota_metadata_t ab_corrupted = copies[1];
    ab_corrupted.previous_slot = 99u;
    ab_corrupted.metadata_ext_crc32 = pota_metadata_ext_crc32(&ab_corrupted);
    ab_corrupted.metadata_ab_crc32 = pota_metadata_ab_crc32(&ab_corrupted);
    failed += expect_false("invalid previous slot", pota_metadata_is_valid(&ab_corrupted));

    pota_metadata_t defaults;
    pota_metadata_set_default(&defaults);
    failed += expect_true("default metadata valid", pota_metadata_is_valid(&defaults));
    if (defaults.active_slot != (uint32_t)POTA_SLOT_A ||
        defaults.confirmed_slot != (uint32_t)POTA_SLOT_A ||
        defaults.pending_slot != (uint32_t)POTA_SLOT_NONE) {
        (void)printf("default metadata slots: expected A/NONE/A\n");
        failed++;
    }

    pota_metadata_t txn = defaults;
    txn.copy_txn_state = (uint32_t)POTA_COPY_TXN_PROGRAMMING;
    txn.copy_source_slot = (uint32_t)POTA_SLOT_B;
    txn.copy_destination_slot = (uint32_t)POTA_SLOT_A;
    txn.copy_size = 1024u;
    txn.copy_crc32 = 0x12345678u;
    txn.copy_written = 512u;
    txn.copy_attempts = 2u;
    txn.copy_last_error = 7u;
    pota_metadata_clear_copy_transaction_fields(&txn);
    pota_metadata_update_crc(&txn);
    failed += expect_true("cleared transaction valid", pota_metadata_is_valid(&txn));
    if (txn.copy_txn_state != (uint32_t)POTA_COPY_TXN_NONE ||
        txn.copy_source_slot != (uint32_t)POTA_SLOT_NONE ||
        txn.copy_destination_slot != (uint32_t)POTA_SLOT_NONE ||
        txn.copy_size != 0u ||
        txn.copy_attempts != 0u) {
        (void)printf("cleared transaction fields: expected zeros/NONE\n");
        failed++;
    }

    pota_metadata_t pending = defaults;
    failed += expect_true("mark pending slot b",
                          pota_metadata_mark_pending(&pending, POTA_SLOT_B, 4096u, 0xA5A5A5A5u));
    failed += expect_true("pending metadata valid", pota_metadata_is_valid(&pending));
    failed += expect_u32("pending slot", pending.pending_slot, (uint32_t)POTA_SLOT_B);
    failed += expect_u32("pending slot b size", pending.slot_b_size, 4096u);
    failed += expect_u32("pending boot result", pending.last_boot_result, (uint32_t)POTA_BOOT_RESULT_NONE);
    failed += expect_false("mark pending none",
                           pota_metadata_mark_pending(&pending, POTA_SLOT_NONE, 4096u, 0u));

    pending.active_slot = (uint32_t)POTA_SLOT_B;
    pending.pending_slot = (uint32_t)POTA_SLOT_NONE;
    pending.last_boot_result = (uint32_t)POTA_BOOT_RESULT_APPLIED;
    pending.slot_b_size = 4096u;
    pending.slot_b_crc32 = 0xA5A5A5A5u;
    pota_metadata_update_crc(&pending);
    failed += expect_true("can confirm active", pota_metadata_can_confirm_active(&pending));
    failed += expect_true("confirm active", pota_metadata_confirm_active(&pending));
    failed += expect_true("confirmed metadata valid", pota_metadata_is_valid(&pending));
    failed += expect_u32("confirmed slot", pending.confirmed_slot, (uint32_t)POTA_SLOT_B);
    failed += expect_u32("confirm clears pending", pending.pending_slot, (uint32_t)POTA_SLOT_NONE);
    failed += expect_u32("confirm boot result", pending.last_boot_result, (uint32_t)POTA_BOOT_RESULT_APPLIED);

    pota_metadata_t blocked_confirm = defaults;
    failed += expect_false("cannot confirm without applied result",
                           pota_metadata_can_confirm_active(&blocked_confirm));
    failed += expect_false("confirm rejects without applied result",
                           pota_metadata_confirm_active(&blocked_confirm));

    pota_metadata_t mode = defaults;
    mode.pending_slot = (uint32_t)POTA_SLOT_B;
    mode.boot_attempts = 2u;
    mode.copy_txn_state = (uint32_t)POTA_COPY_TXN_PROGRAMMING;
    failed += expect_true("set direct ab",
                          pota_metadata_set_boot_mode(&mode, POTA_BOOT_MODE_DIRECT_AB));
    failed += expect_true("direct ab metadata valid", pota_metadata_is_valid(&mode));
    failed += expect_u32("direct ab mode", mode.boot_mode, (uint32_t)POTA_BOOT_MODE_DIRECT_AB);
    failed += expect_u32("direct ab caps",
                         mode.boot_capabilities,
                         POTA_BOOT_CAP_COPY_TO_ACTIVE | POTA_BOOT_CAP_DIRECT_AB);
    failed += expect_u32("direct ab clears pending", mode.pending_slot, (uint32_t)POTA_SLOT_NONE);
    failed += expect_u32("direct ab clears txn", mode.copy_txn_state, (uint32_t)POTA_COPY_TXN_NONE);

    pota_metadata_t fault = defaults;
    failed += expect_true("set fault injection",
                          pota_metadata_set_fault_injection(&fault, POTA_FAULT_INJECT_COPY_FAIL));
    failed += expect_true("fault metadata valid", pota_metadata_is_valid(&fault));
    failed += expect_u32("fault flag", fault.fault_injection_flags, POTA_FAULT_INJECT_COPY_FAIL);

    pota_metadata_t copy = defaults;
    failed += expect_true("begin copy transaction",
                          pota_metadata_begin_copy_transaction(&copy,
                                                               POTA_SLOT_B,
                                                               POTA_SLOT_A,
                                                               8192u,
                                                               0x12345678u));
    failed += expect_u32("copy started", copy.copy_txn_state, (uint32_t)POTA_COPY_TXN_STARTED);
    failed += expect_u32("copy attempts", copy.copy_attempts, 1u);
    failed += expect_true("update copy transaction",
                          pota_metadata_update_copy_transaction(&copy,
                                                                (uint32_t)POTA_COPY_TXN_PROGRAMMING,
                                                                4096u,
                                                                0u));
    failed += expect_u32("copy programmed", copy.copy_txn_state, (uint32_t)POTA_COPY_TXN_PROGRAMMING);
    failed += expect_u32("copy written", copy.copy_written, 4096u);
    failed += expect_false("copy written too large",
                           pota_metadata_update_copy_transaction(&copy,
                                                                 (uint32_t)POTA_COPY_TXN_PROGRAMMING,
                                                                 8193u,
                                                                 0u));
    failed += expect_true("finish copy transaction", pota_metadata_finish_copy_transaction(&copy));
    failed += expect_u32("copy done", copy.copy_txn_state, (uint32_t)POTA_COPY_TXN_DONE);
    failed += expect_u32("copy done written", copy.copy_written, 8192u);
    failed += expect_true("fail copy transaction after done",
                          pota_metadata_fail_copy_transaction(&copy, 5u));
    failed += expect_u32("copy failed", copy.copy_txn_state, (uint32_t)POTA_COPY_TXN_FAILED);
    failed += expect_u32("copy last error", copy.copy_last_error, 5u);
    failed += expect_true("clear copy transaction", pota_metadata_clear_copy_transaction(&copy));
    failed += expect_true("copy metadata valid", pota_metadata_is_valid(&copy));
    failed += expect_u32("copy cleared", copy.copy_txn_state, (uint32_t)POTA_COPY_TXN_NONE);

    pota_metadata_t boot = defaults;
    boot.slot_b_size = 12288u;
    boot.slot_b_crc32 = 0xBEEFBEEFu;
    failed += expect_true("copy-to-active done",
                          pota_metadata_apply_copy_to_active_done(&boot, POTA_SLOT_B, POTA_SLOT_A));
    failed += expect_true("copy-to-active metadata valid", pota_metadata_is_valid(&boot));
    failed += expect_u32("copy-to-active active", boot.active_slot, (uint32_t)POTA_SLOT_A);
    failed += expect_u32("copy-to-active confirmed", boot.confirmed_slot, (uint32_t)POTA_SLOT_A);
    failed += expect_u32("copy-to-active slot a size", boot.slot_a_size, 12288u);
    failed += expect_u32("copy-to-active result", boot.last_boot_result, (uint32_t)POTA_BOOT_RESULT_APPLIED);
    failed += expect_u32("copy-to-active source", boot.last_boot_source_slot, (uint32_t)POTA_SLOT_B);

    pota_metadata_t direct = defaults;
    direct.boot_mode = (uint32_t)POTA_BOOT_MODE_DIRECT_AB;
    direct.active_slot = (uint32_t)POTA_SLOT_A;
    direct.confirmed_slot = (uint32_t)POTA_SLOT_A;
    direct.pending_slot = (uint32_t)POTA_SLOT_B;
    direct.slot_b_size = 16384u;
    direct.slot_b_crc32 = 0x1234ABCDu;
    pota_metadata_update_crc(&direct);
    failed += expect_true("direct pending apply",
                          pota_metadata_apply_direct_ab_pending(&direct, POTA_SLOT_B));
    failed += expect_true("direct pending metadata valid", pota_metadata_is_valid(&direct));
    failed += expect_u32("direct active", direct.active_slot, (uint32_t)POTA_SLOT_B);
    failed += expect_u32("direct previous", direct.previous_slot, (uint32_t)POTA_SLOT_A);
    failed += expect_u32("direct pending cleared", direct.pending_slot, (uint32_t)POTA_SLOT_NONE);
    failed += expect_u32("direct boot attempts", direct.boot_attempts, 1u);
    failed += expect_u32("direct boot generation", direct.boot_generation, 1u);

    failed += expect_true("direct increment boot attempts",
                          pota_metadata_increment_boot_attempts(&direct));
    failed += expect_u32("direct boot attempts incremented", direct.boot_attempts, 2u);

    failed += expect_true("direct rollback",
                          pota_metadata_rollback_direct_ab(&direct,
                                                           POTA_BOOT_RESULT_MAX_ATTEMPTS,
                                                           POTA_SLOT_B,
                                                           POTA_SLOT_A));
    failed += expect_true("direct rollback metadata valid", pota_metadata_is_valid(&direct));
    failed += expect_u32("rollback active", direct.active_slot, (uint32_t)POTA_SLOT_A);
    failed += expect_u32("rollback previous", direct.previous_slot, (uint32_t)POTA_SLOT_B);
    failed += expect_u32("rollback attempts", direct.boot_attempts, 0u);
    failed += expect_u32("rollback count", direct.rollback_count, 1u);
    failed += expect_u32("rollback result", direct.last_boot_result, (uint32_t)POTA_BOOT_RESULT_MAX_ATTEMPTS);

    pota_direct_ab_decision_t decision;
    direct.pending_slot = (uint32_t)POTA_SLOT_B;
    direct.boot_attempts = 1u;
    pota_metadata_update_crc(&direct);
    failed += expect_true("direct decision boot pending",
                          pota_direct_ab_decide(&direct, 3u, &decision));
    failed += expect_u32("direct decision kind",
                         decision.kind,
                         POTA_DIRECT_AB_DECISION_BOOT_PENDING);
    failed += expect_u32("direct decision slot",
                         decision.pending_slot,
                         (uint32_t)POTA_SLOT_B);

    direct.boot_attempts = 3u;
    pota_metadata_update_crc(&direct);
    failed += expect_true("direct decision rollback",
                          pota_direct_ab_decide(&direct, 3u, &decision));
    failed += expect_u32("direct rollback decision kind",
                         decision.kind,
                         POTA_DIRECT_AB_DECISION_ROLLBACK);
    failed += expect_u32("direct rollback decision slot",
                         decision.rollback_slot,
                         (uint32_t)POTA_SLOT_A);

    direct.pending_slot = (uint32_t)POTA_SLOT_NONE;
    direct.boot_attempts = 0u;
    pota_metadata_update_crc(&direct);
    failed += expect_true("direct decision no pending",
                          pota_direct_ab_decide(&direct, 3u, &decision));
    failed += expect_u32("direct no pending kind",
                         decision.kind,
                         POTA_DIRECT_AB_DECISION_NO_PENDING);

    failed += expect_true("record boot result clear pending",
                          pota_metadata_record_boot_result(&direct,
                                                           POTA_BOOT_RESULT_STAGE_VALIDATE_FAILED,
                                                           POTA_SLOT_B,
                                                           true));
    failed += expect_true("record boot result metadata valid", pota_metadata_is_valid(&direct));
    failed += expect_u32("record clears pending", direct.pending_slot, (uint32_t)POTA_SLOT_NONE);
    failed += expect_u32("record rollback count", direct.rollback_count, 2u);

    const pota_metadata_t *selected = pota_metadata_select_newest(copies, 3u);
    if (selected != &copies[1]) {
        (void)printf("select newest: expected sequence 3\n");
        failed++;
    }

    copies[1].metadata_crc32 ^= 0x1u;
    selected = pota_metadata_select_newest(copies, 3u);
    if (selected != &copies[2]) {
        (void)printf("select newest after corruption: expected sequence 2\n");
        failed++;
    }

    copies[0].metadata_crc32 ^= 0x1u;
    copies[2].metadata_crc32 ^= 0x1u;
    selected = pota_metadata_select_newest(copies, 3u);
    if (selected != NULL) {
        (void)printf("select newest all invalid: expected NULL\n");
        failed++;
    }

    return failed == 0 ? 0 : 1;
}
