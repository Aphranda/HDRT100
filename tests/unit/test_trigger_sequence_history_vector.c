/* White-box tests exercise publication interleavings without test hooks in
 * firmware. The normal LINK suite separately covers production FSM writers. */
#define main sequence_link_fixture_main
#include "test_trigger_sequence_link.c"
#undef main
#include "../../components/sync_trigger/src/trigger_sequence_link.c"
#include <pthread.h>

static uint32_t writer_done, reader_started, reader_checks;

static history_entry_t example(uint32_t ordinal)
{
    history_entry_t entry = {.ordinal = ordinal, .position = ordinal,
        .exchange_id = ordinal, .observed_pulses = ordinal * 1000u,
        .timing_flags = 63u, .outcome_flags = 7u};
    for (unsigned i = 0; i < TRIGGER_SEQUENCE_LINK_TIME_COUNT; ++i)
        entry.timing_ticks[i] = ((uint64_t)ordinal << 32) | (ordinal + i);
    return entry;
}

static void append_example(uint32_t ordinal)
{
    s_link.history_total = ordinal;
    s_link.history_retained = ordinal < TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY ?
        ordinal : TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY;
    const history_entry_t entry = example(ordinal);
    history_store_entry(&entry);
}

static void verify_entry(const trigger_sequence_link_history_t *record)
{
    assert(record->exchange_id == record->ordinal);
    assert(record->position == record->ordinal);
    assert(record->observed_pulses == record->ordinal * 1000u);
    for (unsigned i = 0; i < TRIGGER_SEQUENCE_LINK_TIME_COUNT; ++i)
        assert(record->timing_ticks[i] == ((uint64_t)record->ordinal << 32 |
            (record->ordinal + i)));
}

static void retention(void)
{
    s_link.run_id = 21u; s_link.generation = 31u; s_link.binding_epoch = 41u;
    s_link.config.counter_threshold = 1000u;
    history_reset();
    trigger_sequence_link_history_status_t status;
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.total == 0u && status.retained == 0u && status.run_id == 21u);
    for (uint32_t ordinal = 1; ordinal <= 240u; ++ordinal) append_example(ordinal);
    for (uint32_t ordinal = 1; ordinal <= 240u; ++ordinal) {
        trigger_sequence_link_history_t record;
        assert(trigger_sequence_link_get_history(ordinal, &record));
        verify_entry(&record);
        assert(record.generation == 31u && record.binding_epoch == 41u);
    }
    for (uint32_t ordinal = 241; ordinal <= 600u; ++ordinal) append_example(ordinal);
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.version == TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION);
    assert(status.clock_hz == BOARD_SYS_CLOCK_HZ && status.capacity == 256u);
    assert(status.total == 600u && status.retained == 256u && status.overwritten == 344u);
    trigger_sequence_link_history_t record, untouched;
    memset(&untouched, 0xa5, sizeof(untouched)); record = untouched;
    assert(!trigger_sequence_link_get_history(344u, &record));
    assert(memcmp(&record, &untouched, sizeof(record)) == 0);
    assert(trigger_sequence_link_get_history(345u, &record)); verify_entry(&record);
    assert(trigger_sequence_link_get_history(600u, &record)); verify_entry(&record);
    record = untouched;
    assert(!trigger_sequence_link_get_history(601u, &record));
    assert(memcmp(&record, &untouched, sizeof(record)) == 0);
    s_link.run_id = 22u; s_link.generation = 32u; history_reset();
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.run_id == 22u && status.generation == 32u && status.total == 0u);
    assert(!trigger_sequence_link_get_history(600u, &record));
    append_example(1u);
    assert(trigger_sequence_link_get_history(1u, &record) && record.run_id == 22u);
    /* Publication sequence wrap, distinct from ordinal overflow. */
    __atomic_store_n(&s_history_vector.sequence, UINT32_MAX - 1u, __ATOMIC_RELAXED);
    append_example(2u);
    assert(__atomic_load_n(&s_history_vector.sequence, __ATOMIC_RELAXED) == 0u);
    assert(trigger_sequence_link_get_history(2u, &record)); verify_entry(&record);
}

