from argparse import Namespace

import pytest

from tools.calibration_ring_validate import calibration_marker_loopback as loop


def fixture_capture(fault=False):
    waveform, vector = loop.marker_raw_waveform(codebook_id=1, epoch=7, source_node=0)
    samples = []
    for index in range(1024):
        byte = 0
        for pin, delay in zip(loop.PINS, (32, 45, 47, 60)):
            value = 1 if fault or index < delay else waveform[index - delay]
            byte |= value << (pin - 24)
        samples.append(byte)
    words = [sum(samples[index + byte] << (8 * byte) for byte in range(4))
             for index in range(0, len(samples), 4)]
    status = dict.fromkeys(loop.FIELDS, 0)
    status.update(version=1, state=3 if fault else 2, scope=1, flags=119 if fault else 127,
                  reject_reason=1 if fault else 0, epoch=7, codebook=1,
                  sample_period_ns=4, marker_samples=vector.raw_samples,
                  tx_words=vector.raw_words + 10, tx_drained=1,
                  requested_words=256, produced_words=256, edge_mask=0 if fault else 15)
    for edge, index in enumerate((42, 55, 57, 70), 1):
        status[f"t{edge}_lo"] = 0 if fault else index * 4
        status[f"t{edge}_ns"] = status[f"t{edge}_lo"]
    return status, words


def evaluate(status, words, fault=False):
    return loop.evaluate(status, words, epoch=7, codebook=1, requested_words=256,
                         fault_idle_high=fault)


def test_real_four_edges_and_marker_prefix():
    status, words = fixture_capture()
    result = evaluate(status, words)
    assert result["passed"]
    assert result["forward_path_ns"] == result["return_path_ns"] == 52
    assert result["responder_residence_ns"] == 8
    assert result["origin_rtt_ns"] == 112
    assert result["tx_prefix_mismatches"] == 0
    assert not result["production_training_certified"]
    assert not result["full_marker_correlation_measured"]


@pytest.mark.parametrize("field,value", [
    ("version", 3), ("scope", 0), ("epoch", 6), ("codebook", 0),
    ("produced_words", 255), ("requested_words", 255), ("tx_drained", 0),
    ("tx_dma_remaining", 1), ("flags", 63), ("edge_mask", 9),
    ("state", 3), ("reject_reason", 1), ("marker_samples", 0), ("t2_ns", 168),
    ("tx_words", 1),
])
def test_rejects_stale_or_incomplete_snapshot(field, value):
    status, words = fixture_capture()
    status[field] = value
    assert not evaluate(status, words)["passed"]


def test_no_assumed_pre_edge():
    status, words = fixture_capture()
    words[0] &= ~4
    assert "missing_pre_edge_high" in evaluate(status, words)["failures"]


def test_detects_raw_timestamp_mismatch():
    status, words = fixture_capture()
    status["t4_ns"] += 4
    assert "t4_raw_mismatch" in evaluate(status, words)["failures"]


def test_detects_non_marker_stimulus_after_first_edges():
    status, words = fixture_capture()
    words[30] ^= 4
    assert "tx_marker_prefix" in evaluate(status, words)["failures"]


def test_idle_high_negative_requires_real_absence_of_edges():
    status, words = fixture_capture(True)
    assert evaluate(status, words, True)["passed"]
    words[30] ^= 4
    assert not evaluate(status, words, True)["passed"]


@pytest.mark.parametrize("field,value", [("reject_reason", 2), ("reject_reason", 4),
                                        ("flags", 63), ("tx_drained", 0)])
def test_idle_high_cannot_pass_on_unrelated_fault(field, value):
    status, words = fixture_capture(True)
    status[field] = value
    assert not evaluate(status, words, True)["passed"]


def test_data_chunks_bound_to_epoch_offset_count_and_u32():
    assert loop.parse_data("7,256,32,2,0,4294967295", epoch=7, total=256,
                           offset=32, count=2) == [0, 0xFFFFFFFF]
    for raw in ("6,256,32,2,0,1", "7,256,0,2,0,1", "7,256,32,1,0",
                "7,256,32,2,0,-1", "7,256,32,2,0,4294967296"):
        with pytest.raises(ValueError):
            loop.parse_data(raw, epoch=7, total=256, offset=32, count=2)


def test_status_strict_width_and_u64():
    status, _ = fixture_capture()
    status["t1_hi"] = 1
    parsed = loop.parse_status(",".join(str(status[field]) for field in loop.FIELDS))
    assert parsed["t1_ns"] == (1 << 32) + 168
    with pytest.raises(ValueError):
        loop.parse_status("1,2,3")


def test_trial_download_and_cleanup():
    status, words = fixture_capture()
    idle = dict.fromkeys(loop.FIELDS, 0)
    idle["version"] = 1
    active = False
    calls = []

    def exchange(command):
        nonlocal active
        calls.append(command)
        if command.endswith(":STOP"):
            active = False
            return "OK"
        if ":STARt " in command:
            active = True
            return "1,7,256,0"
        if ":DATA? " in command:
            epoch, offset, count = map(int, command.split()[1].split(","))
            return ",".join(map(str, [epoch, 256, offset, count] + words[offset:offset + count]))
        source = status if active else idle
        return ",".join(str(source[field]) for field in loop.FIELDS)

    result = loop.run_trial(exchange, Namespace(capture_timeout=1, capture_words=256,
                                                codebook=1, fault_idle_high=False), 7)
    assert result["passed"]
    assert result["capture"]["words"] == words
    assert result["cleanup"]["state"] == 0
    assert sum(command.endswith(":STOP") for command in calls) == 2


