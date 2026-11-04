#include "tdma_pio_spi_phys.h"
#include "tdma_service_timing.h"
#include "tdma_pio_spi_phys_programs.h"
#include "tdma_pio_spi_phys_internal.h"
#include "tdma_pio_spi_origin_workspace.h"
#include "tdma_origin_build_job.h"

#include <string.h>

#include "board_config.h"
#include "project_config.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "drv_watchdog.h"
#include "pico/bit_ops.h"
#include "pico/time.h"
#include "tdma_flight_overlay.h"
#include "tdma_pio_spi.pio.h"
#include "tdma_origin.pio.h"
#include "tdma_process_image_layout.h"
#include "tdma_pio_spi_phys_timing.h"
#include "tdma_rx_sequence.h"
#include "vdc_timestamp_clock.h"

uint8_t tdma_pio_spi_phys_rx_ring_byte(uint64_t produced)
{
    /* Only the process follower uses right-shifting ISR to support live XOR.
     * Normalize the observation copy; wire forwarding never reads this ring. */
    if (s_tdma_pio_spi_program_persona ==
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)
        return tdma_pio_spi_phys_rx_ring_reversed_byte(produced);
    return (uint8_t)tdma_pio_spi_phys_rx_ring_word(produced);
}

static uint8_t tdma_pio_spi_phys_rx_ring_aligned_byte(uint64_t produced,
                                                      uint32_t bit_shift)
{
    const uint8_t first = tdma_pio_spi_phys_rx_ring_byte(produced);
    if (bit_shift == 0u) {
        return first;
    }
    const uint8_t second = tdma_pio_spi_phys_rx_ring_byte(produced + 1u);
    return (uint8_t)(((uint32_t)first << bit_shift) |
                     ((uint32_t)second >> (8u - bit_shift)));
}

/* Callers prove the entire raw interval is completed before copying, then
 * recheck epoch and overwrite afterwards. Normalize each raw word once.
 * The Core1 owner cannot switch persona during this synchronous copy. Select
 * its ISR direction outside the loops; each ring word remains a volatile DMA
 * read, and configuration/STOP retain their existing owner boundaries.
 * Keep this bounded leaf in SRAM so its per-word loop does not fetch XIP
 * instructions while competing with the background parser. Its instruction
 * storage is part of the linked RAM budget; callers still prove retention. */
__attribute__((noinline, section(".time_critical.tdma_pio_spi_phys_rx_ring_copy")))
void tdma_pio_spi_phys_rx_ring_copy(uint8_t *destination,
    uint64_t produced, uint32_t count, uint32_t bit_shift)
{
    if (count == 0u) return;
    if (s_tdma_pio_spi_program_persona ==
        TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER) {
        if (bit_shift == 0u) {
            for (uint32_t i = 0u; i < count; ++i)
                destination[i] = tdma_pio_spi_phys_rx_ring_reversed_byte(produced + i);
            return;
        }
        uint8_t previous = tdma_pio_spi_phys_rx_ring_reversed_byte(produced);
        for (uint32_t i = 0u; i < count; ++i) {
            const uint8_t next = tdma_pio_spi_phys_rx_ring_reversed_byte(produced + i + 1u);
            destination[i] = (uint8_t)(((uint32_t)previous << bit_shift) |
                                      ((uint32_t)next >> (8u - bit_shift)));
            previous = next;
        }
        return;
    }
    if (bit_shift == 0u) {
        for (uint32_t i = 0u; i < count; ++i)
            destination[i] = (uint8_t)tdma_pio_spi_phys_rx_ring_word(produced + i);
        return;
    }
    uint8_t previous = (uint8_t)tdma_pio_spi_phys_rx_ring_word(produced);
    for (uint32_t i = 0u; i < count; ++i) {
        const uint8_t next = (uint8_t)tdma_pio_spi_phys_rx_ring_word(produced + i + 1u);
        destination[i] = (uint8_t)(((uint32_t)previous << bit_shift) |
                                  ((uint32_t)next >> (8u - bit_shift)));
        previous = next;
    }
}


static bool tdma_pio_spi_phys_transport_header_matches(
    uint64_t packet_start,
    uint32_t bit_shift,
    uint16_t frame_size)
{
    if (frame_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        return false;
    }
    const uint16_t transport_magic =
        (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start, bit_shift) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
             packet_start + 1u, bit_shift) << 8u);
    const uint16_t transport_size =
        (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start + 4u, bit_shift) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
             packet_start + 5u, bit_shift) << 8u);
    const uint8_t frame_class =
        tdma_pio_spi_phys_rx_ring_aligned_byte(
            packet_start + 3u, bit_shift);

    return transport_magic == TDMA_TRANSPORT_FRAME_MAGIC &&
           tdma_pio_spi_phys_rx_ring_aligned_byte(
               packet_start + 2u, bit_shift) ==
               TDMA_TRANSPORT_FRAME_VERSION &&
           (frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
            frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG) &&
           transport_size == frame_size &&
           tdma_pio_spi_phys_rx_ring_aligned_byte(
               packet_start + 6u, bit_shift) ==
               TDMA_TRANSPORT_FRAME_HEADER_SIZE;
}

/* An alignment a scan RECORDS must satisfy the same bound the origin BEGIN
 * ENFORCES before it arms anything (tdma_pio_spi_phys_origin.c: the whole packet
 * has to fit inside the physical frame from that offset). A candidate can match the
 * magic and the transport header while still resting on a wrong phase whose packet
 * would wrap the frame boundary; recording that phase poisoned the NEXT arm's
 * alignment. Measured 2026-10-13: a recorded byte_shift of 42 in a frame whose whole
 * prefix region is 9 bytes made BEGIN refuse check #38 (prefix 374 > 72), and that
 * refusal is latched as a permanent origin fault which STOPs the ring - the run was
 * lost while a peer was still joining. Rejecting the phase as an alignment witness
 * here keeps the recorder and the consumer on one predicate. */
