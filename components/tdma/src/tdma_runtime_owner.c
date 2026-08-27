#include "tdma_runtime_owner.h"

#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "vdc_timestamp_clock.h"
#include "resource_arbiter.h"

#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
#include "FreeRTOS.h"
#endif

static tdma_service_service_t s_tdma_runtime_owner;
static tdma_traffic_scheduler_t s_tdma_traffic_scheduler;
static tdma_pio_spi_ring_adapter_t s_tdma_pio_spi_ring_adapter;
static tdma_pio_spi_phys_t s_tdma_pio_spi_phys;
static tdma_operating_profile_manager_t s_tdma_operating_profile_manager;
typedef enum {
    TDMA_CAL_LOOPBACK_INTENT_NONE = 0u,
    TDMA_CAL_LOOPBACK_INTENT_START = 1u,
    TDMA_CAL_LOOPBACK_INTENT_STOP = 2u,
} tdma_cal_loopback_intent_opcode_t;

/* Core0 is the single producer and core1 is the single consumer.  The
 * calibration domain may publish only this guarded intent; PIO/SM/DMA state
 * remains private to the TDMA owner and is changed from core1 service only. */
typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    uint32_t sample_hz;
    uint32_t sample_words;
    uint32_t epoch;
} tdma_cal_loopback_intent_t;

static tdma_cal_loopback_intent_t s_tdma_cal_loopback_intent;
static volatile uint32_t s_tdma_cal_loopback_consumed_sequence;
static uint32_t s_tdma_cal_loopback_next_sequence;
#if !defined(PROJECT_USE_FREERTOS) || !PROJECT_USE_FREERTOS
static tdma_traffic_scheduler_slot_t
    s_tdma_traffic_slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
#endif
static bool s_tdma_runtime_owner_initialized;
static uint32_t s_tdma_topology_probe_phase_delay_cycles;

static bool tdma_runtime_owner_flight_phys_arm(
    void *context,
    const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    const tdma_ring_calibration_stage_t *stage =
        &s_tdma_runtime_owner.calibration_stage;
    if (phys == NULL || config == NULL ||
        config->local_slot_id >= config->node_count) {
        return false;
    }
    if (stage->enabled == 0u) {
        const uint32_t phase = s_tdma_topology_probe_phase_delay_cycles;
        return phase != 0u && phase <= 31u &&
               tdma_pio_spi_phys_set_flight_offsets(
                   phys, 0, 0, 0, phase, phase, phase) &&
               tdma_pio_spi_phys_arm(context, config);
    }
    if (stage->node_count != config->node_count ||
        config->local_slot_id >= stage->node_count) {
        return false;
    }
    /* Calibration step 1 freezes the directed link endpoints.  Search those
     * endpoints; never infer physical wiring from the numeric Node value. */
    const uint32_t node = config->local_slot_id;
    uint32_t marker_link = stage->node_count;
    uint32_t data_link = stage->node_count;
    for (uint32_t link = 0u; link < stage->node_count; link++) {
        if (stage->links[link].marker_destination_node == node) {
            marker_link = link;
        }
        if (stage->links[link].data_destination_node == node) {
            data_link = link;
        }
    }
    if (marker_link >= stage->node_count || data_link >= stage->node_count) {
        return false;
    }
    const tdma_ring_calibration_link_t *marker =
        &stage->links[marker_link];
    const tdma_ring_calibration_link_t *data = &stage->links[data_link];
    if (marker->valid == 0u || data->valid == 0u ||
        !tdma_pio_spi_phys_set_flight_offsets(
            phys,
            marker->marker_offset_sample_count,
            marker->sck_offset_sample_count,
            data->data_offset_sample_count,
            marker->marker_phase_delay_cycles,
            marker->sck_phase_delay_cycles,
            data->data_phase_delay_cycles)) {
        return false;
    }
    return tdma_pio_spi_phys_arm(context, config);
}

