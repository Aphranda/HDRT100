#ifndef TDMA_PAYLOAD_REGISTRY_H
#define TDMA_PAYLOAD_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_profile.h"

#define TDMA_PAYLOAD_REGISTRY_VERSION 1u
#define TDMA_PAYLOAD_REGISTRY_COUNT 8u
#define TDMA_PAYLOAD_FRAME_CLASS_SHORT 1u
#define TDMA_PAYLOAD_FRAME_CLASS_LONG 2u

typedef enum {
    TDMA_PAYLOAD_REGISTRY_OK = 0u,
    TDMA_PAYLOAD_REGISTRY_BAD_ARGUMENT = 1u,
    TDMA_PAYLOAD_REGISTRY_CLASS_REJECTED = 2u,
    TDMA_PAYLOAD_REGISTRY_CAPACITY_REJECTED = 3u,
    TDMA_PAYLOAD_REGISTRY_FULL = 4u,
    TDMA_PAYLOAD_REGISTRY_NOT_REGISTERED = 5u,
} tdma_payload_registry_result_t;

typedef struct {
    uint32_t used;
    uint32_t producer_id;
    uint32_t consumer_id;
    uint32_t payload_class;
    uint32_t frame_class;
    uint32_t max_payload_size;
    uint32_t flags;
} tdma_payload_binding_t;

typedef struct {
    uint32_t version;
    uint32_t config_seq;
    uint32_t registration_seq;
    uint32_t payload_whitelist_mask;
    uint32_t short_frame_capacity;
    uint32_t long_frame_capacity;
    uint32_t used_count;
    uint32_t admitted_count;
    uint32_t reject_count;
    uint32_t last_result;
    uint32_t last_payload_class;
} tdma_payload_registry_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    volatile uint32_t config_seq;
    volatile uint32_t registration_seq;
    volatile uint32_t payload_whitelist_mask;
    volatile uint32_t short_frame_capacity;
    volatile uint32_t long_frame_capacity;
    volatile uint32_t used_count;
    volatile uint32_t admitted_count;
    volatile uint32_t reject_count;
    volatile uint32_t last_result;
    volatile uint32_t last_payload_class;
    tdma_payload_binding_t binding[TDMA_PAYLOAD_REGISTRY_COUNT];
} tdma_payload_registry_t;

bool tdma_payload_registry_init(tdma_payload_registry_t *registry,
                                uint32_t short_frame_capacity,
                                uint32_t long_frame_capacity);
bool tdma_payload_registry_configure(tdma_payload_registry_t *registry,
                                     uint32_t payload_whitelist_mask,
                                     uint32_t short_frame_capacity,
                                     uint32_t long_frame_capacity);
bool tdma_payload_registry_register(tdma_payload_registry_t *registry,
                                    const tdma_payload_binding_t *binding);
bool tdma_payload_registry_admit(tdma_payload_registry_t *registry,
                                 uint32_t frame_class,
                                 uint32_t payload_class,
                                 size_t frame_size);
bool tdma_payload_registry_get_snapshot(
    const tdma_payload_registry_t *registry,
    tdma_payload_registry_snapshot_t *snapshot);

#endif
