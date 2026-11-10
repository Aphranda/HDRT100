#!/usr/bin/env python3
"""Host tests for the single-port REFMEM trigger debug demo."""

from __future__ import annotations

import socket
import tempfile
import threading
import time
import zlib
from pathlib import Path

import pytest

from tools.domains.trigger.trigger_debug_demo import catalog
from tools.domains.trigger.trigger_debug_demo import device
from tools.domains.trigger.trigger_debug_demo import ota


def test_catalog_modules_and_function_ids() -> None:
    assert [module.key for module in catalog.MODULES] == [
        "overview", "run", "trigger_config", "angle", "switch", "sequence",
        "readback", "ack", "control", "diagnostics", "raw"]
    keys = [function.key for function in catalog.FUNCTIONS]
    assert len(keys) == len(set(keys))
    assert len(catalog.QUERIES) >= 20
    for function in catalog.FUNCTIONS:
        if function.ctl_name is None:
            continue
        entry = device.CATALOG_BY_NAME[function.ctl_name]
        assert entry[0] == function.ctl_id
        params = catalog.ctl_params(function, catalog.example_values(function.fields))
        assert len(params) == entry[4], function.key


def test_float_and_plan_encoding() -> None:
    sweep = catalog.FUNCTION_BY_KEY["angle.sweep"]
    params = catalog.ctl_params(sweep, {"start": "0", "stop": "360", "step": "1"})
    assert params == [0, 1135869952, 1065353216]
    assert catalog.float_from_bits(params[1]) == 360.0
    words = catalog.plan_words("PLAN_A", sweep.fields[0])
    assert len(words) == 4
    assert catalog.plan_from_words(words) == "PLAN_A"


def test_build_app_and_control_commands() -> None:
    sweep = catalog.FUNCTION_BY_KEY["angle.sweep"]
    assert catalog.build_app(sweep, {"start": "0", "stop": "360", "step": "1"}) == \
        "CONFigure:ANGLe:SWEEp 0,360,1"
    assert catalog.build_ctl(sweep, {"start": "0", "stop": "360", "step": "1"},
                             15, 15) == \
        "SYSTem:CTRL:EXECute TRIGGER_ANGLE_SWEEP,15,15,0,1135869952,1065353216"
    switch = catalog.FUNCTION_BY_KEY["switch.polarization"]
    assert switch.app_deprecated
    with pytest.raises(ValueError):
        catalog.build_app(switch, {"value": "1"})
    assert catalog.build_ctl(catalog.FUNCTION_BY_KEY["switch.channel"],
                             {"number": "1", "value": "1"}, 15, 15) == \
        "SYSTem:CTRL:EXECute TRIGGER_SWITCH,15,15,1,1"
    plan = catalog.FUNCTION_BY_KEY["sequence.plan"]
    command = catalog.build_ctl(plan, {"plan_id": "PLAN_A", "states": "0,1,2"}, 15, 15)
    assert command.startswith("SYSTem:CTRL:EXECute TRIGGER_SEQUENCE_PLAN,15,15,")
    assert len(command.split(",")[3:]) == 16
    assert catalog.build_app(catalog.FUNCTION_BY_KEY["run.start"],
                             {"plan_id": ""}) == "TRIGger:STARt"


def test_query_label_fallback() -> None:
    query = catalog.QUERY_BY_KEY["ack.ctrl_status"]
    prefix, values = catalog.split_response("1,0,4,101,15,15,15")
    assert prefix == [("status", "1"), ("result", "0")]
    assert values[0] == ("0", "4")
    assert catalog.response_label(query, 0, "4") == "seq"
    assert catalog.response_label(query, 99, "x") == "value[99]"


def test_simulator_refmem_ack_flow() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    assert sim.execute("*IDN?").startswith("DHRT100,SIMULATOR")
    assert sim.execute("SYSTem:CTRL:EXECute TRIGGER_MODE,15,15,1") == "QUEUED,101"
    status = [int(item) for item in sim.execute("SYSTem:CTRL:STATus?").split(",")]
    assert status[2] == 4
    assert status[6] == 0xF
    board = sim.execute("SYSTem:COMMand:TARget? 3").split(",")
    assert board[0] == "3" and board[2] == "4" and board[6] == "8"
    catalog_rows = sim.execute("SYSTem:CTRL:CATalog? 0,16").split(",")
    assert len(catalog_rows) == 16 * 7
    sim.close()