static void tdma_runtime_owner_cal_intent_write_begin(void)
{
    (void)__atomic_add_fetch(&s_tdma_cal_loopback_intent.guard,
                             1u, __ATOMIC_ACQ_REL);
}

static void tdma_runtime_owner_cal_intent_write_end(void)
{
    (void)__atomic_add_fetch(&s_tdma_cal_loopback_intent.guard,
                             1u, __ATOMIC_RELEASE);
}

static void tdma_runtime_owner_cal_intent_publish(
    tdma_cal_loopback_intent_opcode_t opcode,
    uint32_t sample_hz,
    uint32_t sample_words,
    uint32_t epoch)
{
    tdma_runtime_owner_cal_intent_write_begin();
    s_tdma_cal_loopback_intent.sequence = ++s_tdma_cal_loopback_next_sequence;
    s_tdma_cal_loopback_intent.opcode = (uint32_t)opcode;
    s_tdma_cal_loopback_intent.sample_hz = sample_hz;
    s_tdma_cal_loopback_intent.sample_words = sample_words;
    s_tdma_cal_loopback_intent.epoch = epoch;
    tdma_runtime_owner_cal_intent_write_end();
}

static bool tdma_runtime_owner_cal_intent_read(
    tdma_cal_loopback_intent_t *intent)
{
    if (intent == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_tdma_cal_loopback_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *intent = s_tdma_cal_loopback_intent;
        const uint32_t end = __atomic_load_n(
            &s_tdma_cal_loopback_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

bool tdma_runtime_owner_init(void)
{
    if (s_tdma_runtime_owner_initialized) {
        return true;
    }
    s_tdma_pio_spi_phys.coded.version =
        TDMA_PIO_SPI_CODED_SNAPSHOT_VERSION;
    s_tdma_pio_spi_phys.coded.state = TDMA_PIO_SPI_CODED_IDLE;
    s_tdma_pio_spi_phys.marker.version =
        TDMA_PIO_SPI_MARKER_SNAPSHOT_VERSION;
    s_tdma_pio_spi_phys.marker.state = TDMA_PIO_SPI_MARKER_IDLE;
    s_tdma_pio_spi_phys.marker.flags =
        TDMA_PIO_SPI_MARKER_FLAG_DIAGNOSTIC_ONLY;
    s_tdma_pio_spi_phys.data_train.version =
        TDMA_PIO_SPI_DATA_TRAIN_SNAPSHOT_VERSION;
    s_tdma_pio_spi_phys.data_train.state = TDMA_PIO_SPI_DATA_TRAIN_IDLE;
    s_tdma_pio_spi_phys.data_train.flags =
        TDMA_PIO_SPI_DATA_TRAIN_FLAG_DIAGNOSTIC_ONLY;
    tdma_traffic_scheduler_slot_t *slots = NULL;
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
    slots = pvPortMalloc(sizeof(tdma_traffic_scheduler_slot_t) *
                         TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT);
#else
    slots = s_tdma_traffic_slots;
#endif
    bool initialized = false;
    tdma_operating_profile_t boot_profile;
    if (slots != NULL &&
        tdma_operating_profile_find_baud(PROJECT_TDMA_SPI_BAUD_HZ,
                                         &boot_profile) &&
        tdma_operating_profile_manager_init(
            &s_tdma_operating_profile_manager, boot_profile.level) &&
        tdma_traffic_scheduler_init(&s_tdma_traffic_scheduler,
                                    slots,
                                    TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT) &&
        tdma_service_init(&s_tdma_runtime_owner) &&
        tdma_service_set_operating_profile(
            &s_tdma_runtime_owner,
            &s_tdma_operating_profile_manager.active) &&
        tdma_service_bind_traffic_scheduler(&s_tdma_runtime_owner,
                                            &s_tdma_traffic_scheduler) &&
        tdma_pio_spi_ring_adapter_init(&s_tdma_pio_spi_ring_adapter) &&
        tdma_pio_spi_ring_adapter_set_forwarding_mode(
            &s_tdma_pio_spi_ring_adapter,
            TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT) &&
        tdma_service_register_adapter_impl(
            &s_tdma_runtime_owner,
            TDMA_ADAPTER_PIO_SPI,
            tdma_pio_spi_ring_adapter_ops(),
            &s_tdma_pio_spi_ring_adapter)) {
        tdma_pio_spi_ring_adapter_set_phys_ctrl(
            &s_tdma_pio_spi_ring_adapter,
            tdma_runtime_owner_flight_phys_arm,
            tdma_pio_spi_phys_disarm,
            tdma_pio_spi_phys_train_clock,
            tdma_pio_spi_phys_train_clock_service,
            &s_tdma_pio_spi_phys);
        tdma_pio_spi_ring_adapter_set_phys(
            &s_tdma_pio_spi_ring_adapter,
            tdma_pio_spi_phys_tx,
            tdma_pio_spi_phys_rx,
            &s_tdma_pio_spi_phys);
        tdma_pio_spi_ring_adapter_set_phys_feedback(
            &s_tdma_pio_spi_ring_adapter,
            tdma_pio_spi_phys_feedback_round_trip);
        tdma_pio_spi_ring_adapter_set_phys_overlay(
            &s_tdma_pio_spi_ring_adapter,
            tdma_pio_spi_phys_prepare_process_overlay,
            tdma_pio_spi_phys_service_process_overlay_boundary);
        tdma_pio_spi_ring_adapter_set_flight_fifo(
            &s_tdma_pio_spi_ring_adapter,
            &s_tdma_runtime_owner.flight_fifo);
        tdma_pio_spi_ring_adapter_set_flight_engine(
            &s_tdma_pio_spi_ring_adapter,
            &s_tdma_runtime_owner.flight_engine);
        (void)vdc_timestamp_clock_init();
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &s_tdma_pio_spi_ring_adapter,
            vdc_timestamp_clock_resolution_ns() * 2u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        initialized = true;
    }
    if (!initialized) {
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
        if (slots != NULL) {
            vPortFree(slots);
        }
#endif
        return false;
    }
    s_tdma_runtime_owner_initialized = true;
    return true;
}

tdma_service_service_t *tdma_runtime_owner_get(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_runtime_owner : NULL;
}

tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_traffic_scheduler : NULL;
}

tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_pio_spi_ring_adapter : NULL;
}

bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    return tdma_ring_runtime_get_snapshot(&s_tdma_runtime_owner.ring_runtime,
                                          snapshot);
}

