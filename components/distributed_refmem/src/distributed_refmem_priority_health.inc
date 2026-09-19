/* The sole Core0 RefMem task owns this asynchronous diagnostic extension.
 * No Core1 copy/checksum, TDMA mailbox, or deterministic service reservation.
 * The legacy model and source diagnostic revisions are independent; neither
 * implies the other. A sampled age cannot authorize live control/LOCKED. */
#include "vdc_priority_follow.h"
_Static_assert(sizeof(vdc_priority_follow_health_t) == REFMEM_VDC_PRIORITY_SOURCE_WORDS * 4u,
               "typed source schema 1 must match the diagnostic extension");
static void distributed_refmem_publish_priority_health_core0(void)
{
    vdc_priority_follow_health_t health;
    refmem_vdc_priority_payload_t value = {
        .schema = REFMEM_VDC_PRIORITY_SCHEMA,
        .payload_bytes = sizeof(refmem_vdc_priority_payload_t),
        .writer = REFMEM_VECTOR_WRITER_CORE0};
    if (!vdc_dpll_manager_get_priority_follow_health_revision(&health,
            &value.source_revision) || health.schema != 1u) return;
    refmem_vdc_priority_region_t *region = &distributed_refmem_vdc_vector_region()->priority;
    /* Sole Core0 writer: no payload reader/CRC pass needed for local dedup.
     * Zero revision is valid after wrap; schema distinguishes empty SRAM. */
    if (__atomic_load_n(&region->words[0], __ATOMIC_RELAXED) == value.schema &&
        __atomic_load_n(&region->words[3], __ATOMIC_RELAXED) == value.source_revision)
        return;
    memcpy(value.source_words, &health, sizeof(health));
    value.payload_crc32 = refmem_vector_fast_crc32(&value,
        offsetof(refmem_vdc_priority_payload_t, payload_crc32));
    const uint32_t guard = __atomic_load_n(&region->guard, __ATOMIC_RELAXED);
    __atomic_store_n(&region->guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&value + i * 4u, 4u);
        __atomic_store_n(region->words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&region->guard, guard + 2u, __ATOMIC_RELEASE);
}

bool distributed_refmem_get_vdc_priority_snapshot(refmem_vdc_priority_payload_t *out)
{
    if (!out || !s_initialized) return false;
    const refmem_vdc_priority_region_t *region = &distributed_refmem_vdc_vector_region()->priority;
    const uint32_t guard = __atomic_load_n(&region->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    refmem_vdc_priority_payload_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(region->words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + i * 4u, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&region->guard, __ATOMIC_ACQUIRE) ||
        value.schema != REFMEM_VDC_PRIORITY_SCHEMA || value.payload_bytes != sizeof(value) ||
        value.writer != REFMEM_VECTOR_WRITER_CORE0 || (value.source_revision & 1u) ||
        __atomic_load_n(&region->reserved, __ATOMIC_RELAXED) ||
        value.reserved || value.source_words[13] || value.source_words[0] != 1u ||
        value.source_words[1] > VDC_PRIORITY_HEALTH_RETIRED ||
        value.payload_crc32 != refmem_vector_fast_crc32(&value,
            offsetof(refmem_vdc_priority_payload_t, payload_crc32))) return false;
    *out = value;
    return true;
}