def test_simulator_obs_pages_match_contract() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    values = [int(item) for item in
              sim.execute("SYSTem:REFMEM:OBS? 6,0,4").split(",")]
    assert values[0:3] == [6, 0, 4]
    # row 0: slot,valid,epoch,seq,age,stale + enabled/up/down/adapter/nodes/slot
    assert values[3:5] == [0, 1]
    assert values[8] == 0                      # stale=0 on the local row
    assert values[9:15] == [1, 1, 1, 1, 4, 0]
    peer = values[15:27]
    assert peer[0] == 1 and peer[1] == 0
    assert peer[4] == 0xFFFFFFFF and peer[5] == 1
    assert sim.execute("SYSTem:REFMEM:OBS? 99,0,1").startswith("-200")
    assert sim.execute("SYSTem:REFMEM:OBS? 6,4,4").startswith("-200")
    sim.close()


def test_dispatcher_sync_request() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    dispatcher = device.Dispatcher(sim, lambda _result: None)
    dispatcher.start()
    try:
        result = dispatcher.request("*IDN?", "test", 2.0)
        assert not result.error
        assert result.response.startswith("DHRT100")
    finally:
        dispatcher.stop()
        dispatcher.join(2.0)
        sim.close()


def test_dispatcher_transport_serializes_worker_reads() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    dispatcher = device.Dispatcher(sim, lambda _result: None)
    dispatcher.start()
    try:
        adapter = device.DispatcherTransport(dispatcher, 2.0)
        assert adapter.execute("*IDN?").startswith("DHRT100")
        page = adapter.execute("SYSTem:REFMEM:OBS? 6,0,1")
        assert page.startswith("6,0,1")
        with pytest.raises(device.TransportError):
            adapter.execute("")
    finally:
        dispatcher.stop()
        dispatcher.join(2.0)
        sim.close()


def test_tcp_bridge_roundtrip() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    dispatcher = device.Dispatcher(sim, lambda _result: None)
    dispatcher.start()
    bridge = device.TcpBridge(dispatcher, port=0, timeout_s=2.0)
    bridge.start()
    try:
        with socket.create_connection(("127.0.0.1", bridge.actual_port),
                                      timeout=2.0) as client:
            stream = client.makefile("rwb", buffering=0)
            stream.write(b"*IDN?\n")
            assert stream.readline().decode().strip().startswith("DHRT100")
            stream.write(b"SYSTem:CTRL:EXECute TRIGGER_MODE,15,15,1\n")
            assert stream.readline().decode().strip() == "QUEUED,101"
            stream.write(b"SYSTem:CTRL:STATus?\n")
            values = stream.readline().decode().strip().split(",")
            assert values[2] == "4" and values[6] == "15"
    finally:
        bridge.stop()
        dispatcher.stop()
        dispatcher.join(2.0)
        sim.close()


def test_gui_and_tcp_share_one_transport() -> None:
    class FakeTransport:
        def __init__(self) -> None:
            self.opens = 0
            self.commands: list[str] = []

        def open(self) -> None:
            self.opens += 1

        def close(self) -> None:
            pass

        def execute(self, command: str) -> str:
            self.commands.append(command)
            return "OK"

    transport = FakeTransport()
    dispatcher = device.Dispatcher(transport, lambda _result: None)
    dispatcher.start()
    bridge = device.TcpBridge(dispatcher, port=0, timeout_s=2.0)
    bridge.start()
    try:
        transport.open()
        dispatcher.submit("*IDN?", "gui")
        with socket.create_connection(("127.0.0.1", bridge.actual_port),
                                      timeout=2.0) as client:
            stream = client.makefile("rwb", buffering=0)
            stream.write(b"SYSTem:CTRL:STATus?\n")
            assert stream.readline().decode().strip() == "OK"
        deadline = time.monotonic() + 2.0
        while len(transport.commands) < 2 and time.monotonic() < deadline:
            time.sleep(0.01)
        assert transport.opens == 1
        assert transport.commands == ["*IDN?", "SYSTem:CTRL:STATus?"]
    finally:
        bridge.stop()
        dispatcher.stop()
        dispatcher.join(2.0)


