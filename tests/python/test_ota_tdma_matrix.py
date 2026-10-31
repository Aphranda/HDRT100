import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.ota.ota_stream_send import tdma_matrix as matrix


def test_default_plan_round_trips_through_json():
    plan = matrix.default_plan("image.bin", "COM5", 1, 512)
    data = json.loads(plan.to_json())
    assert data["version"] == matrix.MATRIX_PLAN_VERSION
    assert data["port"] == "COM5"
    assert data["relay_node"] == 1
    assert [case["name"] for case in data["cases"]] == [
        "clean", "drop-1", "drop-3", "drop-8"]
    assert data["cases"][0]["repeats"] == 3


def test_classify_marks_retry_recovery():
    result = matrix.classify("clean", 0, 1, True, 2)
    assert result.passed and result.recovered
    assert result.reason == "recovered"

    clean = matrix.classify("clean", 0, 1, True, 1)
    assert clean.passed and not clean.recovered
    assert clean.reason == "clean"


def test_classify_marks_failures():
    dropped = matrix.classify("drop-8", 8, 1, False, 3)
    assert not dropped.passed
    assert dropped.reason == "drop-exhausted"

    transport = matrix.classify("clean", 0, 1, False, 1)
    assert transport.reason == "transfer-failed"


def test_run_matrix_injects_drops_before_each_attempt():
    plan = matrix.MatrixPlan(
        image="image.bin", port="COM5", relay_node=1, block_size=512,
        cases=[matrix.MatrixCase("clean", repeats=2),
               matrix.MatrixCase("drop-3", drop_chunks=3)])
    injected = []
    seen = []

    def runner(case):
        seen.append(case.name)
        return True, 1

    def dropper(count):
        injected.append(count)

    results = matrix.run_matrix(plan, runner, dropper)
    assert [r.name for r in results] == ["clean", "clean", "drop-3"]
    assert injected == [3]
    assert seen == ["clean", "clean", "drop-3"]
    assert all(r.passed for r in results)
