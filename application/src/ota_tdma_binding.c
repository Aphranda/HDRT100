#include "ota_tdma_binding.h"

#include <stddef.h>

#include "ota_tdma_carrier.h"
#include "ota_tdma_relay.h"
#include "board.h"
#include "resource_arbiter.h"
#include "tdma_second_class_parser.h"
#include "portable_ota_port.h"
#include "pota_stream_wire.h"
#include "ota_tdma_session.h"
#include "drv_watchdog.h"
#include "tdma_maintenance_runtime.h"

/* FreeRTOS heap allocation keeps this ~250-byte state out of .bss: the target
 * image is already at the RAM ceiling, and the binding only exists while a
 * static maintenance transfer is possible. */
extern void *pvPortMalloc(size_t size);

#define OTA_TDMA_BINDING_STREAM_ID 0x4Fu
#define OTA_TDMA_BINDING_MAX_SLOT 1u

typedef struct {
    uint32_t local_slot;
    ota_tdma_carrier_t carrier;
    ota_tdma_session_t session;
    ota_tdma_transport_t transport;
} ota_tdma_binding_ctx_t;

static ota_tdma_binding_ctx_t s_ctx_storage;
static ota_tdma_binding_ctx_t *s_ctx;
static bool s_active;
static ota_tdma_relay_t s_relay_storage;
static ota_tdma_relay_t *s_relay;
static ota_tdma_binding_counters_t s_counters;
static uint32_t s_last_result;

static uint32_t ota_tdma_binding_crc32(const uint8_t *data, uint32_t length)
{
    return portable_ota_port_crc32_compute(data, (size_t)length);
}

static bool ota_tdma_binding_relay_send(void *context, const uint8_t *frame,
                                       size_t length)
{
    (void)context;
    s_counters.relay_frames_tx++;
    return ota_tdma_carrier_send(&s_ctx->carrier, frame, length);
}

static bool ota_tdma_binding_reset_target(void)
{
    return ota_tdma_session_init_target_for_node(&s_ctx->session,
                                                 &s_ctx->transport,
                                                 (uint8_t)s_ctx->local_slot);
}

static uint32_t s_last_trace;
static uint32_t s_relay_wire_code;
static uint32_t s_diag[26]; /* see ota_tdma_binding_diag() */
static uint32_t s_stray_relay; /* frames seen while a local relay was active */
static uint32_t s_entry[6]; /* size, b0, b1, b2, b3, count */

uint32_t ota_tdma_binding_relay_wire_code(void)
{
    return s_relay_wire_code;
}

uint32_t ota_tdma_last_trace(void)
{
    return s_last_trace;
}

void ota_tdma_trace(uint32_t code);

void pota_wire_trace(uint32_t code)
{
    ota_tdma_trace(code);
}

void ota_tdma_trace(uint32_t code)
{
    s_last_trace = code;
    s_diag[0] = code;
}

void ota_tdma_binding_diag(uint32_t out[26])
{
    if (out != NULL) {
        for (uint32_t i = 0; i < 24u; ++i) {
            out[i] = i < 16u ? s_diag[i]
                             : (i < 22u ? s_entry[i - 16u] : 0u);
        }
        out[22] = s_entry[5];
        out[23] = s_diag[5];
        out[24] = s_relay != NULL && ota_tdma_relay_active(s_relay) ? 1u : 0u;
        out[25] = s_stray_relay;
        out[23] = ota_tdma_ingress_last_session_result();
    }
}

bool ota_tdma_binding_init(void)
{
    uint32_t local_slot = 0u;
    if (s_ctx == NULL) {
        s_ctx = &s_ctx_storage;
    }
    if (s_ctx == NULL) {
        s_active = false;
        return false;
    }
    if (!tdma_maintenance_runtime_get_local_slot(&local_slot) ||
        local_slot > OTA_TDMA_BINDING_MAX_SLOT) {
        /* v1: the maintenance runtime does not report the source slot of a
         * completed frame, so the carrier can only ACK a single known peer. */
        s_active = false;
        return false;
    }
    const uint8_t peer_slot = (uint8_t)(local_slot == 0u ? 1u : 0u);
    const uint8_t target_mask = (uint8_t)(1u << peer_slot);
    if (!ota_tdma_carrier_init(&s_ctx->carrier,
                               tdma_maintenance_runtime_submit,
                               tdma_maintenance_runtime_take,
                               tdma_maintenance_runtime_ack,
                               OTA_TDMA_BINDING_STREAM_ID, target_mask,
                               peer_slot)) {
        s_active = false;
        return false;
    }
    s_ctx->local_slot = local_slot;
    s_ctx->transport.send = ota_tdma_carrier_send;
    s_ctx->transport.context = &s_ctx->carrier;
    if (!ota_tdma_binding_reset_target()) {
        s_active = false;
        return false;
    }
    s_active = true;
    return true;
}

