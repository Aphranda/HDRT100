#ifndef TDMA_SEAT_MOUNT_STORE_H
#define TDMA_SEAT_MOUNT_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_seat_admission.h"

/* The seat mount table names which seat each load rides. A seat is a capacity;
 * a load is what is mounted on it, so this table never equates the two. The
 * table is configured over SCPI, persisted through the flash store and loaded
 * into memory at boot; the publisher only looks the mounting up. */
#define TDMA_SEAT_MOUNT_STORE_SCHEMA_VERSION 1u
#define TDMA_SEAT_MOUNT_ENTRY_COUNT 8u
#define TDMA_SEAT_MOUNT_PAYLOAD_SIZE \
    (4u + TDMA_SEAT_MOUNT_ENTRY_COUNT * 2u)

typedef struct {
    uint8_t message_class;
    uint8_t seat_class;
} tdma_seat_mount_entry_t;

typedef struct {
    uint32_t count;
    tdma_seat_mount_entry_t entries[TDMA_SEAT_MOUNT_ENTRY_COUNT];
} tdma_seat_mount_table_t;

static inline bool tdma_seat_mount_seat_valid(uint8_t seat_class)
{
    return seat_class == (uint8_t)TDMA_LOOP_SLOT_PRIORITY ||
        seat_class == (uint8_t)TDMA_LOOP_SLOT_FIRST_CLASS ||
        seat_class == (uint8_t)TDMA_LOOP_SLOT_LOAD;
}

/* Explicit little-endian payload: count then count pairs of (class, seat).
 * Zero-length, oversized or invalid entries are rejected, and encoding always
 * writes the full payload size so a record has a fixed span. */
static inline bool tdma_seat_mount_encode(const tdma_seat_mount_table_t *table,
    uint8_t *payload, uint32_t capacity)
{
    if (table == NULL || payload == NULL || capacity < TDMA_SEAT_MOUNT_PAYLOAD_SIZE ||
        table->count == 0u || table->count > TDMA_SEAT_MOUNT_ENTRY_COUNT)
        return false;
    payload[0] = (uint8_t)table->count;
    payload[1] = 0u; payload[2] = 0u; payload[3] = 0u;
    for (uint32_t index = 0u; index < TDMA_SEAT_MOUNT_ENTRY_COUNT; ++index) {
        const uint32_t at = 4u + index * 2u;
        if (index < table->count) {
            if (table->entries[index].message_class == 0u ||
                !tdma_seat_mount_seat_valid(table->entries[index].seat_class))
                return false;
            payload[at] = table->entries[index].message_class;
            payload[at + 1u] = table->entries[index].seat_class;
        } else {
            payload[at] = 0u; payload[at + 1u] = 0u;
        }
    }
    return true;
}

static inline bool tdma_seat_mount_decode(const uint8_t *payload, uint32_t length,
    tdma_seat_mount_table_t *table)
{
    if (payload == NULL || table == NULL || length < TDMA_SEAT_MOUNT_PAYLOAD_SIZE)
        return false;
    table->count = payload[0];
    if (table->count == 0u || table->count > TDMA_SEAT_MOUNT_ENTRY_COUNT) return false;
    for (uint32_t index = 0u; index < TDMA_SEAT_MOUNT_ENTRY_COUNT; ++index) {
        const uint32_t at = 4u + index * 2u;
        table->entries[index].message_class = payload[at];
        table->entries[index].seat_class = payload[at + 1u];
        if (index < table->count &&
            (table->entries[index].message_class == 0u ||
             !tdma_seat_mount_seat_valid(table->entries[index].seat_class)))
            return false;
    }
    return true;
}

/* Store lifecycle: load at boot, read the resident table, and persist a new
 * table through the flash store (generation-selected, commit-marked). Each of
 * these returns false on failure; a failed load leaves the table unmounted. */
bool tdma_seat_mount_store_init(void);
bool tdma_seat_mount_store_get(tdma_seat_mount_table_t *table);
bool tdma_seat_mount_store_save(const tdma_seat_mount_table_t *table, uint32_t generation);
/* Clear the resident table: every load is then unmounted, so no seat window
 * constrains publication until a new table is loaded. */
bool tdma_seat_mount_store_clear(void);
/* Diagnostics for the last load/save attempt: a reject code (0 = none) and the
 * generation of the record currently held in memory. */
void tdma_seat_mount_store_status(uint32_t *last_reject, uint32_t *record_generation);
/* Record why an external caller (SCPI) rejected a table before reachi
 * the store, so the reason is readable from the same status query. */
void tdma_seat_mount_store_note_reject(uint32_t code);

/* Content fingerprint of a mount table: CRC-32 (reflected, 0xEDB88320) over
 * little-endian words -- generation, count, then one word per entry holding the
 * message class in the low byte and the seat class in the next byte. The host
 * tool and SCPI both use this so a table cannot be applied half-written. */
static inline uint32_t tdma_seat_mount_crc(uint32_t generation,
    const tdma_seat_mount_table_t *table)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t words[2] = {generation, 0u};
    if (table != NULL) words[1] = table->count;
    for (uint32_t index = 0u; index < 2u; ++index) {
        uint32_t word = words[index];
        for (uint32_t byte = 0u; byte < 4u; ++byte) {
            crc ^= word & 0xFFu;
            word >>= 8u;
            for (uint32_t bit = 0u; bit < 8u; ++bit)
                crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    if (table != NULL) {
        for (uint32_t index = 0u; index < table->count; ++index) {
            uint32_t word = (uint32_t)table->entries[index].message_class |
                ((uint32_t)table->entries[index].seat_class << 8u);
            for (uint32_t byte = 0u; byte < 4u; ++byte) {
                crc ^= word & 0xFFu;
                word >>= 8u;
                for (uint32_t bit = 0u; bit < 8u; ++bit)
                    crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* Seat the load rides, or NONE when it is not mounted on a scheduled seat. */
static inline tdma_loop_slot_class_t tdma_seat_mount_lookup(
    const tdma_seat_mount_table_t *table, uint8_t message_class)
{
    if (table == NULL) return TDMA_LOOP_SLOT_NONE;
    for (uint32_t index = 0u; index < table->count && index < TDMA_SEAT_MOUNT_ENTRY_COUNT;
         ++index) {
        if (table->entries[index].message_class == message_class)
            return (tdma_loop_slot_class_t)table->entries[index].seat_class;
    }
    return TDMA_LOOP_SLOT_NONE;
}

#endif