bool tdma_runtime_owner_get_phys_snapshot(tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    return tdma_pio_spi_phys_get_snapshot(&s_tdma_pio_spi_phys, snapshot);
}

bool tdma_runtime_owner_set_flight_process_image_mode(bool enabled)
{
    if (!s_tdma_runtime_owner_initialized) {
        return false;
    }
    tdma_ring_runtime_snapshot_t ring;
    tdma_flight_engine_snapshot_t engine;
    if (!tdma_ring_runtime_get_snapshot(&s_tdma_runtime_owner.ring_runtime,
                                        &ring) ||
        !tdma_flight_engine_get_snapshot(&s_tdma_runtime_owner.flight_engine,
                                         &engine) ||
        ring.enabled != 0u || ring.adapter_started != 0u) {
        return false;
    }
    const uint32_t payload_size =
        engine.configured != 0u && engine.payload_size != 0u
            ? engine.payload_size
            : TDMA_FLIGHT_SHORT_PAYLOAD_SIZE;
    const tdma_pio_spi_ring_forwarding_mode_t mode = enabled
        ? TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE
        : TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT;
    if (!tdma_pio_spi_phys_set_process_image_mode(
            &s_tdma_pio_spi_phys, enabled, payload_size)) {
        return false;
    }
    if (!tdma_pio_spi_ring_adapter_set_forwarding_mode(
            &s_tdma_pio_spi_ring_adapter, mode)) {
        (void)tdma_pio_spi_phys_set_process_image_mode(
            &s_tdma_pio_spi_phys, false, 0u);
        return false;
    }
    return true;
}