static void busy(void)
{
    trigger_sequence_link_history_status_t status, before;
    trigger_sequence_link_history_t record, original;
    memset(&before, 0xa5, sizeof(before)); status = before;
    memset(&original, 0x5a, sizeof(original)); record = original;
    const uint32_t locks = critical_entries, reads = owner_status_reads;
    history_write_begin();
    for (unsigned i = 0; i < 10000u; ++i) {
        assert(!trigger_sequence_link_get_history_status(&status));
        assert(!trigger_sequence_link_get_history(1u, &record));
        assert(memcmp(&status, &before, sizeof(status)) == 0);
        assert(memcmp(&record, &original, sizeof(record)) == 0);
    }
    history_write_end();
    assert(trigger_sequence_link_get_history_status(&status));
    assert(critical_entries == locks && owner_status_reads == reads);
}

static void *reader(void *argument)
{
    (void)argument;
    __atomic_store_n(&reader_started, 1u, __ATOMIC_RELEASE);
    do {
        trigger_sequence_link_history_status_t status;
        if (trigger_sequence_link_get_history_status(&status)) {
            assert(status.total == status.retained + status.overwritten);
            assert(status.retained <= status.capacity && status.capacity == 256u);
            if (status.total) {
                trigger_sequence_link_history_t record;
                if (trigger_sequence_link_get_history(status.total, &record)) verify_entry(&record);
            }
            ++reader_checks;
        }
    } while (!__atomic_load_n(&writer_done, __ATOMIC_ACQUIRE));
    return NULL;
}

static void concurrent(void)
{
    history_reset();
    pthread_t thread;
    assert(pthread_create(&thread, NULL, reader, NULL) == 0);
    while (!__atomic_load_n(&reader_started, __ATOMIC_ACQUIRE)) {}
    const uint32_t locks = critical_entries, reads = owner_status_reads;
    for (uint32_t ordinal = 1; ordinal <= 100000u; ++ordinal) append_example(ordinal);
    __atomic_store_n(&writer_done, 1u, __ATOMIC_RELEASE);
    assert(pthread_join(thread, NULL) == 0);
    assert(reader_checks && s_link.history_total == 100000u);
    assert(critical_entries == locks && owner_status_reads == reads);
}

static void lifecycle(void)
{
    fixture(); counter_start(1u, 8u);
    owner.counter_events = 10u; counter_request();
    trigger_sequence_link_history_t record;
    assert(trigger_sequence_link_get_history(1u, &record));
    assert(record.outcome_flags == (TRIGGER_SEQUENCE_LINK_HISTORY_REQUESTED |
        TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED));
    const trigger_sequence_link_history_t partial = record;
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE; trigger_sequence_link_service();
    ring.enabled = ring.adapter_started = 0u;
    trigger_sequence_link_config_t next = s_link.config;
    next.counter_threshold = 20u;
    assert(trigger_sequence_link_configure(&next));
    assert(trigger_sequence_link_get_history(1u, &record));
    assert(record.run_id == partial.run_id && record.binding_epoch == partial.binding_epoch);
    assert(record.outcome_flags == partial.outcome_flags && record.timing_flags == partial.timing_flags);
    ring.enabled = ring.adapter_started = 1u;
    assert(guard()); ++owner.run_id; ++owner.generation;
    owner.counter_events = 0u; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service();
    trigger_sequence_link_history_status_t status;
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.total == 0u && status.run_id == owner.run_id && status.threshold == 20u);
    assert(!trigger_sequence_link_get_history(1u, &record));
}

