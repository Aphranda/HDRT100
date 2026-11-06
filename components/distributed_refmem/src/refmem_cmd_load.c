#include "refmem_cmd_load.h"

#include <string.h>

#include "board_identity.h"
#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"
#include "osal.h"
#include "refmem_cmd_codec.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_fragment.h"
#include "tdma_long_frame.h"
#include "tdma_maintenance_runtime.h"
#include "tdma_maintenance_stream.h"
#include "tdma_second_class_parser.h"

static tdma_pio_spi_ring_adapter_t *s_adapter;
static volatile uint32_t s_initialized;
static volatile uint32_t s_submitted;
static volatile uint32_t s_tx_ok;
static volatile uint32_t s_tx_fail;
static volatile uint32_t s_rejected;
static volatile uint32_t s_rx_frames;
static volatile uint32_t s_decoded;
static volatile uint32_t s_posted;
static volatile uint32_t s_acked;
static uint8_t s_generation;
static uint8_t s_tx_frame[TDMA_MAINTENANCE_RUNTIME_MAX];
static refmem_cmd_fragment_ctx_t s_frag_ctx;
static uint32_t s_frag_frame_seq;
/* Retained fragment source for one bounded replay.
 *
 * The maintenance channel moves one 10-byte chunk per ring revolution, so a
 * 216-byte command frame is ~50 chunks and takes ~50 s at 10 kHz.  That is
 * throughput, not loss: the targets apply it together, one revolution after
 * the last chunk.  The only loss path left is a maintenance frame that fails
 * all three of its own attempts and is dropped; the fragments the target had
 * already accepted stay valid, so re-sending the SAME command_seq (new
 * fragment frame_seq) lets the target restart cleanly and dedupe the command
 * by its seq.  The source is kept here only until the replicated record shows
 * every required ACK bit; the replay is gated on a quiet transport so an old
 * and a new fragment sequence never interleave at the target. */
static uint8_t s_pending_frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                               REFMEM_SYNC_FRAME_PAYLOAD_MAX];
static uint32_t s_pending_size;
static uint32_t s_pending_count;
static uint32_t s_pending_seq;
static uint32_t s_pending_active;
static uint32_t s_pending_retries;
static uint8_t s_pending_target;
static uint32_t s_frag_complete;
static uint32_t s_frag_submit_fail;
static uint32_t s_frag_rejected;
static uint8_t s_maint_ack_stream_id;
static uint8_t s_maint_ack_generation;
static uint8_t s_maint_ack_ok;
static uint32_t s_maint_ack_remaining;
static uint32_t s_maint_ack_count;
/* Issuer-side fail-closed deadline. The replicated node-control record keeps no
 * issue tick, and posting the command into the local command slot changed the
 * record's lifecycle (the mirror then rewrote it empty), so the deadline lives
 * here: it belongs to the submitter, not to the command fact itself. */
static uint32_t s_cmd_deadline_ms;
static uint32_t s_cmd_pending_seq;
static uint32_t s_cmd_budget_ms;

static bool refmem_cmd_load_emit_fragments(const uint8_t *frame,
                                           uint32_t frame_size,
                                           uint8_t target_mask);

static void refmem_cmd_load_service_timeout(void);