def test_scpi_block_and_receipt() -> None:
    block = ota.scpi_block(b"abc")
    assert block == b"#13abc"
    with tempfile.TemporaryDirectory() as directory:
        tmp_path = Path(directory)
        image_path = tmp_path / "image.bin"
        image_path.write_bytes(b"12345678")
        image = ota.OtaImage.load(image_path)
        assert image.size == 8
        assert image.crc32 == zlib.crc32(b"12345678") & 0xFFFFFFFF
        body = ota.receipt_body(
            mode="CONVENTIONAL_SINGLE_BOARD", backend="simulator",
            resource="sim", board_id="SIM-1", board_no="1", image=image,
            before={"build": "1"}, after={"build": "1"}, transfer={"chunks": 1},
            stage="READY_TO_REBOOT", passed=True, log_sha256="0" * 64)
        assert body["schema"] == ota.RECEIPT_SCHEMA
        assert len(body["receipt_sha256"]) == 64
        path = ota.write_receipt(tmp_path / "receipts", body)
        assert path.is_file()
        assert path.with_suffix(".json.sha256").is_file()


def test_ota_workflow_runs_all_steps_and_receipt() -> None:
    from types import SimpleNamespace
    from tools.domains.trigger.trigger_debug_demo.ota_workflow import OtaWorkflow

    class FakeDispatcher:
        def __init__(self) -> None:
            self.blocks = 0

        def request_bytes(self, payload, source, timeout, expect_response):
            self.blocks += 1
            return SimpleNamespace(error="", response="OK")

    class FakeApp:
        def __init__(self) -> None:
            self.dispatcher = FakeDispatcher()

    class FakePage:
        def __init__(self) -> None:
            import threading
            self.app = FakeApp()
            self._cancel_event = threading.Event()
            self.finished = None

        def _snapshot(self):
            return {"board_id": "SIM", "board_no": "1", "status": "READY_TO_REBOOT"}

        def _emit_state(self, _snapshot):
            return None

        def _request(self, command, timeout):
            if "PROGress?" in command:
                return "512,512,1000,0"
            return "1"

        def _record(self, _text):
            return None

        def _progress(self, _received, _expected):
            return None

        def _wait_ready(self):
            return "READY_TO_REBOOT", {"last_result": 0}

        def _finish(self, image, before, after, stage, passed, transfer,
                    auto_receipt, backend, resource):
            self.finished = {
                "stage": stage, "passed": passed, "transfer": transfer,
                "auto_receipt": auto_receipt,
            }

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        image_path = root / "image.bin"
        image_path.write_bytes(b"x" * 512)
        image = ota.OtaImage.load(image_path)
        page = FakePage()
        workflow = OtaWorkflow(page, root / "workflows")
        state = workflow.start(image, 256, False, False, "sim", "sim")

        assert state.status == "SUCCEEDED"
        assert state.cursor == len(workflow.spec.steps)
        assert page.finished["passed"] is True
        assert page.finished["transfer"]["chunks"] == 2
        assert page.app.dispatcher.blocks == 2
        assert (root / "workflows" /
                f"ota.conventional-{state.run_id}.json").is_file()


