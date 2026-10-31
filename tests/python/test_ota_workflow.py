import json

from tools.ota.ota_stream_send import ota_workflow as wf


def test_step_order_is_stable():
    assert wf.STEPS == [
        "identify", "open", "header", "data", "close",
        "wait_ready", "commit", "reboot", "verify",
    ]


def test_state_round_trip(tmp_path):
    state = wf.State(tmp_path, "image.pkg", "COM5", 4096)
    state.durable = 512
    state.completed = ["identify"]
    state.open_params = {"map_version": 2}
    state.save()

    loaded = wf.State.load(tmp_path, "image.pkg", "COM5", 4096)
    assert loaded.durable == 512
    assert loaded.completed == ["identify"]
    assert loaded.open_params == {"map_version": 2}


def test_record_marks_completed_only_on_success(tmp_path):
    state = wf.State(tmp_path, "image.pkg", "COM5", 4096)
    wf.record(state, 1, "identify", True, {"idn": "NO.1"})
    assert state.completed == ["identify"]

    wf.record(state, 2, "open", False, {"response": "<timeout>"})
    assert state.completed == ["identify"]

    payload = json.loads((tmp_path / "state.json").read_text(encoding="utf-8"))
    assert payload["steps"]["open"]["ok"] is False
    assert (tmp_path / "steps" / "01_identify.json").is_file()
    assert (tmp_path / "steps" / "02_open.json").is_file()


def test_parse_args_defaults():
    args = wf.parse_args(["image.pkg", "--port", "COM5",
                          "--attempt-dir", "out/x", "--steps", "identify"])
    assert args.port == "COM5"
    assert args.steps == "identify"
    assert args.block_size == 4096
    assert args.resume is False