static bool refmem_cmd_parser(const uint8_t *payload,
                              size_t payload_size,
                              void *context)
{
    (void)context;
    if (payload == NULL || payload_size != REFMEM_CMD_WIRE_SIZE) {
        return false;
    }
    refmem_command_request_t request;
    if (refmem_cmd_codec_decode(payload, &request) != REFMEM_CMD_CODEC_OK) {
        return false;
    }
    s_decoded++;
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    if ((request.target_mask & (1u << local_slot)) == 0u) {
        return true;
    }
    if (!distributed_refmem_command_try_post_replacing_complete(
            &request, osal_tick_ms())) {
        return true;
    }
    s_posted++;
    const refmem_command_take_result_t take =
        distributed_refmem_command_try_take(
            local_slot, request.issue_epoch, request.run_id,
            request.payload_crc32, 0u);
    if (take == REFMEM_COMMAND_TAKE_TAKEN) {
        (void)distributed_refmem_command_ack(local_slot, 0u);
        s_acked++;
        /* This transport (0x52 maintenance frame) posts, takes and acks inside
         * one call stack, so the local record never rests in POSTED and
         * distributed_refmem_node_control_service() -- the only other arm
         * site -- can never observe it.  Arm the wire reply here so the ACK
         * seat carries a VALID|COMMAND result back to the issuing node;
         * without it the issuer's reply aggregate stays POSTED even though
         * this target reports acked. */
        distributed_refmem_cmd_reply_arm(
            request.command_seq, request.source_node,
            (uint32_t)REFMEM_COMMAND_STATE_ACKED,
            (uint32_t)REFMEM_COMMAND_REASON_NONE);
    }
    return true;
}

static bool refmem_fragment_parser(const uint8_t *payload,
                                  size_t payload_size,
                                  void *context)
{
    (void)context;
    if (payload == NULL || payload_size == 0u ||
        payload_size % REFMEM_CMD_FRAGMENT_RECORD_SIZE != 0u) {
        s_frag_rejected++;
        return false;
    }
    bool ok = true;
    for (size_t offset = 0u;
         offset + REFMEM_CMD_FRAGMENT_RECORD_SIZE <= payload_size;
         offset += REFMEM_CMD_FRAGMENT_RECORD_SIZE) {
        uint32_t frame_seq = 0u;
        uint8_t index = 0u;
        uint8_t count = 0u;
        uint8_t source_slot = 0u;
        uint32_t word = 0u;
        if (!refmem_cmd_fragment_record_parse(
                payload + offset, REFMEM_CMD_FRAGMENT_RECORD_SIZE, &frame_seq,
                &index, &count, &source_slot, &word)) {
            s_frag_rejected++;
            ok = false;
            continue;
        }
        uint8_t frame[REFMEM_CMD_FRAGMENT_MAX_BYTES];
        size_t frame_size = 0u;
        if (refmem_cmd_fragment_accept(&s_frag_ctx, source_slot, frame_seq,
                                       count, index, word, frame,
                                       sizeof(frame), &frame_size)) {
            s_frag_complete++;
            if (!distributed_refmem_cmd_frame_receive(frame, frame_size)) {
                ok = false;
            }
        }
    }
    return ok;
}

bool refmem_cmd_load_init(tdma_pio_spi_ring_adapter_t *adapter)
{
    s_adapter = adapter;
    s_initialized = tdma_maintenance_runtime_ready() ? 1u : 0u;
    if (s_initialized == 0u) {
        return false;
    }
    tdma_second_class_parser_reset();
    if (!tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, refmem_cmd_parser, NULL) ||
        !tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_T2_STATIC, tdma_t2_static_parser, NULL) ||
        !tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, tdma_t2_dynamic_parser, NULL) ||
        !tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_REFMEM_FRAGMENT, refmem_fragment_parser,
            NULL)) {
        s_initialized = 0u;
        return false;
    }
    return true;
}

bool refmem_cmd_load_submit(const uint8_t *wire, uint32_t wire_size)
{
    return refmem_cmd_load_submit_type(TDMA_SECOND_CLASS_TYPE_REFMEM_CMD,
                                       wire, wire_size);
}

