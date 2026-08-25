import json
from pathlib import Path

from tools.calibration_ring_validate.trn02_profile_gate import aggregate


def _summary(tmp_path: Path, level: int, generation: int, profile: int) -> Path:
    links = []
    for link in range(4):
        links.append({
            "link": link,
            "trial_count": 3,
            "accepted_count": 3,
            "passed": True,
        })
    value = {
        "phase": "TRN-02D_REPEAT_MATRIX",
        "passed": True,
        "matrix": {
            "passed": True,
            "expected_trial_count": 12,
            "accepted_count": 12,
            "identity": {
                "calibration_generation": [generation],
                "topology_generation": [13],
                "topology_crc32": [100],
                "profile_crc32": [profile],
                "schedule_crc32": [200 + level],
                "sample_period_ns": [4],
            },
            "links": links,
        },
    }
    path = tmp_path / f"level{level}.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    return path


def test_profile_gate_requires_residence_evidence(tmp_path: Path) -> None:
    paths = [(level, _summary(tmp_path, level, 80 + level, 10 + level))
             for level in (7, 8, 9)]
    result = aggregate(paths, None)
    assert result["passed"] is False
    assert "forward_residence_evidence_missing" in result["gate_failures"]


def test_profile_gate_rejects_residence_from_wrong_generation(tmp_path: Path) -> None:
    paths = [(level, _summary(tmp_path, level, 80 + level, 10 + level))
             for level in (7, 8, 9)]
    residence = {
        "phase": "TRN-01",
        "records": [{
            "calibration_generation": 61,
            "topology_generation": 13,
            "topology_crc32": 100,
            "profile_crc32": 17,
            "schedule_crc32": 207,
            "forward_residence_ticks": 1,
        }],
    }
    residence_path = tmp_path / "residence.json"
    residence_path.write_text(json.dumps(residence), encoding="utf-8")
    result = aggregate(paths, residence_path)
    assert result["passed"] is False
    assert "residence_calibration_generation_mismatch" in result["gate_failures"]