bool tdma_runtime_owner_copy_normal_capture_core1(
    uint32_t *rx_bytes,
    size_t rx_capacity,
    uint32_t *tx_bytes,
    size_t tx_capacity,
    tdma_pio_spi_normal_capture_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_copy_normal_capture(
               &s_tdma_pio_spi_phys, rx_bytes, rx_capacity,
               tx_bytes, tx_capacity, snapshot);
}

bool tdma_runtime_owner_get_clk_train_snapshot(
    tdma_pio_spi_clk_train_snapshot_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    return tdma_pio_spi_phys_get_clk_train_snapshot(&s_tdma_pio_spi_phys,
                                                     snapshot);
}

bool tdma_runtime_owner_train_clock(uint32_t cycles)
{
    if (!s_tdma_runtime_owner_initialized || cycles == 0u ||
        !tdma_service_ring_train_clock(&s_tdma_runtime_owner, cycles)) {
        return false;
    }
    /* Publish before core1 consumes the command to close the admission race. */
    resource_arbiter_publish_tdma_clock_training(true);
    return true;
}

void tdma_runtime_owner_update_training_gate(void)
{
    tdma_pio_spi_clk_train_snapshot_t snapshot;
    const bool active =
        tdma_runtime_owner_get_clk_train_snapshot(&snapshot) &&
        (snapshot.state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING ||
         snapshot.state == TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING);
    resource_arbiter_publish_tdma_clock_training(active);
}

bool tdma_runtime_owner_get_operating_profile(
    tdma_operating_profile_manager_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    *snapshot = s_tdma_operating_profile_manager;
    return true;
}

bool tdma_runtime_owner_stage_operating_profile(uint32_t level)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_operating_profile_manager_stage(
               &s_tdma_operating_profile_manager, level);
}

bool tdma_runtime_owner_apply_operating_profile(void)
{
    tdma_ring_runtime_snapshot_t ring;
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_ring_runtime_get_snapshot(&s_tdma_runtime_owner.ring_runtime,
                                        &ring)) {
        return false;
    }
    if (ring.enabled != 0u) {
        return tdma_operating_profile_manager_apply(
            &s_tdma_operating_profile_manager, false);
    }
    if (!tdma_service_set_operating_profile(
            &s_tdma_runtime_owner,
            &s_tdma_operating_profile_manager.staged)) {
        s_tdma_operating_profile_manager.reject_count++;
        s_tdma_operating_profile_manager.last_result =
            TDMA_OPERATING_PROFILE_BAD_ARGUMENT;
        return false;
    }
    return tdma_operating_profile_manager_apply(
        &s_tdma_operating_profile_manager, true);
}

bool tdma_runtime_owner_cal_loopback_start(uint32_t sample_hz,
                                           uint32_t sample_words,
                                           uint32_t epoch)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_pio_spi_cal_loopback_snapshot_t loopback;
    tdma_cal_loopback_intent_t pending;
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_ring_runtime_get_snapshot(&s_tdma_runtime_owner.ring_runtime,
                                        &ring) || ring.enabled != 0u ||
        sample_words == 0u ||
        sample_words > TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS ||
        !tdma_pio_spi_phys_get_cal_loopback_snapshot(&s_tdma_pio_spi_phys,
                                                     &loopback) ||
        loopback.armed != 0u ||
        !tdma_runtime_owner_cal_intent_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_tdma_cal_loopback_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    tdma_runtime_owner_cal_intent_publish(TDMA_CAL_LOOPBACK_INTENT_START,
                                          sample_hz, sample_words, epoch);
    return true;
}