bool refmem_cmd_load_submit_type(uint8_t type,
                                 const uint8_t *wire,
                                 uint32_t wire_size)
{
    if (wire == NULL || wire_size == 0u ||
        wire_size > TDMA_MAINTENANCE_RUNTIME_MAX) {
        s_rejected++;
        return false;
    }
    /* The bootstrap carrier is a separately owned transport (CONSOLE-017/035).
     * It is used only while the ring link is not ready; the ring keeps
     * priority.  Without it the module keeps its original init gate. */
    const bool bootstrap =
        refmem_control_transport_route(
            distributed_refmem_ring_link_ready() ? 1u : 0u) ==
        REFMEM_CONTROL_TRANSPORT_BOOTSTRAP_SPI;
    if (!bootstrap && s_initialized == 0u) {
        s_rejected++;
        return false;
    }
    const uint8_t generation = s_generation++;
    memcpy(s_tx_frame, wire, wire_size);
    uint8_t target_mask = 0x0Fu;
    if (type == TDMA_SECOND_CLASS_TYPE_REFMEM_CMD &&
        wire_size == REFMEM_CMD_WIRE_SIZE) {
        refmem_command_request_t request;
        if (refmem_cmd_codec_decode(wire, &request) == REFMEM_CMD_CODEC_OK) {
            target_mask = (uint8_t)request.target_mask;
        }
    }
    const bool submitted =
        bootstrap
            ? refmem_control_transport_submit(s_tx_frame, wire_size, type,
                                              generation, target_mask)
            : tdma_maintenance_runtime_submit(s_tx_frame, wire_size, type,
                                              generation, target_mask);
    if (!submitted) {
        s_rejected++;
        s_tx_fail++;
        return false;
    }
    s_submitted++;
    s_tx_ok++;
    if (type == TDMA_SECOND_CLASS_TYPE_REFMEM_CMD) {
        /* C0: the issuer also records the command in its own REFMEM node
         * control slot, so the source is observable through the same
         * snapshot as the targets instead of only through TX counters. */
        refmem_command_request_t request;
        if (wire_size == REFMEM_CMD_WIRE_SIZE &&
            refmem_cmd_codec_decode(wire, &request) == REFMEM_CMD_CODEC_OK) {
            const uint32_t board_no = board_identity_get_no();
            const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
            refmem_command_snapshot_t snapshot;
            memset(&snapshot, 0, sizeof(snapshot));
            snapshot.state = REFMEM_COMMAND_STATE_POSTED;
            snapshot.command_seq = request.command_seq;
            snapshot.source_node = local_slot;
            snapshot.source_instance = request.source_instance;
            snapshot.target_mask = request.target_mask;
            snapshot.required_mask = request.required_mask;
            snapshot.command_type = request.command_type;
            snapshot.command_class = request.command_class;
            snapshot.payload_kind = request.payload_kind;
            snapshot.payload_ref = request.payload_ref;
            snapshot.payload_size = request.payload_size;
            snapshot.payload_crc32 = request.payload_crc32;
            snapshot.issue_epoch = request.issue_epoch;
            snapshot.run_id = request.run_id;
            snapshot.timeout_us = request.timeout_us;
            (void)distributed_refmem_node_control_write(
                local_slot, &snapshot, request.command_seq);
            /* Arm the issuer-side deadline. timeout_us is microseconds; the
             * service compares whole milliseconds, so round the deadline up
             * to the next millisecond (a 0 timeout means "no deadline"). */
            if (request.timeout_us != 0u && request.required_mask != 0u) {
                const uint32_t timeout_ms =
                    (request.timeout_us + 999u) / 1000u;
                s_cmd_pending_seq = request.command_seq;
                s_cmd_deadline_ms = osal_tick_ms() + timeout_ms;
            }
        }
    }
    return true;
}

/* Fail closed on the issuer: once the command's deadline passes and the
 * replicated record still lacks required ACK/NACK bits, advance that record to
 * TIMED_OUT. Only the record is touched -- no command slot, no mirror -- so the
 * request/status writer split (issuer writes the request, the owner writes
 * status) stays exactly as it was. */
