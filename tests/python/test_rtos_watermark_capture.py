from __future__ import annotations

import types

from tools.rtos_watermark_capture.rtos_watermark_capture import (
    parse_board,
    parse_status_response,
    summarize_board,
)


def _args() -> types.SimpleNamespace:
    return types.SimpleNamespace(
        min_heap_free_bytes=24 * 1024,
        min_stack_free_percent=25.0,
        min_stack_free_bytes=512,
        ota_free_percent=60.0,
    )


def test_parse_board_requires_name_and_port() -> None:
    assert parse_board("no1=COM3") == ("NO1", "COM3")


def test_parse_status_response_and_task_fields() -> None:
    response = (
        '22912,22000,2,"ota",1536,1507,29,3,'
        '"ui",2048,1700,348,1'
    )
    parsed = parse_status_response(response)
    assert parsed["heap_free_bytes"] == 22912
    assert parsed["heap_min_free_bytes"] == 22000
    assert parsed["task_count"] == 2
    assert parsed["tasks"][0]["name"] == "ota"
    assert parsed["tasks"][1]["used_words"] == 348


def test_summary_marks_heap_and_ota_as_not_ready_for_reduction() -> None:
    args = _args()
    sample = {
        "heap_free_bytes": 22912,
        "heap_min_free_bytes": 22912,
        "tasks": [
            {"name": "ota", "stack_words": 1536,
             "stack_free_words": 29, "used_words": 1507, "priority": 3},
            {"name": "ui", "stack_words": 2048,
             "stack_free_words": 1700, "used_words": 348, "priority": 1},
        ],
    }
    result = summarize_board([sample], args)
    assert result["heap_min_free_bytes_min"] == 22912
    assert result["gate"]["heap_pass"] is False
    assert result["tasks"]["ota"]["ota_reduction_candidate"] is False
    assert result["tasks"]["ui"]["watermark_pass"] is True


def test_summary_uses_worst_watermark_across_samples() -> None:
    args = _args()
    def sample(free: int) -> dict[str, object]:
        return {
            "heap_free_bytes": 40000,
            "heap_min_free_bytes": 40000,
            "tasks": [{"name": "task", "stack_words": 1000,
                        "stack_free_words": free, "used_words": 1000 - free,
                        "priority": 1}],
        }
    result = summarize_board([sample(800), sample(600)], args)
    assert result["tasks"]["task"]["stack_free_words_min"] == 600
    assert result["tasks"]["task"]["free_percent_min"] == 60.0