void tdma_runtime_owner_cal_loopback_stop(void)
{
    if (s_tdma_runtime_owner_initialized) {
        /* STOP supersedes an unconsumed START.  Both are core0 publications;
         * core1 will observe one complete seqlock record. */
        tdma_runtime_owner_cal_intent_publish(TDMA_CAL_LOOPBACK_INTENT_STOP,
                                              0u, 0u, 0u);
    }
}

void tdma_runtime_owner_cal_loopback_service(void)
{
    if (!s_tdma_runtime_owner_initialized) {
        return;
    }
    tdma_cal_loopback_intent_t intent;
    if (tdma_runtime_owner_cal_intent_read(&intent) &&
        intent.sequence != __atomic_load_n(
            &s_tdma_cal_loopback_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (intent.opcode == TDMA_CAL_LOOPBACK_INTENT_START) {
            tdma_ring_runtime_snapshot_t ring;
            if (tdma_ring_runtime_get_snapshot(
                    &s_tdma_runtime_owner.ring_runtime, &ring) &&
                ring.enabled == 0u) {
                (void)tdma_pio_spi_phys_cal_loopback_start(
                    &s_tdma_pio_spi_phys,
                    intent.sample_hz,
                    intent.sample_words,
                    intent.epoch);
            }
        } else if (intent.opcode == TDMA_CAL_LOOPBACK_INTENT_STOP) {
            tdma_pio_spi_phys_cal_loopback_stop(&s_tdma_pio_spi_phys);
        }
        __atomic_store_n(&s_tdma_cal_loopback_consumed_sequence,
                         intent.sequence, __ATOMIC_RELEASE);
    }
    tdma_pio_spi_phys_cal_loopback_service(&s_tdma_pio_spi_phys);
}

bool tdma_runtime_owner_get_cal_loopback_snapshot(
    tdma_pio_spi_cal_loopback_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_cal_loopback_snapshot(&s_tdma_pio_spi_phys,
                                                       snapshot);
}

bool tdma_runtime_owner_set_loop_delay_ns(uint32_t loop_delay_ns,
                                          uint32_t tolerance_ns)
{
    if (!s_tdma_runtime_owner_initialized) {
        return false;
    }
    return tdma_service_set_loop_delay_ns(&s_tdma_runtime_owner,
                                          loop_delay_ns,
                                          tolerance_ns);
}

bool tdma_runtime_owner_begin_calibration_stage(
    const tdma_ring_calibration_stage_t *header)
{
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_service_begin_calibration_stage(&s_tdma_runtime_owner,
                                              header)) {
        return false;
    }
    tdma_pio_spi_ring_adapter_clear_calibration_topology(
        &s_tdma_pio_spi_ring_adapter);
    return true;
}

bool tdma_runtime_owner_stage_calibration_link(
    const tdma_ring_calibration_link_t *link)
{
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_service_stage_calibration_link(&s_tdma_runtime_owner, link)) {
        return false;
    }
    tdma_ring_calibration_stage_t stage;
    bool complete = false;
    if (!tdma_service_get_calibration_stage(&s_tdma_runtime_owner,
                                            &stage, &complete)) {
        return false;
    }
    return !complete || tdma_pio_spi_ring_adapter_set_calibration_topology(
                            &s_tdma_pio_spi_ring_adapter, &stage);
}

bool tdma_runtime_owner_get_calibration_stage(
    tdma_ring_calibration_stage_t *stage,
    bool *complete)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_service_get_calibration_stage(&s_tdma_runtime_owner,
                                              stage, complete);
}

bool tdma_runtime_owner_clear_calibration_stage(void)
{
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_service_clear_calibration_stage(&s_tdma_runtime_owner)) {
        return false;
    }
    tdma_pio_spi_ring_adapter_clear_calibration_topology(
        &s_tdma_pio_spi_ring_adapter);
    return true;
}