static bool tdma_pio_spi_phys_capture_words_legacy(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (phys == NULL || received_words == NULL ||
        max_words == 0u || max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !phys->rx_capture_active || phys->flight_origin_workspace_owned ||
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN) {
        return false;
    }
    const uint64_t produced = tdma_pio_spi_phys_rx_produced_words(phys);
    const uint64_t observation_epoch = s_tdma_pio_spi_rx_sequence.observation_epoch;
    phys->snapshot.rx_dma_produced_words = (uint32_t)produced;
    phys->snapshot.rx_scan_produced_words =
        (uint32_t)s_tdma_pio_spi_rx_scan_produced;
    phys->snapshot.rx_dma_write_index = tdma_pio_spi_phys_rx_write_index();
    phys->snapshot.rx_dma_channel = (uint32_t)s_tdma_pio_spi_rx_dma_channel;
    if (produced <= s_tdma_pio_spi_rx_scan_produced) {
        return false;
    }

    if (produced - s_tdma_pio_spi_rx_scan_produced >
        TDMA_PIO_SPI_RX_RING_WORDS) {
        phys->snapshot.rx_ring_overrun_count++;
        s_tdma_pio_spi_rx_scan_produced =
            produced - TDMA_PIO_SPI_RX_RING_WORDS;
    }

    /* When the consumer falls behind, inspect a bounded newest window with
     * space for a complete packet plus one physical frame of candidate
     * positions. Do not spend subsequent beats chasing overwritten bytes. */
    const uint32_t keep_words = (uint32_t)max_words + TDMA_RX_OBSERVATION_SCAN_WORDS;
    if (produced - s_tdma_pio_spi_rx_scan_produced > keep_words) {
        tdma_service_timing_rx_drop(TDMA_RX_DROP_CLAMP,
            produced - s_tdma_pio_spi_rx_scan_produced - keep_words);
        s_tdma_pio_spi_rx_scan_produced = produced - keep_words;
        phys->snapshot.rx_observation_drop_count++;
    }

    uint64_t candidate = s_tdma_pio_spi_rx_scan_produced;
    uint32_t inspected = 0u;
    while (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE <= produced) {
        if (inspected++ == TDMA_RX_OBSERVATION_SCAN_WORDS) {
            s_tdma_pio_spi_rx_scan_produced = candidate;
            phys->snapshot.rx_scan_yield_count++;
            return false;
        }
        /* All candidate phases share the same three raw bytes. Read them
         * once, then test the two-byte magic in registers. Candidate bounds
         * already guarantee the complete outer-header prefix is published;
         * shifted length/header and post-copy epoch checks still apply. */
        const uint32_t prefix =
            ((uint32_t)tdma_pio_spi_phys_rx_ring_aligned_byte(candidate, 0u) << 16u) |
            ((uint32_t)tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + 1u, 0u) << 8u) |
            tdma_pio_spi_phys_rx_ring_aligned_byte(candidate + 2u, 0u);
        const uint32_t magic = (TDMA_PIO_SPI_PACKET_MAGIC0 << 8u) |
                               TDMA_PIO_SPI_PACKET_MAGIC1;
        for (uint32_t bit_shift = 0u; bit_shift < 8u; bit_shift++) {
            const uint32_t alignment_extra = bit_shift == 0u ? 0u : 1u;
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    alignment_extra > produced ||
                ((prefix >> (8u - bit_shift)) & 0xFFFFu) != magic) {
                continue;
            }
            const uint16_t frame_size =
                (uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
                    candidate + 2u, bit_shift) |
                ((uint16_t)tdma_pio_spi_phys_rx_ring_aligned_byte(
                     candidate + 3u, bit_shift) << 8u);
            const uint32_t total_words =
                TDMA_PIO_SPI_PACKET_HEADER_SIZE + frame_size;
            if (frame_size == 0u || total_words > max_words) {
                continue;
            }
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    TDMA_TRANSPORT_FRAME_HEADER_SIZE + alignment_extra >
                produced) {
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            if (!tdma_pio_spi_phys_transport_header_matches(
                    candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE,
                    bit_shift,
                    frame_size)) {
                continue;
            }
            if (candidate + total_words + alignment_extra > produced) {
                /* Header is valid, but the last bytes are still on the wire. */
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            tdma_pio_spi_phys_rx_ring_copy(s_tdma_pio_spi_rx_frame,
                candidate, total_words, bit_shift);
            __dmb();
            const uint64_t after_copy = tdma_pio_spi_phys_rx_produced_words(phys);
            /* Count retires completed SRAM writes. Exclude the next writer
             * slot as well; a boundary or full counter-epoch ambiguity must
             * never publish a mixed packet or update wire alignment. */
            if (s_tdma_pio_spi_rx_sequence.observation_epoch != observation_epoch ||
                after_copy < produced || after_copy - candidate >= TDMA_PIO_SPI_RX_RING_WORDS) {
                tdma_service_timing_rx_drop(TDMA_RX_DROP_FRAME_COPY, 0u);
                phys->snapshot.rx_observation_drop_count++;
                s_tdma_pio_spi_rx_scan_produced = after_copy >= produced ? after_copy : produced;
                return false;
            }
            /* A shifted aligned byte uses raw[i] and raw[i+1].  The final
             * raw word is therefore also the first word for the next aligned
             * byte/frame; consuming alignment_extra here skips every next
             * frame when the origin capture has no idle raw byte between
             * frames. */
            s_tdma_pio_spi_rx_scan_produced = candidate + total_words;
            /* Parser-copy loss may move candidate without moving the wire
             * slots. Freeze the physical alignment after first publication
             * for this ARM epoch; parsing may still realign its own copy. */
            if (phys->process_image_enabled &&
                !phys->flight_overlay_alignment_locked &&
                phys->flight_physical_byte_count != 0u) {
                const uint32_t byte_shift = (uint32_t)(candidate %
                    phys->flight_physical_byte_count);
                if (tdma_pio_spi_phys_alignment_fits(phys, byte_shift, bit_shift)) {
                    if (phys->flight_overlay_alignment_samples != 0u &&
                        candidate - phys->flight_overlay_alignment_candidate ==
                            phys->flight_physical_byte_count &&
                        byte_shift == phys->flight_alignment_byte_shift &&
                        bit_shift == phys->flight_alignment_bit_shift) {
                        if (phys->flight_overlay_alignment_samples <
                                TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES)
                            phys->flight_overlay_alignment_samples++;
                    } else {
                        phys->flight_overlay_alignment_samples = 1u;
                    }
                    phys->flight_overlay_alignment_candidate = candidate;
                    phys->flight_alignment_byte_shift = byte_shift;
                    phys->flight_alignment_bit_shift = bit_shift;
                }
                /* A phase that does not fit the physical frame is not an alignment
                 * witness: leave the recorded alignment and its stability count
                 * untouched (see tdma_pio_spi_phys_alignment_fits). Clearing the count
                 * here instead would make the NEXT arm refuse check #27 "overlay
                 * alignment unstable" - measured 2026-10-13. The frame itself is still
                 * returned to the caller below. */
            }
            if (bit_shift == 0u) {
                phys->snapshot.rx_magic_at_zero++;
            } else {
                phys->snapshot.rx_magic_at_shift++;
            }
            *received_words = total_words;
            return true;
        }
        candidate++;
    }
    /* A shifted four-byte outer header consumes one additional raw word.
     * Preserve that full prefix across service passes: retaining only the
     * two-byte magic can discard a valid candidate when DMA publication is
     * observed after four raw words but before the fifth word arrives. */
    const uint64_t retain_words =
        TDMA_PIO_SPI_PACKET_HEADER_SIZE + 1u;
    const uint64_t bad_start = s_tdma_pio_spi_rx_scan_produced;
    const uint64_t bad_available = produced - bad_start;
    phys->snapshot.last_bad_header0 =
        bad_available > 0u ? tdma_pio_spi_phys_rx_ring_word(bad_start) : 0u;
    phys->snapshot.last_bad_header1 =
        bad_available > 1u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 1u) : 0u;
    phys->snapshot.last_bad_header2 =
        bad_available > 2u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 2u) : 0u;
    phys->snapshot.last_bad_header3 =
        bad_available > 3u ? tdma_pio_spi_phys_rx_ring_word(bad_start + 3u) : 0u;
    phys->snapshot.last_bad_words = (uint32_t)bad_available;
    phys->snapshot.rx_magic_fail_count++;
    s_tdma_pio_spi_rx_scan_produced =
        produced > retain_words ? produced - retain_words : 0u;
    return false;
}


