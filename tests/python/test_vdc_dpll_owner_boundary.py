"""Source-contract tests for the Core0/Core1 VDC owner boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANAGER = (ROOT / "components" / "vdc_dpll_manager" / "src" /
           "vdc_dpll_manager.c").read_text(encoding="utf-8")
MANAGER_H = (ROOT / "components" / "vdc_dpll_manager" / "inc" /
             "vdc_dpll_manager.h").read_text(encoding="utf-8")
CALIBRATION = (ROOT / "components" / "calibration_manager" / "src" /
               "calibration_manager.c").read_text(encoding="utf-8")
REFMEM = (ROOT / "components" / "distributed_refmem" / "src" /
          "distributed_refmem.c").read_text(encoding="utf-8")
SCPI_SYNC = (ROOT / "middleware" / "scpi_port" / "src" /
             "scpi_sync_commands.c").read_text(encoding="utf-8")
SCPI_CAL = (ROOT / "middleware" / "scpi_port" / "src" /
            "scpi_calibration_commands.c").read_text(encoding="utf-8")
SCPI_SYSTEM = (ROOT / "middleware" / "scpi_port" / "src" /
               "scpi_system_snapshot_commands.c").read_text(encoding="utf-8")


def _function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def _ordered(body: str, *needles: str) -> None:
    positions = [body.index(needle) for needle in needles]
    assert positions == sorted(positions)


def test_init_publishes_an_off_domain_snapshot() -> None:
    body = _function(MANAGER, "bool vdc_dpll_manager_init(void)")
    _ordered(body, "vdc_domain_init(&s_vdc_domain)",
             "vdc_dpll_manager_publish_domain_snapshot_locked()")
    assert "s_vdc_ready_applied_generation = 0u" in body
    assert "s_vdc_stop_applied_generation = 0u" in body


def test_ready_and_sticky_stop_share_one_seqlock_snapshot() -> None:
    setter = _function(MANAGER, "void vdc_dpll_manager_set_vdc_ready(bool ready)")
    reader = _function(MANAGER, "static bool vdc_dpll_manager_read_ready_request(")
    assert setter.count("s_vdc_ready_request.guard") == 2
    assert "s_vdc_ready_request.value = ready ? 1u : 0u" in setter
    assert "s_vdc_ready_request.generation = generation" in setter
    assert "s_vdc_ready_request.stop_generation = generation" in setter
    assert "candidate_value" in reader
    assert "candidate_generation" in reader
    assert "candidate_stop_generation" in reader

    apply = _function(MANAGER, "static bool vdc_dpll_manager_apply_pending_ready(void)")
    _ordered(apply, "if (stop_requested != stop_applied)",
             "if (requested == stop_requested)",
             "if (requested == applied)")


def test_owner_result_is_published_before_mailbox_release() -> None:
    body = _function(MANAGER, "static bool vdc_dpll_manager_apply_owner_intent(void)")
    _ordered(body, "vdc_dpll_manager_publish_domain_snapshot_locked()",
             "s_owner_intent_result_sequence = sequence",
             "s_owner_intent_consumed_sequence")
    assert "*owner_sequence = s_owner_intent.sequence" in MANAGER


def test_mandatory_owner_step_does_not_prepare_new_evidence() -> None:
    step = _function(
        MANAGER, "vdc_dpll_manager_owner_service_step_core1)(void)")
    _ordered(step, "vdc_dpll_manager_apply_pending_ready()",
             "vdc_dpll_manager_age_quality_core1()",
             "vdc_dpll_manager_progress_ring_transaction()",
             "vdc_dpll_manager_apply_owner_intent()")
    assert "vdc_dpll_manager_prepare_ring_evidence" not in step

    full = _function(MANAGER, "sync_dpll_fb_service)(void)")
    _ordered(full, "vdc_dpll_manager_owner_service_step_core1()",
             "vdc_dpll_manager_prepare_ring_evidence()")


def test_control_status_survives_internal_owner_traffic() -> None:
    getter = _function(
        MANAGER, "bool vdc_dpll_manager_get_control_owner_status(")
    assert "s_control_request.guard" in getter
    assert "s_control_result.guard" in getter
    assert "s_owner_intent.guard" not in getter
    assert "s_control_ack_sequence" in getter
    assert "s_control_rejected_sequence" in getter
    complete = _function(
        MANAGER, "bool vdc_dpll_manager_complete_control_owner_status(")
    assert "s_control_ack_sequence" in complete

    owner_q = _function(
        SCPI_SYNC, "scpi_result_t scpi_cmd_sync_vdc_dpll_owner_q(")
    _ordered(owner_q, "owner.requested_sequence", "owner.applied_sequence",
             "owner.opcode", "owner.result", "pending ? TRUE : FALSE")


def test_failed_topology_second_phase_is_rejected_and_rollback_is_internal() -> None:
    service = _function(
        REFMEM, "static void distributed_refmem_tdma_topology_service(void)")
    assert "vdc_dpll_manager_restore_tdma_ring_topology" in service
    forward = service[service.index("bool applied ="):]
    _ordered(forward, "refmem_application_model_set_tdma_ring_topology(",
             "vdc_dpll_manager_reject_control_owner_status(",
             "vdc_dpll_manager_ack_owner_status(",
             "vdc_dpll_manager_complete_control_owner_status(")
    restore = _function(
        MANAGER, "bool vdc_dpll_manager_restore_tdma_ring_topology(")
    assert "false, owner_sequence" in restore


def test_timer1_plans_are_translated_only_at_tdma_transport_boundary() -> None:
    assert "vdc_dpll_manager_translate_tdma_transport_plan" in MANAGER_H
    self_test = _function(
        MANAGER, "bool vdc_dpll_manager_start_observation_self_test(")
    _ordered(self_test, "vdc_tdma_payload_build_frame(",
             "vdc_dpll_manager_translate_tdma_transport_plan(",
             ".scheduled_window_start_ns = transport_plan.window_start_ns")
    assert ".effective_vdc_time_ns = retained.effective_vdc_time_ns" in MANAGER


def test_calibration_commits_only_after_matching_terminal_result_and_ack() -> None:
    service = _function(
        CALIBRATION, "static void calibration_manager_path_activation_service(void)")
    _ordered(service, "owner.requested_sequence != pending.owner_sequence",
             "vdc_dpll_manager_ack_owner_status(pending.owner_sequence)",
             "s_path_active = pending.next_active",
             "vdc_dpll_manager_complete_control_owner_status(")
    assert "owner.result == VDC_DPLL_MANAGER_OWNER_RESULT_APPLIED" in service


def test_topology_and_run_commands_are_frozen_during_async_change() -> None:
    topology = _function(
        REFMEM, "bool distributed_refmem_set_tdma_ring_topology(")
    assert "s_tdma_topology_pending" in topology
    arm = _function(REFMEM, "bool distributed_refmem_tdma_ring_arm(void)")
    train = _function(REFMEM, "bool distributed_refmem_tdma_ring_train(")
    start = _function(REFMEM, "bool distributed_refmem_tdma_ring_start(void)")
    for body in (arm, train, start):
        assert "distributed_refmem_tdma_topology_change_pending()" in body
        assert "vdc_dpll_manager_owner_change_pending()" in body
    calibration = _function(
        MANAGER, "bool vdc_dpll_manager_activate_tdma_calibration(")
    provisional = _function(
        MANAGER, "bool vdc_dpll_manager_activate_tdma_provisional_training(")
    for body in (calibration, provisional):
        assert "vdc_dpll_manager_control_transaction_open()" in body
    owner_pending = _function(
        MANAGER, "bool vdc_dpll_manager_owner_change_pending(void)")
    assert "vdc_dpll_manager_get_control_owner_status(" in owner_pending
    assert "control_pending" in owner_pending


def test_invalid_bias_never_reaches_accumulator() -> None:
    service = _function(CALIBRATION, "void calibration_manager_service(void)")
    _ordered(service, "const bool measurement_valid",
             "if (!measurement_valid)", "calibration_bias_add(")
    rejected = service[service.index("if (!measurement_valid)"):
                       service.index("calibration_bias_add(")]
    assert "return;" in rejected


def test_compact_observer_counts_only_terminal_results_and_phase_keeps_batching() -> None:
    finish = _function(
        MANAGER, "static bool vdc_dpll_manager_finish_compact_observation_request(void)")
    _ordered(finish, "owner.applied_sequence != sequence",
             "vdc_dpll_manager_ack_owner_status(sequence)",
             "s_sync_io_observer_status.accepted_count++")
    observer = _function(
        MANAGER, "static void vdc_dpll_manager_sync_io_observer_service(void)")
    assert "config.phase_only\n        ? config.max_words_per_service : 1u" in observer


def test_self_test_retries_a_busy_hardware_arm() -> None:
    service = _function(
        MANAGER, "static void vdc_dpll_manager_observation_self_test_service(void)")
    busy = service[service.index(
        "if (!sync_io_sma_observer_pulse_schedule_arm_periodic_at_ns("):
        service.index("if (s_phase_tx_scheduled_count != UINT32_MAX)")]
    assert "s_observation_self_test.active = true" in busy
    assert "return;" in busy


def test_scpi_setters_return_exact_token_as_last_field() -> None:
    calibration_activate = _function(
        SCPI_CAL, "scpi_result_t scpi_calibration_activate(")
    calibration_rollback = _function(
        SCPI_CAL, "scpi_result_t scpi_calibration_rollback(")
    ring_local = _function(
        SCPI_SYSTEM, "scpi_result_t scpi_cmd_system_tdma_ring_local(")
    ring_topology = _function(
        SCPI_SYSTEM, "scpi_result_t scpi_cmd_system_tdma_ring_topology(")
    for body in (calibration_activate, calibration_rollback,
                 ring_local, ring_topology):
        assert "SCPI_ResultText(context, \"QUEUED\")" in body
        assert body.rindex("SCPI_ResultUInt32(context, owner_sequence)") < body.rindex(
            "return SCPI_RES_OK")