void ota_tdma_binding_service(uint32_t now_ms, uint8_t max_frames)
{
    if (!s_active) {
        return;
    }
    if (s_relay != NULL) {
        /* Received frames are delivered by the registered maintenance parser;
         * the service path only advances the source retry timer. */
        const bool was_active = ota_tdma_relay_active(s_relay);
        (void)ota_tdma_relay_tick(s_relay, now_ms);
        if (was_active && !ota_tdma_relay_active(s_relay)) {
            /* The transfer finished: release the OTA admission lease that the
             * relay OPEN acquired so a later maintenance command is not
             * blocked forever. */
            resource_arbiter_release_ota_admission();
        }
        return;
    }
    /* Target frames arrive through the registered maintenance parser; the AO
     * service only needs the relay retry timer here. */
    (void)now_ms;
    (void)max_frames;
    const ota_tdma_session_state_t state =
        ota_tdma_session_state(&s_ctx->session);
    if (state == OTA_TDMA_SESSION_DONE || state == OTA_TDMA_SESSION_FAILED) {
        /* A finished transfer releases the role for the next OPEN frame. */
        (void)ota_tdma_binding_reset_target();
    }
}

bool ota_tdma_binding_active(void)
{
    return s_active;
}

bool ota_tdma_binding_relay_start(const pota_stream_open_t *open)
{
    if (open == NULL) {
        return false;
    }
    if (!s_active && !ota_tdma_binding_init()) {
        return false;
    }
    if (s_relay != NULL && ota_tdma_relay_active(s_relay)) {
        return false; /* a relay is already in flight */
    }
    if (s_ctx == NULL) {
        s_ctx = &s_ctx_storage;
    }
    if (s_relay == NULL) {
        s_relay = &s_relay_storage;
    }
    uint8_t wire[POTA_STREAM_OPEN_WIRE_SIZE] = {0};
    for (uint8_t index = 0u; index < 9u; ++index) {
        const uint32_t value =
            index == 0u ? open->session_id
                        : index == 1u ? open->generation
                                      : index == 2u ? open->capability_mask
                                                    : index == 3u ? open->map_version
                                                                  : index == 4u ? open->partition_id
                                                                                : index == 5u ? open->destination_slot
                                                                                              : index == 6u ? open->object_id
                                                                                                            : index == 7u ? open->total_size
                                                                                                                          : open->package_crc32;
        wire[index * 4u + 0u] = (uint8_t)(value & 0xFFu);
        wire[index * 4u + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
        wire[index * 4u + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
        wire[index * 4u + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
    }
    wire[36] = open->package_mode ? 1u : 0u;
    wire[37] = (open->capability_mask & POTA_STREAM_CAP_RELAY) != 0u
                   ? open->relay_node
                   : 0u;
    memcpy(&wire[POTA_STREAM_OPEN_IDENTITY_OFFSET], open->identity,
           sizeof(open->identity));
    memcpy(&wire[POTA_STREAM_OPEN_PACKAGE_HASH_OFFSET], open->package_hash,
           sizeof(open->package_hash));
    s_relay_wire_code = (uint32_t)wire[36] * 1000u +
                        (uint32_t)wire[37] * 100u +
                        (uint32_t)wire[38] * 10u + (uint32_t)wire[39];
    const ota_tdma_transport_t transport = {
        .send = ota_tdma_binding_relay_send,
        .context = NULL,
    };
    return ota_tdma_relay_open(s_relay, &transport, wire, open->total_size,
                               open->package_crc32, 64u, 3u, 100u,
                               ota_tdma_binding_crc32);
}

bool ota_tdma_binding_relay_feed(uint32_t offset, const uint8_t *data,
                                 uint32_t size)
{
    if (s_relay == NULL) {
        return false;
    }
    return ota_tdma_relay_feed(s_relay, offset, data, size);
}

void ota_tdma_binding_relay_abort(void)
{
    if (s_relay != NULL) {
        s_counters.relay_aborts++;
        ota_tdma_relay_abort(s_relay);
        s_relay = NULL;
        resource_arbiter_release_ota_admission();
    }
}

void ota_tdma_binding_relay_clear(void)
{
    if (s_relay != NULL && !ota_tdma_relay_active(s_relay)) {
        s_relay = NULL;
    }
}

bool ota_tdma_binding_relay_active(void)
{
    return s_relay != NULL && ota_tdma_relay_active(s_relay);
}

uint32_t ota_tdma_binding_relay_durable(void)
{
    return s_relay == NULL ? 0u : ota_tdma_relay_durable(s_relay);
}

bool ota_tdma_binding_relay_done(void)
{
    return s_relay != NULL && ota_tdma_relay_done(s_relay);
}

void ota_tdma_binding_get_counters(ota_tdma_binding_counters_t *counters)
{
    if (counters != NULL) {
        *counters = s_counters;
        counters->ignored_opens =
            s_ctx == NULL ? 0u
                          : ota_tdma_session_ignored_opens(&s_ctx->session);
    }
}

static bool ota_tdma_binding_parser(const uint8_t *payload, size_t payload_size,
                                    void *context)
{
    (void)context;
    return ota_tdma_binding_handle_maintenance_frame(payload, payload_size);
}

bool ota_tdma_binding_register_parser(void)
{
    return tdma_second_class_parser_register(OTA_TDMA_MAINTENANCE_STREAM_ID,
                                             ota_tdma_binding_parser, NULL);
}

bool ota_tdma_binding_handle_maintenance_frame(const uint8_t *payload,
                                               size_t size)
{
    s_entry[0] = (uint32_t)size;
    s_entry[1] = size > 0u ? (uint32_t)payload[0] : 0u;
    s_entry[2] = size > 1u ? (uint32_t)payload[1] : 0u;
    s_entry[3] = size > 2u ? (uint32_t)payload[2] : 0u;
    s_entry[4] = size > 3u ? (uint32_t)payload[3] : 0u;
    s_entry[5]++;
    drv_watchdog_mark_ota_phase(80u); /* maintenance frame received */
    if (payload == NULL || size == 0u) {
        return false;
    }
    if (!s_active && !ota_tdma_binding_init()) {
        return false; /* node out of the v1 single-peer domain */
    }
    drv_watchdog_mark_ota_phase(81u); /* binding active */
    if (s_relay != NULL && ota_tdma_relay_active(s_relay)) {
        s_stray_relay++;
        const bool ok = ota_tdma_session_on_frame(&s_relay->session,
                                                  board_uptime_ms(), payload,
                                                  size);
        ota_tdma_relay_update(s_relay);
        s_counters.relay_frames_rx++;
        if (!ok) {
            s_last_result =
                (uint32_t)ota_tdma_session_failure(&s_relay->session);
        }
        return ok;
    }
    const bool ok = ota_tdma_session_on_frame(&s_ctx->session,
                                              board_uptime_ms(), payload,
                                              size);
    drv_watchdog_mark_ota_phase(ok ? 82u : 83u); /* target frame result */
    if (!ok) {
        /* One composite code so later marks cannot hide earlier facts:
         * 1000 + received*100 + total*10 + failure. */
        drv_watchdog_reset_snapshot_t snapshot;
        drv_watchdog_get_reset_snapshot(&snapshot);
        uint32_t trace = snapshot.ota_phase;
        const uint32_t step = (trace >= 300u && trace <= 320u) ? trace - 300u
                                                                : 0u;
        {
            const uint8_t *w = s_ctx->session.reassembly.buffer;
            s_diag[6] = (uint32_t)w[0];
            s_diag[7] = (uint32_t)w[1];
            s_diag[8] = (uint32_t)w[2];
            s_diag[9] = (uint32_t)w[3];
            s_diag[10] = (uint32_t)w[20];
            s_diag[11] = (uint32_t)w[21];
            s_diag[12] = (uint32_t)w[36];
        }
        {
            uint32_t step_info[4] = {0};
            ota_tdma_session_step_diag(step_info);
            s_diag[13] = step_info[0]; /* seq */
            s_diag[14] = step_info[1]; /* payload_len */
            s_diag[15] = step_info[2] * 100000u + step_info[3];
        }
        {
            uint32_t open_diag[4] = {0};
            ota_tdma_session_open_diag(open_diag);
            s_diag[6] = open_diag[0]; /* decoded map_version */
            s_diag[7] = open_diag[1]; /* destination slot */
            s_diag[8] = open_diag[2]; /* total size */
            s_diag[9] = open_diag[3]; /* caps */
        }
        s_diag[10] = portable_ota_port_stream_platform_map_version();
        s_diag[1] = ((uint32_t)s_ctx->session.reassembly.buffer[12] << 24) |
                    ((uint32_t)s_ctx->session.reassembly.buffer[13] << 16) |
                    ((uint32_t)s_ctx->session.reassembly.buffer[14] << 8) |
                    (uint32_t)s_ctx->session.reassembly.buffer[15];
        s_diag[14] = (uint32_t)s_ctx->session.reassembly.length;
        s_diag[2] = portable_ota_port_crc32_compute(
                        s_ctx->session.reassembly.buffer,
                        OTA_TDMA_OPEN_WIRE_SIZE) % 1000000u;
        s_diag[3] = (uint32_t)s_ctx->session.reassembly.received;
        s_diag[4] = (uint32_t)s_ctx->session.reassembly.total;
        s_diag[5] = (uint32_t)ota_tdma_session_failure(&s_ctx->session);
        drv_watchdog_mark_ota_phase(100000u + s_last_trace * 1000u +
                                    s_diag[5]);
        (void)step;
    }
    if (s_ctx->session.target_open) {
        drv_watchdog_mark_ota_phase(84u); /* target stream opened */
    }
    s_counters.relay_frames_rx++;
    if (!ok) {
        s_last_result = (uint32_t)ota_tdma_session_failure(&s_ctx->session);
    } else if (ota_tdma_session_state(&s_ctx->session) !=
               OTA_TDMA_SESSION_FAILED) {
        s_last_result = 0u;
    }
    return ok;
}

uint32_t ota_tdma_binding_last_result(void)
{
    return s_last_result;
}