bool tdma_runtime_owner_set_topology_probe_mode(bool enabled,
                                                uint32_t phase_delay_cycles)
{
    tdma_ring_runtime_snapshot_t ring;
    if (!s_tdma_runtime_owner_initialized ||
        !tdma_ring_runtime_get_snapshot(
            &s_tdma_runtime_owner.ring_runtime, &ring) ||
        ring.enabled != 0u || ring.adapter_started != 0u ||
        (enabled && (phase_delay_cycles == 0u ||
                     phase_delay_cycles > 31u)) ||
        !tdma_pio_spi_ring_adapter_set_topology_probe_mode(
            &s_tdma_pio_spi_ring_adapter, enabled)) {
        return false;
    }
    s_tdma_topology_probe_phase_delay_cycles =
        enabled ? phase_delay_cycles : 0u;
    return true;
}

bool tdma_runtime_owner_get_staged_ring_config(
    tdma_service_ring_runtime_config_t *snapshot)
{
    if (!s_tdma_runtime_owner_initialized || snapshot == NULL) {
        return false;
    }
    *snapshot = s_tdma_runtime_owner.ring_staged_config;
    return snapshot->enabled != 0u;
}

bool tdma_runtime_owner_coded_start_core1(
    const tdma_pio_spi_coded_request_t *request)
{
    tdma_ring_runtime_snapshot_t ring;
    return s_tdma_runtime_owner_initialized && request != NULL &&
           tdma_ring_runtime_get_snapshot(
               &s_tdma_runtime_owner.ring_runtime, &ring) &&
           ring.enabled == 0u &&
           tdma_pio_spi_phys_coded_start(&s_tdma_pio_spi_phys, request);
}

void tdma_runtime_owner_coded_stop_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_coded_stop(&s_tdma_pio_spi_phys);
    }
}

void tdma_runtime_owner_coded_service_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_coded_service(&s_tdma_pio_spi_phys);
    }
}

bool tdma_runtime_owner_get_coded_snapshot(
    tdma_pio_spi_coded_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_coded_snapshot(
               &s_tdma_pio_spi_phys, snapshot);
}

bool tdma_runtime_owner_copy_coded_capture_core1(
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_copy_coded_capture(
               &s_tdma_pio_spi_phys, capture_words,
               capture_word_capacity, capture_word_count);
}

bool tdma_runtime_owner_core0_publish_ring_tx(
    const uint8_t *data, size_t data_size, uint32_t generation,
    uint32_t sequence, uint32_t segment_mask)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_service_publish_flight_tx(&s_tdma_runtime_owner, data,
                                          data_size, generation, sequence,
                                          segment_mask);
}

bool tdma_runtime_owner_core0_acquire_ring_rx(tdma_flight_rx_view_t *view)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_service_acquire_flight_rx(&s_tdma_runtime_owner, view);
}

bool tdma_runtime_owner_core0_release_ring_rx(uint32_t slot_index)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_service_release_flight_rx(&s_tdma_runtime_owner, slot_index);
}

bool tdma_runtime_owner_marker_arm_core1(
    const tdma_pio_spi_marker_request_t *request)
{
    tdma_ring_runtime_snapshot_t ring;
    return s_tdma_runtime_owner_initialized && request != NULL &&
           tdma_ring_runtime_get_snapshot(
               &s_tdma_runtime_owner.ring_runtime, &ring) &&
           ring.enabled == 0u &&
           tdma_pio_spi_phys_marker_arm(&s_tdma_pio_spi_phys, request);
}

bool tdma_runtime_owner_marker_inject_core1(void)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_marker_inject(&s_tdma_pio_spi_phys);
}

void tdma_runtime_owner_marker_stop_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_marker_stop(&s_tdma_pio_spi_phys);
    }
}

void tdma_runtime_owner_marker_service_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_marker_service(&s_tdma_pio_spi_phys);
    }
}

bool tdma_runtime_owner_get_marker_snapshot(
    tdma_pio_spi_marker_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_marker_snapshot(
               &s_tdma_pio_spi_phys, snapshot);
}