static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (phys != NULL && phys->rx_scan_preparation != NULL &&
        tdma_pio_spi_phys_is_flight_persona())
        return tdma_pio_spi_phys_capture_words_async(phys, max_words, received_words);
    return tdma_pio_spi_phys_capture_words_legacy(phys, max_words, received_words);
}

bool tdma_pio_spi_phys_capture_words_ex(tdma_pio_spi_phys_t *phys,
    size_t max_words, size_t *received_words, tdma_rx_capture_t *capture)
{
    if (capture == NULL)
        return tdma_pio_spi_phys_capture_words(phys, max_words, received_words);
    memset(capture, 0, sizeof(*capture));
    if (phys != NULL && phys->rx_scan_preparation != NULL &&
        tdma_pio_spi_phys_is_flight_persona())
        return tdma_pio_spi_phys_capture_words_async_ex(phys, max_words, received_words, capture);
    return tdma_pio_spi_phys_capture_words_legacy(phys, max_words, received_words);
}

bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config)
{
    if (!s_geometry_request_pending &&
        !tdma_pio_spi_phys_geometry_arm_requested(context, config)) return false;
    s_tdma_pio_spi_rx_arm_valid = false;
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys != NULL) tdma_rx_start_cut_arm_begin();
    if (phys != NULL && (phys->armed || phys->flight_overlay_dma_active ||
                         tdma_overlay_prepare_state(phys->overlay_preparation) != TDMA_OVERLAY_PREPARE_IDLE ||
                         tdma_rx_scan_state(phys->rx_scan_preparation) != TDMA_RX_SCAN_IDLE ||
                         phys->flight_origin_workspace_owned ||
                         phys->flight_origin_prepare.stage != TDMA_ORIGIN_PREPARE_IDLE)) {
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY);
    }
    if (phys == NULL || config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->local_slot_id >= config->node_count ||
        config->tx_dma_channel_id != TDMA_PIO_SPI_TX_DMA_CHANNEL ||
        config->rx_dma_channel_id != TDMA_PIO_SPI_RX_DMA_CHANNEL) {
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
    }
    phys->role = (config->local_slot_id == config->reference_slot_id)
                     ? TDMA_PIO_SPI_ROLE_MASTER
                     : TDMA_PIO_SPI_ROLE_SLAVE;
    phys->baud_hz = config->baud_hz;
    phys->flight_cycle_period_ns = config->cycle_period_ns;
    const uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    const uint32_t period_cycles = phys->baud_hz == 0u
        ? 0u : clk_sys_hz / phys->baud_hz;
    const uint32_t half_period_cycles = phys->baud_hz == 0u
        ? 0u : clk_sys_hz / (2u * phys->baud_hz);
    if (period_cycles == 0u || half_period_cycles == 0u) {
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION);
    }
    const bool diagnostic_continue =
        (config->flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) != 0u;
    const bool process_follower = phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
                                  phys->process_image_enabled;
    if (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
        (phys->flight_origin_capture_phase_delay_cycles == 0u ||
         phys->flight_origin_capture_phase_delay_cycles > 31u)) {
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION);
    }
    if (process_follower && (phys->flight_data_phase_delay_cycles <
            TDMA_PIO_SPI_PROCESS_DATA_DECODE_CYCLES ||
            phys->flight_data_phase_delay_cycles > 31u)) {
        /* Illegal instruction encoding cannot be forced through Debug. */
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION);
    }
    const uint32_t process_sample_path = phys->flight_data_phase_delay_cycles +
                                        TDMA_PIO_SPI_PROCESS_DATA_DECODE_CYCLES;
    const uint32_t high_cycles = (period_cycles + 1u) / 2u;
    const uint32_t data_rearm = process_follower
        ? (process_sample_path > high_cycles ? process_sample_path : high_cycles) +
              TDMA_PIO_SPI_PROCESS_BYTE_REARM_CYCLES
        : (phys->role == TDMA_PIO_SPI_ROLE_SLAVE
            ? (process_sample_path > high_cycles ? process_sample_path : high_cycles) +
                  TDMA_PIO_SPI_RAW_BYTE_REARM_CYCLES
            : phys->flight_data_phase_delay_cycles + TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES);
    const bool phase_admission_warning =
        (phys->role == TDMA_PIO_SPI_ROLE_MASTER &&
         phys->flight_origin_capture_phase_delay_cycles +
             TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES > period_cycles) ||
        phys->flight_sck_phase_delay_cycles <
            TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES ||
        (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
         phys->flight_sck_phase_delay_cycles +
              TDMA_PIO_SPI_FLIGHT_SCK_REARM_CYCLES > half_period_cycles) ||
        data_rearm > period_cycles;
    if (phase_admission_warning && !diagnostic_continue) {
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION);
    }
    (void)tdma_pio_spi_phys_rx_scan_cancel(phys);
    phys->node_count = config->node_count;
    phys->flight_local_slot_id = config->local_slot_id;
    phys->flight_tail_bytes = tdma_pio_spi_phys_flight_tail_bytes(config);
    if (phys->flight_tail_bytes > TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES) {
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_TAIL_CAPACITY);
    }
    const tdma_pio_spi_program_persona_t flight_persona =
        phys->role == TDMA_PIO_SPI_ROLE_MASTER
            ? TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN
            : (phys->process_image_enabled
                   ? TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER
                   : TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER);
    const uint32_t flight_payload_size = phys->flight_payload_size != 0u
        ? phys->flight_payload_size : TDMA_TRANSPORT_SHORT_PAYLOAD_MAX;
    phys->flight_physical_byte_count =
        TDMA_PIO_SPI_PACKET_HEADER_SIZE +
        TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        flight_payload_size +
        phys->flight_tail_bytes;
    if (phys->process_image_enabled) {
        if (phys->flight_physical_byte_count + 1u >
            TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS) {
            return tdma_pio_spi_phys_arm_reject(
                phys, TDMA_PIO_SPI_PHYS_ERROR_OVERLAY_PREPARE);
        }
    }
    const uint64_t arm_geometry_start = tdma_service_timing_now();
    const bool arm_geometry_ok = tdma_geometry_arm_begin(phys, config, flight_persona);
    tdma_service_timing_record(TDMA_TIMING_ARM_GEOMETRY, arm_geometry_start);
    if (!arm_geometry_ok)
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
    tdma_pio_spi_phys_origin_record_invalidate(phys);
    phys->flight_alignment_byte_shift = 0u;
    phys->flight_alignment_bit_shift = 0u;
    phys->flight_overlay_alignment_locked = false;
    phys->flight_overlay_alignment_samples = 0u;
    phys->flight_overlay_alignment_candidate = 0u;
    phys->flight_overlay_pending = false;
    phys->flight_overlay_active_buffer = 0u;
    phys->flight_overlay_pending_buffer = 0u;
    phys->flight_overlay_published_generation = 0u;
    phys->flight_overlay_selected_generation = 0u;
    phys->flight_overlay_next_address = 0u;
    phys->snapshot.overlay_published_generation = 0u;
    phys->snapshot.overlay_selected_generation = 0u;
    phys->snapshot.overlay_selection_pending = 0u;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_completion_timestamp_ns = 0ull;
    phys->flight_tx_packet_size = 0u;
    phys->flight_tx_wire_bytes = 0u;
    phys->flight_tx_launch_timestamp_ns = 0ull;
    phys->flight_tx_deadline_ns = 0ull;
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
    phys->flight_sck_waveform_capture_deadline_us = 0ull;
    phys->flight_normal_capture_copy_stage = 0u;
    phys->flight_normal_capture_sck_cursor = 0u;
    phys->flight_normal_capture_restore_stage = 0u;
    phys->flight_normal_capture_rx_produced = 0u;
    phys->flight_normal_capture_rx_start = 0u;
    phys->flight_normal_capture_rx_count = 0u;
    phys->flight_normal_capture_rx_cursor = 0u;
    const uint64_t arm_persona_start = tdma_service_timing_now();
    const bool arm_persona_ok =
        tdma_pio_spi_phys_select_program_persona(phys, flight_persona);
    tdma_service_timing_record(TDMA_TIMING_ARM_SELECT_PERSONA, arm_persona_start);
    if (!arm_persona_ok) {
        return false;
    }
    const uint64_t arm_configure_start = tdma_service_timing_now();
    const bool arm_configure_ok =
        tdma_pio_spi_phys_configure_flight(phys, config);
    tdma_service_timing_record(TDMA_TIMING_ARM_CONFIGURE, arm_configure_start);
    if (!arm_configure_ok) {
        tdma_pio_spi_phys_release_flight_resources(phys);
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_FLIGHT_CONFIG);
    }
    tdma_pio_spi_phys_set_line_drivers(true);
    const uint64_t arm_rx_start = tdma_service_timing_now();
    const bool arm_rx_ok = tdma_pio_spi_phys_rx_arm(phys);
    tdma_service_timing_record(TDMA_TIMING_ARM_RX, arm_rx_start);
    if (!arm_rx_ok) {
        tdma_pio_spi_phys_set_line_drivers(false);
        tdma_pio_spi_phys_release_flight_resources(phys);
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_RX_ARM);
    }
    /* rx_arm clears both FIFOs of the capture/forward SM. Prime the process
     * command DMA only after that reset so its initial PASS script survives. */
    bool arm_overlay_ok = true;
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE &&
        phys->process_image_enabled) {
        const uint64_t arm_overlay_start = tdma_service_timing_now();
        arm_overlay_ok = tdma_pio_spi_phys_prepare_pass_overlay(phys);
        tdma_service_timing_record(TDMA_TIMING_ARM_OVERLAY, arm_overlay_start);
    }
    if (!arm_overlay_ok) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_set_line_drivers(false);
        tdma_pio_spi_phys_release_flight_resources(phys);
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_OVERLAY_PREPARE);
    }
    if (process_follower) {
        const uint64_t deadline = tdma_pio_spi_phys_now_us() +
                                  TDMA_PIO_SPI_COMMAND_STOP_TIMEOUT_US;
        while (pio_sm_is_tx_fifo_empty(tdma_pio_spi_phys_data_pio(phys),
                                       tdma_pio_spi_phys_data_sm(phys))) {
            if (tdma_pio_spi_phys_now_us() >= deadline) {
                tdma_pio_spi_phys_disarm(phys);
                return tdma_pio_spi_phys_arm_reject(phys,
                    TDMA_PIO_SPI_PHYS_ERROR_OVERLAY_PREPARE);
            }
        }
        pio_sm_exec(tdma_pio_spi_phys_data_pio(phys), tdma_pio_spi_phys_data_sm(phys),
                    pio_encode_pull(false, true));
    }
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        const uint32_t control_bits = phys->flight_physical_byte_count * 8u;
        if (control_bits == 0u) {
            dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
            tdma_pio_spi_phys_set_line_drivers(false);
            tdma_pio_spi_phys_release_flight_resources(phys);
            return tdma_pio_spi_phys_arm_reject(
                phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        pio_sm_put_blocking(tdma_pio_spi_phys_control_pio(phys),
                            tdma_pio_spi_phys_control_sm(phys),
                            control_bits - 1u);
        pio_sm_exec(tdma_pio_spi_phys_control_pio(phys),
                    tdma_pio_spi_phys_control_sm(phys),
                    pio_encode_pull(false, true));
    }
    pio_interrupt_clear(tdma_pio_spi_phys_data_pio(phys), 3u);
    const uint64_t arm_latch_start = tdma_service_timing_now();
    const bool arm_latch_ok = tdma_pio_spi_phys_clock_latch_rearm(phys);
    tdma_service_timing_record(TDMA_TIMING_ARM_LATCH, arm_latch_start);
    if (!arm_latch_ok) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_set_line_drivers(false);
        tdma_pio_spi_phys_release_flight_resources(phys);
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_CLOCK_LATCH);
    }
    bool arm_tx_latch_ok = true;
    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        const uint64_t arm_tx_latch_start = tdma_service_timing_now();
        arm_tx_latch_ok = tdma_pio_spi_phys_tx_clock_latch_rearm(phys);
        tdma_service_timing_record(TDMA_TIMING_ARM_LATCH, arm_tx_latch_start);
    }
    if (!arm_tx_latch_ok) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_set_line_drivers(false);
        tdma_pio_spi_phys_release_flight_resources(phys);
        return tdma_pio_spi_phys_arm_reject(
            phys, TDMA_PIO_SPI_PHYS_ERROR_CLOCK_LATCH);
    }
    if (process_follower) {
        /* The capture program starts with SET Y / WAIT SCK. Hold its first
         * entry until RXCS is low so idle-CS clocks cannot consume PASS bits.
         * Inject after all PULL/seed/restart operations: another SM_INSTR write
         * would replace this wait. It resumes the unchanged entry PC and adds
         * no CPU wait or instruction-memory slot. CS already low (including a
         * low pulse while disabled) remains late-entry diagnostic evidence;
         * this one-shot guard does not establish a clean frame identity. */
        pio_sm_exec(tdma_pio_spi_phys_data_pio(phys),
                    tdma_pio_spi_phys_data_sm(phys),
                    pio_encode_wait_gpio(false, phys->rx_csn_pin));
    }
    if (!tdma_geometry_arm_bound(phys)) {
        (void)tdma_pio_spi_phys_disarm(phys);
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
    }
    const uint64_t arm_sm_enable_start = tdma_service_timing_now();
    tdma_pio_spi_phys_enable_sm_pair(phys);
    tdma_service_timing_record(TDMA_TIMING_ARM_SM_ENABLE, arm_sm_enable_start);

    phys->armed = true;
    phys->snapshot.tx_count = 0u;
    phys->snapshot.rx_count = 0u;
    phys->snapshot.rx_bad_count = 0u;
    phys->snapshot.tx_busy_count = 0u;
    phys->snapshot.rx_partial_count = 0u;
    phys->snapshot.rx_stall_count = 0u;
    phys->snapshot.tx_timeout_count = 0u;
    phys->snapshot.rx_busy_count = 0u;
    phys->snapshot.rx_magic_fail_count = 0u;
    phys->snapshot.rx_magic_at_zero = 0u;
    phys->snapshot.rx_magic_at_shift = 0u;
    phys->snapshot.rx_ring_overrun_count = 0u;
    phys->snapshot.rx_dma_produced_words = 0u;
    phys->snapshot.rx_scan_produced_words = 0u;
    phys->snapshot.rx_dma_write_index = 0u;
    phys->snapshot.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    phys->snapshot.tx_edge_count = 0u;
    phys->snapshot.rx_edge_count = 0u;
    phys->snapshot.last_tx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_tx_done_timestamp_ns = 0ull;
    phys->snapshot.last_rx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_rx_extract_timestamp_ns = 0ull;
    phys->snapshot.clock_latch_count = 0u;
    phys->snapshot.clock_latch_miss_count = 0u;
    phys->flight_tx_edge_capture_generation = 0u;
    phys->snapshot.origin_done_irq_count = 0u;
    phys->snapshot.origin_done_txstall_count = 0u;
    phys->snapshot.origin_clock_timeout_count = 0u;
    phys->snapshot.origin_data_timeout_count = 0u;
    phys->snapshot.origin_recovery_count = 0u;
    phys->snapshot.overlay_prepare_count = 0u;
    phys->snapshot.overlay_prepare_fail_count = 0u;
    phys->snapshot.overlay_replacement_byte_count = 0u;
    phys->snapshot.overlay_alignment_byte_shift = 0u;
    phys->snapshot.overlay_alignment_bit_shift = 0u;
    phys->snapshot.overlay_physical_byte_count =
        phys->flight_physical_byte_count;
    phys->snapshot.overlay_last_error = TDMA_PIO_SPI_OVERLAY_ERROR_NONE;
    phys->snapshot.overlay_tx_dma_remaining = 0u;
    phys->snapshot.overlay_tx_dma_busy = 0u;
    phys->snapshot.overlay_tx_fifo_level_at_fail = 0u;
    phys->snapshot.overlay_prepare_wait_us = 0u;
    phys->snapshot.overlay_program_offset =
        phys->role == TDMA_PIO_SPI_ROLE_SLAVE && phys->process_image_enabled
            ? s_tdma_pio_spi_flight_process_follower_offset
            : 0u;
    phys->snapshot.overlay_tx_dma_read_index = 0u;
    phys->snapshot.overlay_tx_dma_ctrl = 0u;
    phys->snapshot.overlay_sm_shiftctrl = 0u;
    phys->snapshot.overlay_sm_execctrl = 0u;
    phys->snapshot.overlay_sm_pc_at_fail = 0u;
    phys->snapshot.overlay_pio_ctrl_at_fail = 0u;
    phys->snapshot.overlay_pio_fstat_at_fail = 0u;
    phys->snapshot.overlay_pio_fdebug_at_fail = 0u;
    phys->snapshot.overlay_frame_boundary_count = 0u;
    phys->snapshot.overlay_pass_recovery_count = 0u;
    phys->snapshot.overlay_late_coalesce_count = 0u;
    phys->snapshot.overlay_reuse_observation_count = 0u;
    phys->snapshot.rx_observation_drop_count = 0u;
    phys->snapshot.rx_scan_yield_count = 0u;
    phys->snapshot.rx_busy_word0 = 0u;
    phys->snapshot.rx_busy_word1 = 0u;
    phys->snapshot.rx_busy_word2 = 0u;
    phys->snapshot.rx_busy_word3 = 0u;
    phys->snapshot.rx_busy_moved = 0u;
    phys->snapshot.last_bad_header0 = 0u;
    phys->snapshot.last_bad_header1 = 0u;
    phys->snapshot.last_bad_header2 = 0u;
    phys->snapshot.last_bad_header3 = 0u;
    phys->snapshot.last_bad_words = 0u;
    phys->snapshot.last_error = phase_admission_warning
        ? TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION
        : TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_clk_train_reset(phys);
    /* Selected geometry must finish its one-shot observer launch inside this
     * ARM. A later service may never turn a rejected first prefix into READY. */
    const uint64_t arm_prelaunch_start = tdma_service_timing_now();
    const bool arm_prelaunch_ok = tdma_pio_spi_phys_event_prelaunch(phys, config);
    tdma_service_timing_record(TDMA_TIMING_ARM_PRELAUNCH, arm_prelaunch_start);
    if (!arm_prelaunch_ok) {
        (void)tdma_pio_spi_phys_disarm(phys);
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
    }
    /* Keep the single literal `if (process_follower) {` block earlier in this
     * function unique: the production-slice harness anchors its arm tail on
     * that exact text. */
    const uint64_t arm_priority_start = tdma_service_timing_now();
    const bool arm_priority_ok =
        !process_follower || tdma_priority_start(phys, config);
    if (process_follower)
        tdma_service_timing_record(TDMA_TIMING_ARM_PRIORITY, arm_priority_start);
    if (!arm_priority_ok) {
        (void)tdma_pio_spi_phys_disarm(phys);
        return tdma_pio_spi_phys_arm_reject(phys, TDMA_PIO_SPI_PHYS_ERROR_RESOURCE_CONFLICT);
    }
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_disarm(void *context)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL) {
        return false;
    }
    const uint64_t disarm_event_start = tdma_service_timing_now();
    tdma_priority_stop();
    /* Record the observer's normal STOP while its selected binding is still
     * current. Geometry capture below does not need the observer SMs alive. */
    tdma_pio_spi_phys_event_stop(phys);
    tdma_service_timing_record(TDMA_TIMING_PHYS_DISARM_EVENT, disarm_event_start);