static void overflow_and_fault(void)
{
    fixture(); counter_start(1u, 8u);
    owner.counter_events = 10u; counter_request();
    trigger_sequence_link_history_t record;
    assert(trigger_sequence_link_get_history(1u, &record));
    const uint32_t original_flags = record.timing_flags;
    owner.state = TRIGGER_SEQUENCE_SERVICE_FAULT;
    owner.backend_fault = SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY;
    trigger_sequence_link_service();
    assert(trigger_sequence_link_get_history(1u, &record));
    assert(record.timing_flags == original_flags && !(record.outcome_flags & 4u));
    ++owner.run_id; ++owner.generation;
    trigger_sequence_link_service();
    trigger_sequence_link_history_status_t status;
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.run_id == owner.run_id && status.total == 0u && status.retained == 0u);
    s_link.history_total = UINT32_MAX;
    s_link.history_retained = TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY;
    history_write_begin(); history_store_status(); history_write_end();
    assert(!record_position_step(&owner, 0u, 1u));
    assert(s_link.error == LINK_COUNTER_OVERFLOW);
    assert(trigger_sequence_link_get_history_status(&status));
    assert(status.total == UINT32_MAX && status.overwritten == UINT32_MAX - status.retained);
}

static void config_guards(void)
{
    const trigger_sequence_link_status_t before = s_link;
    s_guard = 1u;
    trigger_sequence_link_config_diagnostic_t diagnostic = trigger_sequence_link_configure_checked(&config);
    assert(diagnostic.result == TRIGGER_SEQUENCE_LINK_CONFIG_WRITER_BUSY);
    assert(s_guard == 1u && !configuration_gate && !s_configuring);
    assert(!memcmp(&s_link, &before, sizeof(before)));
    s_guard = 0u;
    s_link.binding_epoch = UINT32_MAX;
    diagnostic = trigger_sequence_link_configure_checked(&config);
    assert(diagnostic.result == TRIGGER_SEQUENCE_LINK_CONFIG_EPOCH_EXHAUSTED);
    assert(!s_guard && !configuration_gate && !s_configuring);
    assert(s_link.binding_epoch == UINT32_MAX && !transport_enabled);
    assert(!diagnostic.tdma_result && !diagnostic.gateway_result && !diagnostic.rollback_result);
}

static void off_service_quiescent(void)
{
    fixture();
    const trigger_sequence_link_config_t off = {0};
    assert(trigger_sequence_link_configure(&off));
    /* Pending rejected transport observations are still drained once. */
    __atomic_store_n(&s_transport_rejected, 3u, __ATOMIC_RELEASE);
    assert(!trigger_sequence_link_service());
    assert(s_published.rejected == 3u && s_transport_rejected == 0u);
    /* A real writer visit republishes this changed private counter. */
    s_link.rejected = 7u;
    for (unsigned i = 0; i < 1000u; ++i) assert(!trigger_sequence_link_service());
    assert(s_published.rejected == 3u);
    assert(trigger_sequence_link_configure(&config));
    trigger_sequence_link_service();
    s_link.rejected = 11u;
    trigger_sequence_link_service();
    assert(s_published.rejected == 11u); /* Enabled IDLE still services STOP. */
    __atomic_store_n(&s_transport_rejected, 5u, __ATOMIC_RELEASE);
    transport_accept = false;
    assert(!trigger_sequence_link_configure(&off));
    assert(s_transport_rejected == 5u); /* Failure must preserve diagnostics. */
    transport_accept = true;
    assert(trigger_sequence_link_configure(&off));
    assert(s_transport_rejected == 0u);
    assert(trigger_sequence_link_configure(&config));
    trigger_sequence_link_service();
    assert(s_published.rejected == 0u); /* No old-binding increments leak. */
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "retention")) retention();
    else if (!strcmp(argv[1], "busy")) busy();
    else if (!strcmp(argv[1], "concurrent")) concurrent();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "overflow_fault")) overflow_and_fault();
    else if (!strcmp(argv[1], "config_guards")) config_guards();
    else if (!strcmp(argv[1], "off_service_quiescent")) off_service_quiescent();
    else assert(false);
    puts("history vector passed");
    return 0;
}
