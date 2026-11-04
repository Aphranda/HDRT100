#ifndef OTA_TDMA_V2_BRIDGE_APP_H
#define OTA_TDMA_V2_BRIDGE_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Architecture v2 target wiring: TDMA frames -> OTA-TDMA bridge -> the existing
 * OTA AO event pipeline (ota_ao/ota_fb -> portable_ota_port_core_*).
 *
 * The feature is compiled unconditionally (so its types stay checked) and is
 * selected at runtime by PROJECT_OTA_TDMA_V2_BRIDGE.  The stream-session path
 * remains available as the transition fallback. */

/* Opt-in until V2-C (N2 single-hop) closes: the default firmware keeps the
 * proven stream path, and bench validation builds enable the bridge with
 * -DPROJECT_OTA_TDMA_V2_BRIDGE=1. */
#ifndef PROJECT_OTA_TDMA_V2_BRIDGE
#define PROJECT_OTA_TDMA_V2_BRIDGE 0
#endif

typedef bool (*ota_tdma_v2_send_frame_fn)(const uint8_t *frame, size_t length);

bool ota_tdma_v2_bridge_app_init(ota_tdma_v2_send_frame_fn send);

/* Feeds one maintenance frame received on the target. */
bool ota_tdma_v2_bridge_app_frame(const uint8_t *payload, size_t size);

/* OTA AO data-done hook: releases the staged block and acknowledges the pump. */
void ota_tdma_v2_bridge_app_release(void);

/* True while the bridge holds a session (OPen accepted, not yet ended/aborted).
 * The binding uses it to hold the OTA resource admission for the whole session,
 * because the v2 path opens no stream session that would declare it. */
bool ota_tdma_v2_bridge_app_active(void);

/* Periodic failure watch: when the AB pipeline has failed while the bridge is
 * still receiving, sends one ABORT to the source and drops the bridge session.
 * Returns true when an ABORT was queued. */
bool ota_tdma_v2_bridge_app_service(void);

/* Diagnostics: {stage_offset, stage_length, received_size, block_in_flight,
 * last_frame_type, last_frame_offset, last_frame_length,
 * last_vector_state, last_ready}. */
void ota_tdma_v2_bridge_app_stats(uint32_t out[9]);

#endif
