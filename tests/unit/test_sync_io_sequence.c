static void reset(void)
{
    memset(&s_sequence, 0, sizeof(s_sequence));
    s_sequence.status.plan_count = 3u;
    s_sequence.status.current_index = 0u;
    s_sequence.status.completed_index = UINT32_MAX;
    for (uint i = 0u; i < 3u; ++i) {
        const uint32_t logical = logical_index_for_transfer(i, 3u);
        s_plan[i * SYNC_IO_SEQUENCE_PLAN_WORDS] = (logical << 4u) | logical;
    }
}

int main(void)
{
    sync_io_sequence_config_t config = {
        1u, false, 7u, 8u, SYNC_IO_SEQUENCE_STATUS_PULSE, 20u, 10u,
        false, 0u, 0u, 0u, false, false, 0u};
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
    config.sequence_output_mask = 15u;
    assert(!config_valid(&config));
    config.sequence_output_mask = 7u;
    config.status_mode = SYNC_IO_SEQUENCE_STATUS_LEVEL;
    config.pulse_us = 0u;
    assert(config_valid(&config));
    config.pulse_us = 1u;
    assert(!config_valid(&config));
    config.status_mode = SYNC_IO_SEQUENCE_STATUS_NONE;
    config.status_output_mask = 0u;
    config.pulse_us = 0u;
    assert(config_valid(&config));
    config.status_output_mask = 8u;
    assert(!config_valid(&config));
    config.status_output_mask = 0u;
    config.pulse_us = 1u;
    assert(!config_valid(&config));
    config.pulse_us = 0u;
    config.status_mode = SYNC_IO_SEQUENCE_STATUS_LEVEL;
    assert(!config_valid(&config));
    assert(!config_valid(NULL));
    config = (sync_io_sequence_config_t){.sequence_output_mask = 7u,
        .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE, .step_limit_enabled = true};
    assert(config_valid(&config));
    config.max_steps = SYNC_IO_SEQUENCE_PLAN_MAX;
    assert(config_valid(&config));
    config.max_steps = 79999u; /* 10000 rounds of the eight-state SP8T */
    assert(config_valid(&config));
    config = (sync_io_sequence_config_t){
        .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
        .gateway_enabled = true,
        .gateway_input_channel = 1u, .gateway_output_mask = 8u, .gateway_pulse_us = 10u};
    for (uint channel = 1u; channel <= 4u; ++channel) {
        config.gateway_input_channel = channel;
        assert(config_valid(&config));
        config.gateway_falling = true;
        assert(config_valid(&config));
    }
    config.gateway_input_channel = 5u;
    assert(!config_valid(&config));
    config.gateway_input_channel = 1u;
    config.input_channel = 1u;
    assert(!config_valid(&config));
    config.input_channel = 0u;
    config.gateway_output_mask = 3u;
    assert(!config_valid(&config));
    config.gateway_output_mask = 1u;
    assert(!config_valid(&config));
    config.gateway_output_mask = 16u;
    assert(!config_valid(&config));
    config.gateway_output_mask = 8u;
    config.gateway_pulse_us = 0u;
    assert(!config_valid(&config));
    config.gateway_pulse_us = SYNC_IO_SEQUENCE_TIME_MAX_US + 1u;
    assert(!config_valid(&config));
    config.gateway_pulse_us = 10u;
    config.gateway_input_channel = 0u;
    assert(config_valid(&config));
    config.gateway_enabled = false;
    assert(!config_valid(&config));
    config.gateway_output_mask = config.gateway_pulse_us = 0u;
    config.gateway_falling = false;
    assert(config_valid(&config));
    assert(logical_index_for_transfer(0u, 3u) == 1u);
    assert(logical_index_for_transfer(1u, 3u) == 2u);
    assert(logical_index_for_transfer(2u, 3u) == 0u);
    assert(logical_index_for_transfer(UINT32_MAX, 1u) == 0u);

    reset();
    for (uint i = 0u; i < 17u; ++i) {
        uint32_t tag = s_plan[(i % 3u) * SYNC_IO_SEQUENCE_PLAN_WORDS];
        assert(receive_word(tag));
        assert(s_sequence.status.written == i + 1u);
        assert(s_sequence.status.accepted == i + 1u);
        assert(s_sequence.status.completed == i);
        assert(s_sequence.status.current_index == (i + 1u) % 3u);
        assert(receive_word(~tag));
        assert(s_sequence.status.completed == i + 1u);
        assert(s_sequence.status.completed_index == (i + 1u) % 3u);
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