static void refmem_cmd_load_service_timeout(void)
{
    if (s_cmd_pending_seq == 0u) {
        return;
    }
    if ((int32_t)(osal_tick_ms() - s_cmd_deadline_ms) < 0) {
        return;
    }
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    refmem_command_snapshot_t snapshot;
    if (!distributed_refmem_node_control_read(local_slot, &snapshot, NULL, NULL)) {
        s_cmd_pending_seq = 0u;
        return;
    }
    if (snapshot.command_seq != s_cmd_pending_seq) {
        s_cmd_pending_seq = 0u; /* superseded or cleared by another owner */
        return;
    }
    const uint32_t pending =
        snapshot.required_mask &
        ~(snapshot.ack_flags | snapshot.nack_flags | snapshot.timeout_flags);
    if (pending == 0u) {
        s_cmd_pending_seq = 0u; /* every required bit answered in time */
        s_pending_active = 0u;
        s_pending_retries = 0u;
        return;
    }
    /* One bounded replay before failing closed.  A maintenance frame that used
     * all three of its own attempts is dropped without telling the fragment
     * layer, so the only recovery is to send the same command again.  The
     * replay must wait for a quiet transport: an old and a new fragment
     * sequence interleaved at the target would reset its reassembler on every
     * switch and never complete. */
    if (s_pending_retries != 0u && s_pending_active != 0u &&
        s_pending_seq == s_cmd_pending_seq) {
        tdma_maintenance_runtime_diag_t diag;
        tdma_maintenance_runtime_get_diag(&diag);
        if (diag.queue_depth != 0u || diag.tx_awaiting_ack != 0u) {
            /* Still draining: re-check shortly instead of replaying now. */
            s_cmd_deadline_ms = osal_tick_ms() + 1000u;
            return;
        }
        s_pending_retries--;
        if (refmem_cmd_load_emit_fragments(s_pending_frame, s_pending_size,
                                           s_pending_target)) {
            s_tx_ok++;
            s_cmd_deadline_ms = osal_tick_ms() + s_cmd_budget_ms;
            return;
        }
        s_frag_submit_fail++;
    }
    (void)distributed_refmem_node_control_update_status(
        local_slot, (uint32_t)REFMEM_COMMAND_STATE_TIMED_OUT,
        snapshot.ack_flags, snapshot.nack_flags, 0u,
        snapshot.timeout_flags | pending,
        (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT,
        (uint32_t)REFMEM_COMMAND_INVALID_NODE, 0u, 0u);
    s_cmd_pending_seq = 0u;
    s_pending_active = 0u;
    s_pending_retries = 0u;
}

void refmem_cmd_load_service(void)
{
    if (s_initialized == 0u) {
        return;
    }
    refmem_cmd_load_service_timeout();
    uint8_t frame[TDMA_MAINTENANCE_RUNTIME_MAX];
    size_t frame_size = 0u;
    uint8_t stream_id = 0u;
    uint8_t generation = 0u;
    while (tdma_maintenance_runtime_take(frame, sizeof(frame), &frame_size,
                                         &stream_id, &generation)) {
        s_rx_frames++;
        const bool accepted =
            tdma_second_class_parser_dispatch(stream_id, frame, frame_size);
        s_maint_ack_stream_id = stream_id;
        s_maint_ack_generation = generation;
        s_maint_ack_ok = accepted ? 1u : 0u;
        s_maint_ack_remaining = 4u;
        s_maint_ack_count++;
    }
}

bool refmem_cmd_load_peek_maintenance_ack(uint8_t *stream_id,
                                          uint8_t *generation,
                                          bool *ok)
{
    if (s_maint_ack_remaining == 0u) {
        return false;
    }
    if (stream_id != NULL) {
        *stream_id = s_maint_ack_stream_id;
    }
    if (generation != NULL) {
        *generation = s_maint_ack_generation;
    }
    if (ok != NULL) {
        *ok = s_maint_ack_ok != 0u;
    }
    return true;
}

void refmem_cmd_load_consume_maintenance_ack(void)
{
    if (s_maint_ack_remaining != 0u) {
        s_maint_ack_remaining--;
    }
}

bool refmem_cmd_load_get_status(refmem_cmd_load_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->load_initialized = s_initialized;
    status->submitted = s_submitted;
    status->served = s_tx_ok;
    status->rejected = s_rejected;
    status->rx_events = s_rx_frames;
    status->decoded = s_decoded;
    status->posted = s_posted;
    status->acked = s_acked;
    status->tx_ok = s_tx_ok;
    status->tx_fail = s_tx_fail;
    /* No standalone load frame is built on the maintenance-stream path. */
    status->tx_stage = 0u;
    status->build_class = 0u;
    status->build_size = 0u;
    status->build_load = 0u;
    uint32_t mailbox_calls = 0u;
    uint32_t chunks = 0u;
    uint32_t frames = 0u;
    uint32_t rejects = 0u;
    tdma_maintenance_runtime_get_counters(&mailbox_calls, &chunks, &frames,
                                          &rejects);
    status->rx_seen = chunks;
    status->rx_delivered = frames;
    status->parser_registered = tdma_second_class_parser_registered_count();
    status->parser_unknown = tdma_second_class_parser_unknown_count();
    tdma_second_class_parser_count_t count;
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, &count)) {
        status->refmem_parser_ok = count.accepted;
        status->refmem_parser_reject = count.rejected;
    }
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_T2_STATIC, &count)) {
        status->t2_static_ok = count.accepted;
        status->t2_static_reject = count.rejected;
    }
    status->t2_static_applied = tdma_t2_static_applied_count();
    uint32_t t2_target = 0u;
    uint32_t t2_config = 0u;
    uint32_t t2_value = 0u;
    (void)tdma_t2_static_get_last(&t2_target, &t2_config, &t2_value);
    status->t2_static_last_config = t2_config;
    status->t2_static_last_value = t2_value;
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, &count)) {
        status->t2_dynamic_ok = count.accepted;
        status->t2_dynamic_reject = count.rejected;
    }
    status->t2_dynamic_applied = tdma_t2_dynamic_applied_count();
    uint32_t dyn_target = 0u;
    uint32_t dyn_run = 0u;
    uint32_t dyn_config = 0u;
    uint32_t dyn_value = 0u;
    (void)tdma_t2_dynamic_get_last(&dyn_target, &dyn_run, &dyn_config,
                                   &dyn_value);
    status->t2_dynamic_last_target = dyn_target;
    status->t2_dynamic_last_run = dyn_run;
    status->t2_dynamic_last_config = dyn_config;
    status->t2_dynamic_last_value = dyn_value;
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_REFMEM_FRAGMENT, &count)) {
        status->frag_ok = count.accepted;
        status->frag_reject = count.rejected + s_frag_rejected;
    }
    status->frag_complete = s_frag_complete;
    status->frag_submit_fail = s_frag_submit_fail;
    status->frag_pending = s_pending_active;
    /* Replay state rather than send position: `index` is the number of bounded
     * replays still allowed and `count` the fragments in the stored frame. */
    status->frag_pending_index = s_pending_retries;
    status->frag_pending_count = s_pending_count;
    if (s_adapter != NULL) {
        tdma_pio_spi_ring_adapter_get_rx_debug(
            s_adapter, &status->rx_drop_count,
            &status->last_rx_packet_size, &status->last_rx_payload_class,
            &status->rx_config_control, &status->rx_process_image);
        tdma_pio_spi_ring_adapter_get_maintenance_inject(
            s_adapter, &status->inject_attempts, &status->inject_ok);
        tdma_maintenance_adapter_diag_t adapter_diag;
        tdma_pio_spi_ring_adapter_get_maintenance_diag(s_adapter,
                                                       &adapter_diag);
        status->inject_launched = adapter_diag.inject_launched;
        status->inject_dropped_unlaunched =
            adapter_diag.inject_dropped_unlaunched;
        status->inject_new_sequence = adapter_diag.inject_new_sequence;
        status->inject_repeated_sequence =
            adapter_diag.inject_repeated_sequence;
        status->inject_last_sequence = adapter_diag.inject_last_sequence;
        status->beacon_frames = adapter_diag.beacon_frames;
        status->rx_polls = adapter_diag.rx_polls;
        status->rx_seen_chunks = adapter_diag.rx_seen_chunks;
        status->rx_accepted_chunks = adapter_diag.rx_accepted_chunks;
        status->rx_dedup_sequence = adapter_diag.rx_dedup_sequence;
        status->rx_bad_mailbox = adapter_diag.rx_bad_mailbox;
        status->rx_last_sequence = adapter_diag.rx_last_sequence;
        status->rx_sequence_cursor = adapter_diag.rx_sequence_cursor;
    }
    tdma_maintenance_runtime_diag_t runtime_diag;
    tdma_maintenance_runtime_get_diag(&runtime_diag);
    status->queue_depth = runtime_diag.queue_depth;
    status->queue_enqueued = runtime_diag.enqueued;
    status->queue_dequeued = runtime_diag.dequeued;
    status->queue_full = runtime_diag.queue_full;
    status->rx_chunk_rejects = runtime_diag.rx_chunk_rejects;
    status->rx_restarts = runtime_diag.rx_restarts;
    status->rx_duplicate_chunks = runtime_diag.rx_duplicate_chunks;
    status->tx_awaiting_ack = runtime_diag.tx_awaiting_ack;
    status->tx_ack_ok = runtime_diag.tx_ack_ok;
    status->tx_retries = runtime_diag.tx_retries;
    status->tx_ack_fail = runtime_diag.tx_fail;
    status->tx_dropped_chunks = runtime_diag.tx_dropped_chunks;
    return true;
}