#if PROJECT_TDMA_EVENT_OBSERVER && defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    if (s_tdma_event_dma_active) {
        tdma_pio_spi_phys_set_line_drivers(false);
        tdma_pio_spi_phys_pause_sm_pair(phys);
        phys->armed = false;
        phys->snapshot.armed = 0u;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
        return false;
    }
#endif
    tdma_geometry_stop_begin(phys);
    /* Explicit STOP cancels even a sparse ARM admission. Successful cleanup
     * below adds STOPPED only after DMA and both workers have retired. */
    tdma_rx_first_window_retire(TDMA_RX_FIRST_STOP_BEFORE_COMPLETE, false);
    s_tdma_pio_spi_rx_arm_valid = false;
    const uint64_t disarm_cancel_start = tdma_service_timing_now();
    const bool worker_retired = tdma_overlay_prepare_cancel(phys->overlay_preparation);
    const bool scanner_retired = tdma_pio_spi_phys_rx_scan_cancel(phys);
    tdma_service_timing_record(TDMA_TIMING_PHYS_DISARM_CANCEL, disarm_cancel_start);
    /* Process-image followers keep the overlay TX DMA blocked on the PIO TX
     * FIFO between frames.  A failed ARM can also leave a DMA or SM active
     * before phys->armed is published.  STOP is the common idempotent
     * rollback for both states, so hardware cleanup must not be conditional
     * on the software armed flag. */
    const uint64_t disarm_dma_start = tdma_service_timing_now();
    const bool command_dma_stopped = tdma_pio_spi_phys_stop_command_dma(phys);
    tdma_service_timing_record(TDMA_TIMING_PHYS_DISARM_DMA, disarm_dma_start);
    if (!command_dma_stopped) return false;
    /* stop_command_dma also retires legacy children without a loader. Do not
     * re-enter the SDK's unbounded abort after that common deadline. */
    if (phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_PATCHED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_ARMED ||
        phys->flight_sck_waveform_capture_state ==
            TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_READY) {
        (void)tdma_pio_spi_phys_restore_clock_latch(phys, false);
    }
    tdma_pio_spi_phys_set_line_drivers(false);
    if (tdma_pio_spi_phys_is_flight_persona()) {
        tdma_pio_spi_phys_prepare_sm_pair(phys);
    } else {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                           BOARD_TDMA_SPI_CAPTURE_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,
                           BOARD_TDMA_SPI_RTT_SM, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO,
                           BOARD_TDMA_SPI_RTT_SM);
    }
    gpio_set_function(phys->tx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(phys->tx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->tx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->tx_pin, GPIO_IN);
    gpio_set_dir(phys->rx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->rx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->rx_pin, GPIO_IN);
    phys->armed = false;
    phys->flight_clock_latch_armed = false;
    phys->flight_tx_clock_latch_armed = false;
    phys->flight_sck_waveform_capture_state =
        TDMA_PIO_SPI_RING_WAVEFORM_CAPTURE_IDLE;
    phys->flight_sck_waveform_capture_deadline_us = 0ull;
    phys->rx_capture_active = false;
    phys->flight_overlay_pending = false;
    phys->flight_overlay_alignment_locked = false;
    phys->flight_overlay_alignment_samples = 0u;
    phys->snapshot.overlay_selection_pending = 0u;
    phys->flight_tx_pending = false;
    phys->flight_tx_completion_pending = false;
    phys->flight_tx_packet_size = 0u;
    phys->flight_tx_wire_bytes = 0u;
    phys->flight_tx_deadline_ns = 0ull;
    tdma_pio_spi_phys_clk_train_reset(phys);
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    const uint64_t release_start = tdma_service_timing_now();
    tdma_pio_spi_phys_release_flight_resources(phys);
    tdma_service_timing_record(TDMA_TIMING_PHYS_RELEASE, release_start);
    if (phys->flight_resource_claimed || phys->flight_origin_resource_claimed) return false;
    /* Also cancel preparation before persona claim, when no command DMA
     * exists and stop_command_dma has no active graph to retire. */
    phys->flight_origin_workspace_owned = false;
    phys->flight_origin_rx_observation_ready = false;
    memset(&phys->flight_origin_prepare, 0, sizeof(phys->flight_origin_prepare));
    /* Hardware is already stopped. A cancelled Core0 writer keeps STOP
     * pending until ACK, so neither ARM nor the origin union can reuse it. */
    if (worker_retired && scanner_retired) {
        tdma_rx_start_cut_disarmed();
        tdma_rx_first_window_retire(TDMA_RX_FIRST_STOP_BEFORE_COMPLETE, true);
        s_geometry_physical_stopped = true;
    }
    return worker_retired && scanner_retired;
}

bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys,
                                     uint32_t word)
{
    const uint64_t deadline_us =
        tdma_pio_spi_phys_now_us() + TDMA_PIO_SPI_TX_PUT_TIMEOUT_US;
    while (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        if (tdma_pio_spi_phys_now_us() >= deadline_us) {
            phys->snapshot.tx_timeout_count++;
            return false; /* SM stopped: do not hang core1. */
        }
    }
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, word);
    return true;
}

bool tdma_pio_spi_phys_train_clock(void *context, uint32_t cycles)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys != NULL && !tdma_overlay_prepare_cancel(phys->overlay_preparation)) return false;
    if (phys != NULL && !tdma_pio_spi_phys_rx_scan_cancel(phys)) return false;
    if (phys == NULL || !phys->armed || cycles == 0u ||
        cycles > TDMA_PIO_SPI_TRAIN_CLOCK_MAX_CYCLES) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
            tdma_pio_spi_phys_clk_train_write_begin(phys);
            phys->clk_train.request_seq++;
            phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
            phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_REJECTED;
            phys->clk_train.requested_cycles = cycles;
            tdma_pio_spi_phys_clk_train_write_end(phys);
        }
        return false;
    }

    /* Clock training replaces the complete resident flight persona.  The
     * product persona may have both the process-overlay TX DMA and the
     * independent CS clock-latch SM active, while the reference also owns
     * the RTT SM.  Quiesce every TDMA-owned execution resource before asking
     * the PIO allocator to remove and replace the program set. */
    if (!tdma_pio_spi_phys_stop_command_dma(phys)) return false;
    if (s_tdma_pio_spi_tx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_tx_dma_channel);
    }
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    phys->flight_clock_latch_armed = false;
    tdma_pio_spi_phys_prepare_sm_pair(phys);
    if (!tdma_pio_spi_phys_select_program_persona(
            phys, TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE)) {
        tdma_pio_spi_phys_set_error(
            phys, TDMA_PIO_SPI_PHYS_ERROR_RESOURCE_CONFLICT);
        tdma_pio_spi_phys_clk_train_write_begin(phys);
        phys->clk_train.request_seq++;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
        phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_REJECTED;
        phys->clk_train.requested_cycles = cycles;
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return false;
    }
    /* Maintenance personas return to the legacy PIO2 SM assignment after
     * the direction-isolated flight programs have been unloaded. */
    phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
    for (uint32_t irq = 0u; irq < 4u; irq++) {
        pio_interrupt_clear(BOARD_TDMA_SPI_PIO, irq);
    }
    gpio_put(phys->tx_csn_pin, true);

    const uint32_t request_seq = phys->clk_train.request_seq + 1u;
    const uint64_t burst_duration_ns =
        ((uint64_t)cycles * 1000000000ull + phys->baud_hz - 1ull) /
        phys->baud_hz;
    tdma_pio_spi_phys_clk_train_write_begin(phys);
    memset(&phys->clk_train, 0, sizeof(phys->clk_train));
    phys->clk_train.version = TDMA_PIO_SPI_CLK_TRAIN_SNAPSHOT_VERSION;
    phys->clk_train.role = phys->role;
    phys->clk_train.request_seq = request_seq;
    phys->clk_train.baud_hz = phys->baud_hz;
    phys->clk_train.requested_cycles = cycles;
    phys->clk_train.tx_sck_pin = phys->tx_sck_pin;
    phys->clk_train.rx_sck_pin = phys->rx_sck_pin;
    phys->clk_train.timestamp_resolution_ns =
        vdc_timestamp_clock_resolution_ns();
    phys->clk_train.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    phys->clk_train.burst_duration_ns = burst_duration_ns;

    if (phys->role == TDMA_PIO_SPI_ROLE_SLAVE) {
        tdma_pio_spi_clk_forward_program_init(
            BOARD_TDMA_SPI_PIO,
            phys->tx_sm,
            s_tdma_pio_spi_clk_forward_offset,
            phys->rx_sck_pin,
            phys->tx_sck_pin);
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_FORWARDING;
        phys->clk_train.result =
            TDMA_PIO_SPI_CLK_TRAIN_RESULT_FORWARD_ARMED;
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return true;
    }

    tdma_pio_spi_clk_burst_program_init(BOARD_TDMA_SPI_PIO,
                                        phys->tx_sm,
                                        s_tdma_pio_spi_clk_burst_offset,
                                        phys->tx_sck_pin,
                                        phys->baud_hz);
    tdma_pio_spi_clk_capture_program_init(
        BOARD_TDMA_SPI_PIO,
        phys->rx_sm,
        s_tdma_pio_spi_clk_capture_offset,
        phys->rx_sck_pin);
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, cycles - 1u);
    const uint64_t tx_start_timestamp_ns = vdc_timestamp_clock_now_ns();
    phys->clk_train.tx_start_timestamp_ns = tx_start_timestamp_ns;
    phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING;
    phys->clk_train.result = TDMA_PIO_SPI_CLK_TRAIN_RESULT_NONE;
    phys->clk_train_return_deadline_ns =
        tdma_pio_spi_phys_now_us() * 1000ull + burst_duration_ns +
        TDMA_PIO_SPI_TRAIN_RETURN_TIMEOUT_NS;
    pio_enable_sm_mask_in_sync(
        BOARD_TDMA_SPI_PIO,
        (1u << phys->tx_sm) | (1u << phys->rx_sm));
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_clk_train_write_end(phys);
    return true;
}