bool tdma_runtime_owner_copy_marker_capture_core1(
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_copy_marker_capture(
               &s_tdma_pio_spi_phys, capture_words,
               capture_word_capacity, capture_word_count);
}

bool tdma_runtime_owner_data_train_arm_core1(
    const tdma_pio_spi_data_train_request_t *request)
{
    tdma_ring_runtime_snapshot_t ring;
    return s_tdma_runtime_owner_initialized && request != NULL &&
           tdma_ring_runtime_get_snapshot(
               &s_tdma_runtime_owner.ring_runtime, &ring) &&
           ring.enabled == 0u &&
           tdma_pio_spi_phys_data_train_arm(&s_tdma_pio_spi_phys, request);
}

bool tdma_runtime_owner_data_train_inject_core1(void)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_data_train_inject(&s_tdma_pio_spi_phys);
}

void tdma_runtime_owner_data_train_stop_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_data_train_stop(&s_tdma_pio_spi_phys);
    }
}

void tdma_runtime_owner_data_train_service_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_data_train_service(&s_tdma_pio_spi_phys);
    }
}

bool tdma_runtime_owner_get_data_train_snapshot(
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_data_train_snapshot(
               &s_tdma_pio_spi_phys, snapshot);
}

bool tdma_runtime_owner_copy_data_train_capture_core1(
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_copy_data_train_capture(
               &s_tdma_pio_spi_phys, capture_words,
               capture_word_capacity, capture_word_count);
}

bool tdma_runtime_owner_sck_train_arm_core1(
    const tdma_pio_spi_data_train_request_t *request)
{
    tdma_ring_runtime_snapshot_t ring;
    return s_tdma_runtime_owner_initialized && request != NULL &&
           tdma_ring_runtime_get_snapshot(
               &s_tdma_runtime_owner.ring_runtime, &ring) &&
           ring.enabled == 0u &&
           tdma_pio_spi_phys_sck_train_arm(&s_tdma_pio_spi_phys, request);
}

bool tdma_runtime_owner_sck_train_inject_core1(void)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_sck_train_inject(&s_tdma_pio_spi_phys);
}

void tdma_runtime_owner_sck_train_stop_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_sck_train_stop(&s_tdma_pio_spi_phys);
    }
}

void tdma_runtime_owner_sck_train_service_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_sck_train_service(&s_tdma_pio_spi_phys);
    }
}

bool tdma_runtime_owner_get_sck_train_snapshot(
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_sck_train_snapshot(
               &s_tdma_pio_spi_phys, snapshot);
}

bool tdma_runtime_owner_copy_sck_train_capture_core1(
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_copy_sck_train_capture(
               &s_tdma_pio_spi_phys, capture_words,
               capture_word_capacity, capture_word_count);
}

bool tdma_runtime_owner_p3_start_core1(
    const tdma_pio_spi_p3_request_t *request)
{
    tdma_ring_runtime_snapshot_t ring;
    return s_tdma_runtime_owner_initialized && request != NULL &&
           tdma_ring_runtime_get_snapshot(
               &s_tdma_runtime_owner.ring_runtime, &ring) &&
           ring.enabled == 0u &&
           tdma_pio_spi_phys_p3_start(&s_tdma_pio_spi_phys, request);
}

void tdma_runtime_owner_p3_stop_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_p3_stop(&s_tdma_pio_spi_phys);
    }
}

void tdma_runtime_owner_p3_service_core1(void)
{
    if (s_tdma_runtime_owner_initialized) {
        tdma_pio_spi_phys_p3_service(&s_tdma_pio_spi_phys);
    }
}

bool tdma_runtime_owner_get_p3_snapshot(
    tdma_pio_spi_p3_snapshot_t *snapshot)
{
    return s_tdma_runtime_owner_initialized &&
           tdma_pio_spi_phys_get_p3_snapshot(&s_tdma_pio_spi_phys, snapshot);
}
