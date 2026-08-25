import json
from pathlib import Path

from tools.calibration_ring_validate.trn02_profile_gate import aggregate


def _identity(generation: int, profile: int, schedule: int) -> dict:
    return {
        "calibration_generation": [generation],
        "topology_generation": [13 + generation],
        "topology_crc32": [100],
        "profile_crc32": [profile],
        "schedule_crc32": [schedule],
        "sample_period_ns": [4],
    }


def _summary(tmp_path: Path, level: int, generation: int, profile: int) -> Path:
    links = []
    for link in range(4):
        links.append({
            "link": link,
            "trial_count": 3,
            "accepted_count": 3,
            "offset_span_sample": 0,
            "gate_failures": [],
            "passed": True,
        })
    value = {
        "phase": "TRN-02D_REPEAT_MATRIX",
        "passed": True,
        "node_ids_in_loop_order": ["n0", "n1", "n2", "n3"],
        "repeats": 3,
        "max_offset_span_sample": 1,
        "matrix": {
            "passed": True,
            "expected_trial_count": 12,
            "trial_count": 12,
            "accepted_count": 12,
            "identity": _identity(generation, profile, 200 + level),
            "identity_failures": [],
            "gate_failures": [],
            "links": links,
        },
    }
    path = tmp_path / f"level{level}.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    return path


def _residence(tmp_path: Path, level: int, generation: int,
               profile: int) -> Path:
    identity = _identity(generation, profile, 200 + level)
    identity["tick_resolution_ns"] = identity.pop("sample_period_ns")
    value = {
        "phase": "TRN-01_RESIDENCE_MATRIX",
        "passed": True,
        "node_ids_in_loop_order": ["n0", "n1", "n2", "n3"],
        "trial_count": 4,
        "matrix": {
            "passed": True,
            "identity": identity,
            "failures": [],
            "links": [{
                "link_index": link,
                "source_node": link,
                "destination_node": (link + 1) % 4,
                "forward_residence_ticks": [1, 1, 1],
                "forward_residence_span_ticks": 0,
                "selected_forward_residence_ticks": 1,
                "repeat_count": 3,
                "passed": True,
            } for link in range(4)],
            "loops": [{"node": node, "loop_delay_ticks": [10 + node]}
                      for node in range(4)],
        },
    }
    path = tmp_path / f"level{level}_residence.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    return path


def _evidence(tmp_path: Path) -> tuple[list[tuple[int, Path]],
                                       list[tuple[int, Path]]]:
    profiles = []
    residences = []
    for level in (7, 8, 9):
        generation = 80 + level
        profile = 10 + level
        profiles.append((level, _summary(
            tmp_path, level, generation, profile)))
        residences.append((level, _residence(
            tmp_path, level, generation, profile)))
    return profiles, residences


def test_profile_gate_accepts_three_paired_profiles(tmp_path: Path) -> None:
    profiles, residences = _evidence(tmp_path)
    result = aggregate(profiles, residences)
    assert result["passed"] is True
    assert result["profile_count"] == 3


def test_profile_gate_requires_residence_for_every_level(tmp_path: Path) -> None:
    profiles, residences = _evidence(tmp_path)
    result = aggregate(profiles, residences[:-1])
    assert result["passed"] is False
    assert "profile_residence_level_mismatch" in result["gate_failures"]


def test_profile_gate_rejects_residence_from_wrong_generation(tmp_path: Path) -> None:
    profiles, residences = _evidence(tmp_path)
    residence = json.loads(residences[0][1].read_text(encoding="utf-8"))
    residence["matrix"]["identity"]["calibration_generation"] = [61]
    residences[0][1].write_text(json.dumps(residence), encoding="utf-8")
    result = aggregate(profiles, residences)
    assert result["passed"] is False
    assert "level7:residence_calibration_generation_mismatch" in \
        result["gate_failures"]