void tdma_pio_spi_phys_train_clock_service(void *context, uint64_t now_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed) {
        return;
    }

    const uint32_t state = phys->clk_train.state;
    if (state != TDMA_PIO_SPI_CLK_TRAIN_FORWARDING &&
        state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING) {
        return;
    }

    tdma_pio_spi_phys_clk_train_write_begin(phys);
    phys->clk_train.service_count++;
    if (state == TDMA_PIO_SPI_CLK_TRAIN_FORWARDING) {
        tdma_pio_spi_phys_clk_train_write_end(phys);
        return;
    }

    const bool return_seen =
        pio_interrupt_get(BOARD_TDMA_SPI_PIO, 2u);
    const bool tx_done = pio_interrupt_get(BOARD_TDMA_SPI_PIO, 1u);
    if (tx_done && phys->clk_train.tx_done_observed_timestamp_ns == 0ull) {
        phys->clk_train.tx_done_observed_timestamp_ns = now_ns;
    }
    if (return_seen) {
        phys->clk_train.return_seen = 1u;
        phys->clk_train.return_before_tx_done = tx_done ? 0u : 1u;
        phys->clk_train.return_observed_timestamp_ns = now_ns;
        phys->clk_train.result = tx_done
                                     ? TDMA_PIO_SPI_CLK_TRAIN_RESULT_NO_OVERLAP
                                     : TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_OVERLAP;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE;
    } else if (tx_done && now_ns >= phys->clk_train_return_deadline_ns) {
        phys->clk_train.result =
            TDMA_PIO_SPI_CLK_TRAIN_RESULT_RETURN_TIMEOUT;
        phys->clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
    }

    if (phys->clk_train.state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING) {
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    }
    tdma_pio_spi_phys_clk_train_write_end(phys);
}