def test_trial_stale_start_is_failed_but_stopped():
    calls = []

    def exchange(command):
        calls.append(command)
        if command.endswith(":STOP"):
            return "OK"
        if ":STARt " in command:
            return "1,6,256,0"
        return ",".join("1" if field == "version" else "0" for field in loop.FIELDS)

    result = loop.run_trial(exchange, Namespace(capture_timeout=1, capture_words=256,
                                                codebook=1, fault_idle_high=False), 7)
    assert not result["passed"]
    assert "START identity mismatch" in result["error"]
    assert result["cleanup"]["state"] == 0


def full_capture(codebook=1):
    waveform, vector = loop.marker_raw_waveform(codebook_id=codebook, epoch=7, source_node=0)
    delays = (256, 269, 275, 288)
    samples = [sum((waveform[index - delay] if 0 <= index - delay < len(waveform)
                    else 1) << (pin - 24) for pin, delay in zip(loop.PINS, delays))
               for index in range(8192)]
    status, _ = fixture_capture()
    status.update(version=2, capture_generation=42, codebook=codebook,
                  marker_samples=vector.raw_samples, tx_words=vector.raw_words + 10,
                  requested_words=2048, produced_words=2048)
    for edge, delay in enumerate(delays, 1):
        status[f"t{edge}_lo"] = status[f"t{edge}_ns"] = (delay + vector.half_chip_samples) * 4
    return status, samples, vector


def pack_samples(samples):
    return [sum(samples[index + byte] << (8 * byte) for byte in range(4))
            for index in range(0, len(samples), 4)]


@pytest.mark.parametrize("codebook", range(4))
def test_complete_four_channel_marker(codebook):
    status, samples, _ = full_capture(codebook)
    result = loop.evaluate(status, pack_samples(samples), epoch=7, codebook=codebook,
                           requested_words=2048, require_full_marker=True)
    assert result["passed"]
    assert result["full_marker_correlation_measured"]
    assert result["full_marker"]["synthetic_prefix_samples"] == 0
    assert all(channel["logical_bit_count"] == 321 and channel["best_distance"] == 0
               for channel in result["full_marker"]["channels"])
    assert not result["production_training_certified"]


@pytest.mark.parametrize("pin", loop.PINS)
@pytest.mark.parametrize("bit", [1, 40, 90, 200, 320])
def test_full_marker_rejects_corrupt_header_payload_and_eof(pin, bit):
    status, samples, vector = full_capture()
    delay = dict(zip(loop.PINS, (256, 269, 275, 288)))[pin]
    start = delay + bit * vector.half_chip_samples * 2
    for index in range(start, start + vector.half_chip_samples * 2):
        samples[index] ^= 1 << (pin - 24)
    result = loop.evaluate_full_marker(samples, [status[f"t{x}_ns"] for x in range(1, 5)],
                                       epoch=7, codebook=1)
    assert not result["passed"]


def test_complete_marker_rejects_truncation_and_wrong_epoch():
    status, samples, _ = full_capture()
    times = [status[f"t{x}_ns"] for x in range(1, 5)]
    assert not loop.evaluate_full_marker(samples[:1024], times, epoch=7, codebook=1)["passed"]
    assert not loop.evaluate_full_marker(samples, times, epoch=8, codebook=1)["passed"]


@pytest.mark.parametrize("change_generation", [False, True])
def test_full_trial_generation_bound_chunks_and_final_snapshot(change_generation):
    status, samples, _ = full_capture()
    words = pack_samples(samples)
    idle = dict.fromkeys(loop.V2_FIELDS, 0)
    idle["version"] = 2
    active = False
    downloads = []

    def exchange(command):
        nonlocal active
        if command.endswith(":STOP"):
            active = False
            return "OK"
        if ":STARt " in command:
            active = True
            return "1,7,2048,0"
        if ":DATA? " in command:
            epoch, offset, count, generation = map(int, command.split()[1].split(","))
            assert generation == 42
            downloads.append(offset)
            return ",".join(map(str, [epoch, 2048, offset, count] + words[offset:offset + count]))
        source = dict(status if active else idle)
        if active and downloads and change_generation:
            source["capture_generation"] += 1
        return ",".join(str(source[field]) for field in loop.V2_FIELDS)

    result = loop.run_trial(exchange, Namespace(capture_timeout=1, capture_words=2048,
                                                codebook=1, fault_idle_high=False,
                                                full_marker=True), 7)
    assert result["passed"] is not change_generation
    assert downloads == list(range(0, 2048, 32))
    assert result["cleanup"]["state"] == 0
    if change_generation:
        assert "snapshot changed" in result["error"]