def test_ota_workflow_resumes_stream_at_chunk_offset() -> None:
    from types import SimpleNamespace
    from tools.domains.trigger.trigger_debug_demo.ota_workflow import OtaWorkflow

    class FakeDispatcher:
        def __init__(self) -> None:
            self.calls = 0
            self.received = 0

        def request_bytes(self, payload, source, timeout, expect_response):
            self.calls += 1
            if self.calls == 2:
                return SimpleNamespace(error="simulated link drop", response="")
            self.received += 256
            return SimpleNamespace(error="", response="OK")

    class FakePage:
        def __init__(self) -> None:
            import threading
            self.app = SimpleNamespace(dispatcher=FakeDispatcher())
            self._cancel_event = threading.Event()
            self.finished = []

        def _snapshot(self):
            return {"board_id": "SIM", "board_no": "1"}

        def _emit_state(self, _snapshot):
            return None

        def _request(self, command, timeout):
            if "PROGress?" in command:
                received = self.app.dispatcher.received
                return f"{received},512,1000,0"
            return "1"

        def _record(self, _text):
            return None

        def _progress(self, _received, _expected):
            return None

        def _wait_ready(self):
            return "READY_TO_REBOOT", {"last_result": 0}

        def _finish(self, image, before, after, stage, passed, transfer,
                    auto_receipt, backend, resource):
            self.finished.append({"stage": stage, "passed": passed})

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        image_path = root / "image.bin"
        image_path.write_bytes(b"y" * 512)
        image = ota.OtaImage.load(image_path)
        page = FakePage()
        state_dir = root / "workflows"
        workflow = OtaWorkflow(page, state_dir)
        try:
            workflow.start(image, 256, False, False, "sim", "sim")
        except RuntimeError:
            pass
        failed = workflow.latest()
        assert failed is not None
        assert failed.status == "FAILED"
        assert failed.step_data.get("chunk_index") == 1

        resumed = OtaWorkflow(page, state_dir)
        final = resumed.resume(failed.run_id)
        assert final.status == "SUCCEEDED"
        assert page.finished[-1]["passed"] is True


def test_simulated_conventional_ota() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    dispatcher = device.Dispatcher(sim, lambda _result: None)
    dispatcher.start()
    try:
        assert dispatcher.request("SYSTem:OTA:BEGin 8,123", "ota", 2).response == "1"
        payload = b"SYSTem:OTA:DATA " + ota.scpi_block(b"12345678")
        assert dispatcher.request_bytes(payload, "ota", 2, False).response == "OK"
        assert dispatcher.request("SYSTem:OTA:END", "ota", 2).response == "1"
        status = dispatcher.request("SYSTem:OTA:STATus?", "ota", 2).response
        assert "READY_TO_REBOOT" in status
        progress = dispatcher.request("SYSTem:OTA:PROGress?", "ota", 2).response
        assert progress.startswith("8,8,1000")
    finally:
        dispatcher.stop()
        dispatcher.join(2)
        sim.close()


def test_tcp_rejects_commands_during_ota() -> None:
    sim = device.SimulatedTransport(ack_delay_s=0)
    sim.open()
    dispatcher = device.Dispatcher(sim, lambda _result: None)
    dispatcher.start()
    bridge = device.TcpBridge(dispatcher, port=0, timeout_s=2.0,
                              can_accept=lambda: False)
    bridge.start()
    try:
        with socket.create_connection(("127.0.0.1", bridge.actual_port),
                                      timeout=2.0) as client:
            stream = client.makefile("rwb", buffering=0)
            stream.write(b"*IDN?\n")
            assert stream.readline().decode().strip() == "BUSY OTA"
    finally:
        bridge.stop()
        dispatcher.stop()
        dispatcher.join(2.0)
        sim.close()


