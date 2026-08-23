#include "pota_stream_session.h"

#include <stdio.h>
#include <string.h>

#define MOCK_FLASH_SIZE 8192u
#define MOCK_SLOT_A_OFFSET 0u
#define MOCK_SLOT_B_OFFSET 4096u
#define MOCK_SLOT_SIZE 2048u
#define MOCK_PAGE_SIZE 16u
#define MOCK_SECTOR_SIZE 256u

static uint8_t s_flash[MOCK_FLASH_SIZE];
static uint32_t s_pending_count;
static uint32_t s_erase_count;
static uint32_t s_program_count;
static uint32_t s_slot_read_bytes;

static bool flash_read(uint32_t offset, void *buffer, uint32_t size)
{
    if (buffer == NULL || offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset) {
        return false;
    }
    if (offset >= MOCK_SLOT_B_OFFSET &&
        offset < MOCK_SLOT_B_OFFSET + MOCK_SLOT_SIZE) {
        s_slot_read_bytes += size;
    }
    memcpy(buffer, &s_flash[offset], size);
    return true;
}

static bool flash_erase(uint32_t offset, uint32_t size)
{
    if (offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset ||
        (offset % MOCK_SECTOR_SIZE) != 0u || (size % MOCK_SECTOR_SIZE) != 0u) {
        return false;
    }
    s_erase_count++;
    memset(&s_flash[offset], 0xFF, size);
    return true;
}

static bool flash_program(uint32_t offset, const void *data, uint32_t size)
{
    if (data == NULL || offset > MOCK_FLASH_SIZE || size > MOCK_FLASH_SIZE - offset ||
        (offset % MOCK_PAGE_SIZE) != 0u || (size % MOCK_PAGE_SIZE) != 0u) {
        return false;
    }
    s_program_count++;
    memcpy(&s_flash[offset], data, size);
    return true;
}

static bool mark_pending(pota_slot_t slot, uint32_t size, uint32_t crc32,
                         uint32_t security_counter)
{
    (void)slot;
    (void)size;
    (void)crc32;
    (void)security_counter;
    s_pending_count++;
    return true;
}

static bool confirm_active(void)
{
    return true;
}

static bool validate_vector(uint32_t offset, uint32_t size, uint32_t run_offset)
{
    return offset == MOCK_SLOT_B_OFFSET && size != 0u && run_offset != 0u;
}

static bool checkpoint_read(void *context, uint32_t offset,
                            void *data, uint32_t length)
{
    (void)context;
    return flash_read(offset, data, length);
}

static bool checkpoint_program(void *context, uint32_t offset,
                               const void *data, uint32_t length)
{
    (void)context;
    if (data == NULL || offset > MOCK_FLASH_SIZE ||
        length > MOCK_FLASH_SIZE - offset) {
        return false;
    }
    memcpy(&s_flash[offset], data, length);
    return true;
}

