static void reset(void)
{
    memset(&s_sequence, 0, sizeof(s_sequence));
    s_sequence.status.plan_count = 3u;
    s_sequence.status.current_index = UINT32_MAX;
    s_sequence.status.completed_index = UINT32_MAX;
    for (uint i = 0u; i < 3u; ++i) s_plan[i * 3u] = (i << 4u) | i;
}

int main(void)
{
    sync_io_sequence_config_t config = {1u, false, 7u, 4u, 20u, 10u};
    assert(config_valid(&config));
    for (uint input = 0u; input <= 4u; ++input) {
        config.input_channel = input;
        assert(config_valid(&config));
    }
    config.input_channel = 5u;
    assert(!config_valid(&config));
    config.input_channel = 1u;
    config.settle_us = SYNC_IO_SEQUENCE_TIME_MAX_US;
    config.pulse_us = SYNC_IO_SEQUENCE_TIME_MAX_US;
    assert(config_valid(&config));
    ++config.pulse_us;
    assert(!config_valid(&config));
    config.pulse_us = 0u;
    assert(!config_valid(&config));
    config.pulse_us = 1u;
    config.output_mask = 15u;
    assert(!config_valid(&config));
    assert(!config_valid(NULL));

    reset();
    for (uint i = 0u; i < 17u; ++i) {
        uint32_t tag = s_plan[(i % 3u) * 3u];
        assert(receive_word(tag));
        assert(s_sequence.status.written == i + 1u);
        assert(s_sequence.status.accepted == i + 1u);
        assert(s_sequence.status.completed == i);
        assert(s_sequence.status.current_index == i % 3u);
        assert(receive_word(~tag));
        assert(s_sequence.status.completed == i + 1u);
        assert(s_sequence.status.completed_index == i % 3u);
    }
    assert(s_sequence.status.written_at_us == 0u);
    assert(s_sequence.status.completion_rise_at_us == 0u);
    assert(s_sequence.status.completed_at_us == 0u);
    reset();
    assert(!receive_word(~s_plan[0]));
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_RECEIPT);
    reset();
    assert(receive_word(s_plan[0]));
    assert(!receive_word(s_plan[3]));
    reset();
    assert(!receive_word(s_plan[6]));

    reset();
    s_sequence.status.accepted = 1u;
    account_input(5u, false);
    assert(s_sequence.status.busy_rejected == 0u);
    assert(s_sequence.status.rejection_counts_pending);
    account_input(5u, true);
    assert(s_sequence.status.busy_rejected == 4u);
    assert(!s_sequence.status.rejection_counts_pending);
    account_input(8u, false);
    assert(s_sequence.status.busy_rejected == 4u);
    s_sequence.pause_started = 8u;
    s_sequence.paused = true;
    account_input(8u, true);
    assert(s_sequence.status.busy_rejected == 7u);
    account_input(10u, false);
    assert(s_sequence.status.notready_rejected == 2u);
    assert(s_sequence.status.busy_rejected == 7u);
    s_sequence.paused_edges = 2u;
    s_sequence.paused = false;
    s_sequence.status.accepted = 2u;
    account_input(11u, true);
    assert(s_sequence.status.notready_rejected == 2u);
    assert(s_sequence.status.busy_rejected == 7u);
    reset();
    s_sequence.status.accepted = 2u;
    account_input(1u, false);
    assert(s_sequence.status.fault == 0u);
    account_input(1u, true);
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_RECEIPT);
    reset();
    account_input(UINT32_MAX - 31u, false);
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_OVERFLOW);
    puts("PIO sequence config, execution receipts and settled counters passed");
    return 0;
}