def test_visa_transport_with_fake_session(monkeypatch) -> None:
    class FakeInstrument:
        def __init__(self) -> None:
            self.writes: list[str] = []
            self.replies: list[str] = []
            self.timeout = 2000
            self.write_termination = ""
            self.read_termination = ""
            self.closed = False

        def write(self, command: str) -> None:
            self.writes.append(command)

        def read(self) -> str:
            if not self.replies:
                raise RuntimeError("timeout")
            return self.replies.pop(0)

        def close(self) -> None:
            self.closed = True

    class FakeManager:
        def __init__(self) -> None:
            self.instrument = FakeInstrument()
            self.closed = False

        def open_resource(self, _resource: str) -> FakeInstrument:
            return self.instrument

        def list_resources(self, *_query) -> tuple[str, ...]:
            return ("USB0::0x1AB1::0x0610::SCOPE::INSTR",
                    "USB0::0x2E8A::0x0005::DHRT::INSTR",
                    "ASRL4::INSTR")

        def close(self) -> None:
            self.closed = True

    class FakeVisa:
        def __init__(self) -> None:
            self.manager = FakeManager()

        def ResourceManager(self) -> FakeManager:
            return self.manager

    fake = FakeVisa()
    monkeypatch.setattr(device, "pyvisa", fake)
    resources = device.available_visa_resources()
    assert resources[0].startswith("USB0::0x2E8A")
    transport = device.VisaTransport("USB0::0x2E8A::0x0005::DHRT::INSTR", 1.0)
    transport.open()
    instrument = fake.manager.instrument
    instrument.replies.append("DHRT100,USB,1,0.1")
    assert transport.execute("*IDN?") == "DHRT100,USB,1,0.1"
    instrument.replies.clear()
    assert transport.execute("TRIGger:STOP") == "OK"
    assert instrument.writes == ["*IDN?", "TRIGger:STOP"]
    transport.close()
    assert instrument.closed
    assert fake.manager.closed


def test_ui_smoke(monkeypatch) -> None:
    tk = pytest.importorskip("tkinter")
    try:
        root = tk.Tk()
    except tk.TclError:
        pytest.skip("no Tk display")
    root.destroy()

    from tools.domains.trigger.trigger_debug_demo import app as app_module
    settings_dir = tempfile.mkdtemp(prefix="trigger-demo-")
    monkeypatch.setattr(app_module, "SETTINGS_PATH",
                        Path(settings_dir) / "settings.json")
    application = app_module.DemoApp()
    try:
        application.withdraw()
        application.update()
        application.connect()
        for _ in range(30):
            application.update()
            time.sleep(0.01)
        assert application.connected
        assert len(application.notebook.tabs()) == 14
        for page_id in application.router.pages():
            widget = application.router._entries[page_id].widget
            assert callable(getattr(widget, "render", None)), page_id
        assert application.store.snapshot().connection.connected is True
        assert application.store.snapshot().connection.transport_mode == "single_port_refmem"
        assert str(application.ack_page.read_aggregate_button["state"]) == "normal"
        assert str(application.raw_page.send_button["state"]) == "normal"
        calibration = application.calibration_page
        calibration.mode.set("10 kHz")
        assert calibration._config_command().startswith(
            "SYST:TDMA:GUIDE:CONFig 1,305397761,1,23,")
        calibration.generation.set("2")
        assert calibration._config_command().startswith(
            "SYST:TDMA:GUIDE:CONFig 1,305397762,2,23,")
        calibration.generation.set("1")
        calibration.mode.set("10 MHz")
        assert calibration._config_command().startswith(
            "SYST:TDMA:GUIDE:CONFig 1,305397761,1,7,")
        calibration.show_result(
            "cal:boot",
            "1,0,8,0,0,0,1,0,4,23,1,1,1680835751,0,8,0,0,1,8,2", "")
        assert "READY" in calibration.boot_state.get()
        assert calibration.boot_vars["profile_source"].get() == "默认档"
        assert len(calibration.notebook.tabs()) == 9
        assert str(calibration.action_pages["P0"].run_button["state"]) == "disabled"
        assert calibration.action_pages["P1"].candidates.get() == "0,4,16"
        assert str(calibration.action_pages["P2"].run_button["state"]) == "normal"
        assert calibration.ladder_button.cget("text").startswith("运行全阶梯")
        assert application.tcp_page.port.get()
        application.overview_page.refresh()
        for _ in range(200):
            application.update()
            if "槽0" in application.overview_page.vars["nodes"].get():
                break
            time.sleep(0.01)
        assert "DHRT100" in application.overview_page.vars["idn"].get()
        assert "槽0" in application.overview_page.vars["nodes"].get()
        assert "heartbeat=" in application.overview_page.vars["nodes"].get()
    finally:
        application.close()