static bool expect(const char *name, bool condition)
{
    if (!condition) {
        (void)printf("FAIL %s\n", name);
        return false;
    }
    return true;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    const pota_platform_t platform = {
        .info = {
            .product_id = "DHRT100",
            .hardware_id = "dhrt100",
            .bootloader_version = POTA_PACK_VERSION(0, 1, 0),
            .map_version = 1u,
            .slot_a_partition_id = 1u,
            .slot_b_partition_id = 2u,
            .boot_mode = POTA_BOOT_MODE_DIRECT_AB,
            .active_slot = POTA_SLOT_A,
            .slot_a = {MOCK_SLOT_A_OFFSET, MOCK_SLOT_SIZE, 0x10040000u},
            .slot_b = {MOCK_SLOT_B_OFFSET, MOCK_SLOT_SIZE, 0x101C0000u},
            .flash_page_size = MOCK_PAGE_SIZE,
            .flash_sector_size = MOCK_SECTOR_SIZE,
        },
        .ops = {
            .flash_read = flash_read,
            .flash_erase = flash_erase,
            .flash_program = flash_program,
            .mark_pending = mark_pending,
            .confirm_active = confirm_active,
            .validate_vector = validate_vector,
        },
    };
    pota_stream_session_t session;
    int failed = 0;
    failed += !expect("init", pota_stream_session_init(&session, &platform));

    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.session_id = 7u;
    open.generation = 3u;
    open.capability_mask = POTA_STREAM_CAP_INACTIVE_WRITE | POTA_STREAM_CAP_DURABLE_ACK;
    open.map_version = 1u;
    open.partition_id = 2u;
    open.destination_slot = POTA_SLOT_B;
    open.object_id = 11u;
    open.total_size = 32u;
    open.package_crc32 = pota_crc32_compute("01234567890123456789012345678901", 32u);
    open.identity[0] = 0xA5u;
    open.package_hash[0] = 0x5Au;
    pota_platform_t signed_platform = platform;
    signed_platform.info.require_signature = true;
    pota_stream_session_t signed_session;
    failed += !expect("signed stream init",
                      pota_stream_session_init(&signed_session,
                                               &signed_platform));
    failed += !expect("signed stream rejects raw open",
                      pota_stream_session_open(&signed_session, &open) ==
                          POTA_STREAM_RESULT_CORE &&
                          s_erase_count == 0u && s_program_count == 0u);
    pota_stream_open_t wrong_map = open;
    wrong_map.map_version = 2u;
    failed += !expect("map mismatch rejected",
                      pota_stream_session_open(&session, &wrong_map) ==
                          POTA_STREAM_RESULT_MISMATCH);
    failed += !expect("open", pota_stream_session_open(&session, &open) == POTA_STREAM_RESULT_OK);
    failed += !expect("wrong state before service",
                      pota_stream_session_write(&session, 0u, (const uint8_t *)"01234567890123456789012345678901", 16u) ==
                          POTA_STREAM_RESULT_INVALID_STATE);
    while (pota_stream_session_state(&session) == POTA_STREAM_STATE_OPEN) {
        failed += !expect("service", pota_stream_session_service(&session, 100u) == POTA_STREAM_RESULT_OK);
    }

    const uint8_t first[17] = "0123456789012345";
    const uint8_t second[17] = "6789012345678901";
    const uint32_t stream_token = pota_stream_session_token(&session);
    failed += !expect("first write", pota_stream_session_write(&session, 0u, first, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("offset reject", pota_stream_session_write(&session, 32u, second, 16u) == POTA_STREAM_RESULT_OFFSET);
    failed += !expect("second write", pota_stream_session_write(&session, 16u, second, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("duplicate accepted", pota_stream_session_write(&session, 16u, second, 16u) == POTA_STREAM_RESULT_OK);
    failed += !expect("conflict reject", pota_stream_session_write(&session, 16u, first, 16u) == POTA_STREAM_RESULT_CONFLICT);
    failed += !expect("durable offset", pota_stream_session_durable_offset(&session) == 32u);
    failed += !expect("stable token", stream_token != 0u &&
                      pota_stream_session_token(&session) == stream_token);
    failed += !expect("close", pota_stream_session_close(&session) == POTA_STREAM_RESULT_OK);
    failed += !expect("pending once", s_pending_count == 1u);
    failed += !expect("abort after close rejected", pota_stream_session_abort(&session) == POTA_STREAM_RESULT_INVALID_STATE);

    memset(s_flash, 0xFF, sizeof(s_flash));
    s_pending_count = 0u;
    s_erase_count = 0u;
    s_program_count = 0u;
    s_slot_read_bytes = 0u;
    uint8_t resume_image[1536u];
    for (uint32_t index = 0u; index < sizeof(resume_image); ++index) {
        resume_image[index] = (uint8_t)(index * 17u + 3u);
    }
    pota_stream_open_t resume_open = open;
    resume_open.total_size = sizeof(resume_image);
    resume_open.package_crc32 =
        pota_crc32_compute(resume_image, sizeof(resume_image));
    resume_open.package_hash[1] = 0x6Bu;
    const pota_stream_checkpoint_config_t checkpoint_config = {
        .context = s_flash,
        .read = checkpoint_read,
        .program = checkpoint_program,
        .base_offset = 7168u,
        .slot_count = 8u,
        .slot_size = POTA_STREAM_CHECKPOINT_RECORD_SIZE,
    };
    const pota_stream_checkpoint_policy_t checkpoint_policy = {
        .interval_bytes = 512u,
        .checkpoint_on_final = true,
    };
    pota_stream_checkpoint_store_t checkpoint_store;
    failed += !expect("checkpoint init",
                      pota_stream_checkpoint_init(&checkpoint_store,
                                                  &checkpoint_config) ==
                          POTA_STREAM_CHECKPOINT_OK);
    failed += !expect("resume session init",
                      pota_stream_session_init(&session, &platform));
    failed += !expect("resume checkpoint attach",
                      pota_stream_session_set_checkpoint_store(
                          &session, &checkpoint_store, &checkpoint_policy));
    failed += !expect("resume seed open",
                      pota_stream_session_open(&session, &resume_open) ==
                          POTA_STREAM_RESULT_OK);
    while (pota_stream_session_state(&session) == POTA_STREAM_STATE_OPEN) {
        failed += !expect("resume seed service",
                          pota_stream_session_service(&session, 100u) ==
                              POTA_STREAM_RESULT_OK);
    }
    failed += !expect("resume seed write",
                      pota_stream_session_write(&session, 0u,
                                                resume_image, 512u) ==
                          POTA_STREAM_RESULT_OK);
    failed += !expect("resume seed second write",
                      pota_stream_session_write(&session, 512u,
                                                &resume_image[512u], 512u) ==
                          POTA_STREAM_RESULT_OK);
    const uint32_t erase_count = s_erase_count;
    const uint32_t program_count = s_program_count;
    memset(&s_flash[MOCK_SLOT_B_OFFSET + 1024u], 0x11, 512u);

    pota_stream_checkpoint_store_t recovered_store;
    failed += !expect("checkpoint rebuild",
                      pota_stream_checkpoint_init(&recovered_store,
                                                  &checkpoint_config) ==
                          POTA_STREAM_CHECKPOINT_OK);
    pota_stream_session_t recovered_session;
    failed += !expect("recovered session init",
                      pota_stream_session_init(&recovered_session, &platform));
    failed += !expect("recovered checkpoint attach",
                      pota_stream_session_set_checkpoint_store(
                          &recovered_session, &recovered_store,
                          &checkpoint_policy));

    pota_stream_open_t wrong_token = resume_open;
    wrong_token.package_hash[2] = 1u;
    failed += !expect("resume token mismatch rejected",
                      pota_stream_session_open(&recovered_session,
                                               &wrong_token) ==
                          POTA_STREAM_RESULT_MISMATCH);

    failed += !expect("recovered session reinit",
                      pota_stream_session_init(&recovered_session, &platform));
    failed += !expect("recovered checkpoint reattach",
                      pota_stream_session_set_checkpoint_store(
                          &recovered_session, &recovered_store,
                          &checkpoint_policy));
    const uint32_t read_bytes_before_corrupt_open = s_slot_read_bytes;
    s_flash[MOCK_SLOT_B_OFFSET] ^= 1u;
    failed += !expect("corrupt resume open accepted for bounded validation",
                      pota_stream_session_open(&recovered_session,
                                               &resume_open) ==
                          POTA_STREAM_RESULT_OK);
    failed += !expect("corrupt resume remains bounded",
                      pota_stream_session_state(&recovered_session) ==
                              POTA_STREAM_STATE_OPEN &&
                          s_slot_read_bytes == read_bytes_before_corrupt_open);
    failed += !expect("corrupt resume first bounded service",
                      pota_stream_session_service(&recovered_session, 100u) ==
                              POTA_STREAM_RESULT_OK &&
                          s_slot_read_bytes ==
                              read_bytes_before_corrupt_open + 512u &&
                          pota_stream_session_state(&recovered_session) ==
                              POTA_STREAM_STATE_OPEN);
    failed += !expect("corrupt durable prefix fails during service",
                      pota_stream_session_service(&recovered_session, 100u) ==
                          POTA_STREAM_RESULT_CHECKPOINT);
    s_flash[MOCK_SLOT_B_OFFSET] ^= 1u;

    failed += !expect("recovered session final init",
                      pota_stream_session_init(&recovered_session, &platform));
    failed += !expect("recovered checkpoint final attach",
                      pota_stream_session_set_checkpoint_store(
                          &recovered_session, &recovered_store,
                          &checkpoint_policy));
    failed += !expect("resume open",
                      pota_stream_session_open(&recovered_session,
                                               &resume_open) ==
                          POTA_STREAM_RESULT_OK);
    const uint32_t read_bytes_before_resume = s_slot_read_bytes;
    failed += !expect("resume verification remains bounded",
                      pota_stream_session_state(&recovered_session) ==
                          POTA_STREAM_STATE_OPEN);
    failed += !expect("write rejected until resume verified",
                      pota_stream_session_write(&recovered_session, 1024u,
                                                &resume_image[1024u], 512u) ==
                          POTA_STREAM_RESULT_INVALID_STATE);
    failed += !expect("resume verification first service",
                      pota_stream_session_service(&recovered_session, 100u) ==
                              POTA_STREAM_RESULT_OK &&
                          s_slot_read_bytes == read_bytes_before_resume + 512u &&
                          pota_stream_session_state(&recovered_session) ==
                              POTA_STREAM_STATE_OPEN &&
                          pota_stream_session_durable_offset(
                              &recovered_session) == 0u);
    failed += !expect("resume verification second service",
                      pota_stream_session_service(&recovered_session, 100u) ==
                              POTA_STREAM_RESULT_OK &&
                          s_slot_read_bytes == read_bytes_before_resume + 1024u &&
                          pota_stream_session_state(&recovered_session) ==
                              POTA_STREAM_STATE_OPEN);
    while (pota_stream_session_state(&recovered_session) ==
           POTA_STREAM_STATE_OPEN) {
        failed += !expect("resume tail erase service",
                          pota_stream_session_service(&recovered_session,
                                                      100u) ==
                              POTA_STREAM_RESULT_OK);
    }
    failed += !expect("resume state and offset",
                      pota_stream_session_state(&recovered_session) ==
                              POTA_STREAM_STATE_RECEIVING &&
                          pota_stream_session_durable_offset(
                              &recovered_session) == 1024u);
    failed += !expect("resume preserved prefix and erased tail",
                      s_erase_count == erase_count + 2u &&
                          s_program_count == program_count &&
                          memcmp(&s_flash[MOCK_SLOT_B_OFFSET], resume_image,
                                 1024u) == 0 &&
                          s_flash[MOCK_SLOT_B_OFFSET + 1024u] == 0xFFu &&
                          s_flash[MOCK_SLOT_B_OFFSET + 1535u] == 0xFFu);
    failed += !expect("resume continuation write",
                      pota_stream_session_write(&recovered_session, 1024u,
                                                &resume_image[1024u], 512u) ==
                          POTA_STREAM_RESULT_OK);
    failed += !expect("resume close",
                      pota_stream_session_close(&recovered_session) ==
                          POTA_STREAM_RESULT_OK);
    failed += !expect("resume pending once", s_pending_count == 1u);

    pota_stream_open_t abort_open = resume_open;
    abort_open.session_id++;
    abort_open.generation++;
    abort_open.package_hash[3] = 0x7Cu;
    failed += !expect("abort session init",
                      pota_stream_session_init(&session, &platform));
    failed += !expect("abort checkpoint attach",
                      pota_stream_session_set_checkpoint_store(
                          &session, &recovered_store, &checkpoint_policy));
    failed += !expect("abort stream open",
                      pota_stream_session_open(&session, &abort_open) ==
                          POTA_STREAM_RESULT_OK);
    while (pota_stream_session_state(&session) == POTA_STREAM_STATE_OPEN) {
        failed += !expect("abort stream service",
                          pota_stream_session_service(&session, 100u) ==
                              POTA_STREAM_RESULT_OK);
    }
    failed += !expect("abort stream write",
                      pota_stream_session_write(&session, 0u,
                                                resume_image, 512u) ==
                          POTA_STREAM_RESULT_OK);
    failed += !expect("durable abort",
                      pota_stream_session_abort(&session) ==
                              POTA_STREAM_RESULT_OK &&
                          pota_stream_session_state(&session) ==
                              POTA_STREAM_STATE_ABORTED);

    pota_stream_checkpoint_store_t aborted_store;
    failed += !expect("aborted checkpoint rebuild",
                      pota_stream_checkpoint_init(&aborted_store,
                                                  &checkpoint_config) ==
                          POTA_STREAM_CHECKPOINT_OK);
    pota_stream_session_t restarted_session;
    failed += !expect("aborted session rebuild",
                      pota_stream_session_init(&restarted_session, &platform));
    failed += !expect("aborted checkpoint reattach",
                      pota_stream_session_set_checkpoint_store(
                          &restarted_session, &aborted_store,
                          &checkpoint_policy));
    failed += !expect("same generation rejected after abort",
                      pota_stream_session_open(&restarted_session,
                                               &abort_open) ==
                          POTA_STREAM_RESULT_MISMATCH);
    failed += !expect("new generation session init",
                      pota_stream_session_init(&restarted_session, &platform));
    failed += !expect("new generation checkpoint attach",
                      pota_stream_session_set_checkpoint_store(
                          &restarted_session, &aborted_store,
                          &checkpoint_policy));
    abort_open.generation++;
    failed += !expect("new generation restarts from zero",
                      pota_stream_session_open(&restarted_session,
                                               &abort_open) ==
                              POTA_STREAM_RESULT_OK &&
                          pota_stream_session_durable_offset(
                              &restarted_session) == 0u);
    if (failed != 0) {
        return 1;
    }
    (void)printf("pota stream session tests passed\n");
    return 0;
}