/* Maintenance starts at the common stopped boundary. In particular an
 * origin executor can keep writing after its DATA child reports BUSY=0.
 * The incremental legacy loaders operate only on the NORMAL catalog. */
bool tdma_pio_spi_phys_leave_flight_for_maintenance(tdma_pio_spi_phys_t *phys)
{
    if (!tdma_pio_spi_phys_is_flight_persona()) return true;
    return tdma_pio_spi_phys_disarm(phys) &&
        tdma_pio_spi_phys_select_program_persona(phys, TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}

bool tdma_pio_spi_phys_clk_train_terminal_core1(
    const tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) return false;
    const uint32_t begin = __atomic_load_n(&phys->clk_train_guard,
                                           __ATOMIC_ACQUIRE);
    if ((begin & 1u) != 0u) return false;
    const uint32_t state = phys->clk_train.state;
    if (state != TDMA_PIO_SPI_CLK_TRAIN_IDLE &&
        state != TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE &&
        state != TDMA_PIO_SPI_CLK_TRAIN_ERROR) return false;
    if (s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE &&
        (BOARD_TDMA_SPI_PIO->ctrl &
         ((1u << BOARD_TDMA_SPI_MASTER_SM) | (1u << BOARD_TDMA_SPI_SLAVE_SM))) != 0u) {
        return false;
    }
    return begin == __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
}

bool tdma_pio_spi_phys_get_clk_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_clk_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t guard_begin =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if ((guard_begin & 1u) != 0u) {
            continue;
        }
        *snapshot = phys->clk_train;
        const uint32_t guard_end =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