/* Emit every 4-byte fragment of one REFMEM frame as 9-byte records packed into
 * maintenance frames of at most TDMA_MAINTENANCE_STREAM_MAX bytes.  Each call
 * starts a new fragment sequence so a replay restarts the target's reassembler
 * instead of being rejected as an out-of-order continuation. */
static bool refmem_cmd_load_emit_fragments(const uint8_t *frame,
                                           uint32_t frame_size,
                                           uint8_t target_mask)
{
    if (frame == NULL || frame_size == 0u) {
        return false;
    }
    const bool bootstrap =
        refmem_control_transport_route(
            distributed_refmem_ring_link_ready() ? 1u : 0u) ==
        REFMEM_CONTROL_TRANSPORT_BOOTSTRAP_SPI;
    if (!bootstrap && s_initialized == 0u) {
        return false;
    }
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    const uint32_t count = refmem_cmd_fragment_count(frame_size);
    if (count == 0u || count > UINT8_MAX) {
        return false;
    }
    s_frag_frame_seq++;
    uint8_t payload[TDMA_MAINTENANCE_STREAM_MAX];
    uint32_t used = 0u;
    uint32_t submitted_frames = 0u;
    for (uint32_t index = 0u; index < count; index++) {
        uint32_t word = 0u;
        if (!refmem_cmd_fragment_get(frame, frame_size, index, &word)) {
            s_frag_submit_fail++;
            return false;
        }
        uint8_t record[REFMEM_CMD_FRAGMENT_RECORD_SIZE];
        (void)refmem_cmd_fragment_record_build(
            s_frag_frame_seq, (uint8_t)index, (uint8_t)count,
            (uint8_t)local_slot, word, record);
        memcpy(payload + used, record, REFMEM_CMD_FRAGMENT_RECORD_SIZE);
        used += REFMEM_CMD_FRAGMENT_RECORD_SIZE;
        const bool last = (index + 1u) == count;
        if (used + REFMEM_CMD_FRAGMENT_RECORD_SIZE >
                TDMA_MAINTENANCE_STREAM_MAX ||
            last) {
            const bool submitted =
                bootstrap
                    ? refmem_control_transport_submit(
                          payload, used, TDMA_SECOND_CLASS_TYPE_REFMEM_FRAGMENT,
                          s_generation++, target_mask)
                    : tdma_maintenance_runtime_submit(
                          payload, used, TDMA_SECOND_CLASS_TYPE_REFMEM_FRAGMENT,
                          s_generation++, target_mask);
            if (!submitted) {
                s_frag_submit_fail++;
                return false;
            }
            submitted_frames++;
            used = 0u;
        }
    }
    return submitted_frames > 0u;
}

