from tools.tdma_ring_monitor.flight_bitmap_validate import (
    FIFO_FIELDS,
    PROCESS_FIELDS,
    REFMEM_FIELDS,
    parse_snapshot,
    validate_board,
)


def _snapshot() -> dict[str, dict[str, int]]:
    process = dict.fromkeys(PROCESS_FIELDS, 0)
    process.update({
        "version": 2,
        "configured": 1,
        "active": 1,
        "local_slot": 1,
        "payload_size": 256,
        "local_segment_count": 1,
        "receive_version": 1,
        "receive_configured": 1,
        "receive_state": 1,
        "receive_expected_segment_mask": 1,
        "receive_accepted_segment_mask": 1,
        "receive_expected_wkc": 1,
        "receive_accepted_wkc": 1,
    })
    fifo = dict.fromkeys(FIFO_FIELDS, 0)
    fifo["version"] = 2
    refmem = dict.fromkeys(REFMEM_FIELDS, 0)
    refmem.update({
        "enabled": 1,
        "local_slot": 1,
        "node_count": 2,
        "active_mask": 3,
        "payload_size": 256,
        "mailbox_size": 32,
    })
    return {"process": process, "fifo": fifo, "refmem": refmem}


def test_parse_process_snapshot_exact_field_count() -> None:
    raw = ",".join(str(index) for index in range(len(PROCESS_FIELDS)))
    parsed = parse_snapshot(raw, PROCESS_FIELDS, "board-a")
    assert parsed["version"] == 0
    assert parsed["receive_stale_age_ns"] == (
        PROCESS_FIELDS.index("receive_stale_age_ns"))
    assert parsed["receive_last_rejected_timestamp_ns"] == (
        PROCESS_FIELDS.index("receive_last_rejected_timestamp_ns"))
    assert parsed["resident_stale_cycle_count"] == (
        len(PROCESS_FIELDS) - 1)


def test_validate_board_accepts_complete_bitmap_pipeline() -> None:
    before = _snapshot()
    after = _snapshot()
    after["process"].update({
        "map_apply_count": 10,
        "rx_bitmap_scan_count": 20,
        "rx_bitmap_hit_count": 9,
        "rx_bitmap_duplicate_count": 11,
        "receive_accepted_count": 10,
    })
    after["refmem"].update({
        "tx_publish_count": 10,
        "rx_accept_count": 9,
    })
    assert validate_board(before, after) == []


def test_validate_board_rejects_scan_without_delivery() -> None:
    before = _snapshot()
    after = _snapshot()
    after["process"].update({
        "map_apply_count": 10,
        "rx_bitmap_scan_count": 20,
        "receive_accepted_count": 10,
    })
    after["refmem"]["tx_publish_count"] = 10
    errors = validate_board(before, after)
    assert "no bitmap candidate delivered" in errors
    assert "core0 accepted no remote mailbox" in errors


def test_validate_board_rejects_drop_and_bad_mailbox_growth() -> None:
    before = _snapshot()
    after = _snapshot()
    after["process"].update({
        "map_apply_count": 10,
        "rx_bitmap_scan_count": 20,
        "rx_bitmap_hit_count": 8,
        "receive_accepted_count": 8,
    })
    after["fifo"]["rx_mirror_drop_count"] = 1
    after["refmem"].update({
        "tx_publish_count": 10,
        "rx_accept_count": 8,
        "rx_bad_mailbox_count": 1,
    })
    errors = validate_board(before, after)
    assert "RX FIFO mirror drop count grew" in errors
    assert "bad mailbox count grew" in errors


def test_validate_reference_accepts_classify_without_flight_apply() -> None:
    before = _snapshot()
    after = _snapshot()
    before["process"]["local_slot"] = 0
    before["refmem"].update({"local_slot": 0, "reference_slot": 0})
    after["process"].update({
        "local_slot": 0,
        "map_apply_count": 0,
        "rx_bitmap_scan_count": 20,
        "rx_bitmap_hit_count": 8,
        "receive_accepted_count": 8,
    })
    after["refmem"].update({
        "local_slot": 0,
        "reference_slot": 0,
        "tx_publish_count": 10,
        "rx_accept_count": 8,
    })
    assert validate_board(before, after) == []