bool refmem_cmd_load_submit_fragments(
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *params)
{
    if (snapshot == NULL) {
        s_rejected++;
        return false;
    }
    /* Same transport rule as the single-frame path: the bootstrap carrier is
     * usable only while the ring link is down, and only after its owner
     * registered it.  Ring keeps priority. */
    const bool bootstrap =
        refmem_control_transport_route(
            distributed_refmem_ring_link_ready() ? 1u : 0u) ==
        REFMEM_CONTROL_TRANSPORT_BOOTSTRAP_SPI;
    if (!bootstrap && s_initialized == 0u) {
        s_rejected++;
        return false;
    }
    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                  REFMEM_SYNC_FRAME_PAYLOAD_MAX];
    size_t frame_size = 0u;
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    if (!refmem_cmd_frame_build((uint8_t)local_slot,
                                (uint8_t)snapshot->target_mask,
                                snapshot->issue_epoch, snapshot->run_id,
                                snapshot->command_seq, snapshot, params, frame,
                                sizeof(frame), &frame_size)) {
        s_rejected++;
        return false;
    }
    const uint32_t count = refmem_cmd_fragment_count(frame_size);
    if (count == 0u || frame_size > sizeof(s_pending_frame)) {
        s_rejected++;
        return false;
    }
    if (!refmem_cmd_load_emit_fragments(frame, (uint32_t)frame_size,
                                        (uint8_t)snapshot->target_mask)) {
        s_pending_active = 0u;
        return false;
    }
    memcpy(s_pending_frame, frame, frame_size);
    s_pending_size = (uint32_t)frame_size;
    s_pending_count = count;
    s_pending_seq = snapshot->command_seq;
    s_pending_target = (uint8_t)snapshot->target_mask;
    s_pending_active = 1u;
    s_pending_retries = 1u;
    s_submitted++;
    s_tx_ok++;
    /* The issuer records the request in its own REFMEM node control slot, so
     * the reply aggregation has one pending command fact and observers see
     * the request even before any target answers. */
    {
        refmem_command_snapshot_t source = *snapshot;
        source.source_node = (uint32_t)local_slot;
        source.state = REFMEM_COMMAND_STATE_POSTED;
        source.ack_flags = 0u;
        source.nack_flags = 0u;
        source.busy_flags = 0u;
        source.timeout_flags = 0u;
        source.last_reason = 0u;
        source.last_reason_slot = 0u;
        source.last_completed_seq = 0u;
        (void)distributed_refmem_node_control_write_with_params(
            local_slot, &source, params, source.command_seq);
    }
    /* Arm the issuer-side deadline on the fragment path too.  Catalog 50/51
     * carry timeout_us = 0, so the budget comes from the ring: at 10 kHz the
     * measured transfer is ~50 s, and this budget is ~57 s; at 10 MHz it is
     * sub-second.  A zero budget means the ring never reported a feedback
     * timeout, and then no deadline is armed (same rule as the single-frame
     * path). */
    if (snapshot->required_mask != 0u) {
        const uint32_t frames = (count + 7u) / 8u;
        const uint32_t bytes = count * REFMEM_CMD_FRAGMENT_RECORD_SIZE;
        uint32_t budget_ms =
            tdma_maintenance_runtime_delivery_budget_ms(bytes, frames);
        const uint32_t explicit_ms = snapshot->timeout_us / 1000u +
                                     ((snapshot->timeout_us % 1000u) != 0u);
        if (explicit_ms > budget_ms) {
            budget_ms = explicit_ms;
        }
        if (budget_ms != 0u) {
            s_cmd_pending_seq = snapshot->command_seq;
            s_cmd_budget_ms = budget_ms;
            s_cmd_deadline_ms = osal_tick_ms() + budget_ms;
        }
    }
    return true;
}

/* The replay path reuses the stored frame above; the per-fragment pacing queue
 * that used to live here was never armed (every write cleared it), so a
 * one-record-per-call path could not have run.  Deleted rather than enabled:
 * one 9-byte record per maintenance frame is 8x slower than the packed form
 * and cannot keep up at a low-rate profile. */

