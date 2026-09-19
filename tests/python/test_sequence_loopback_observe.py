"""Verify passive loopback observation never supplies simulated feedback."""
import pytest

from tools.hardware_acceptance.sequence_trigger_acceptance import STATUS_FIELDS
from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
    MODE_INDEPENDENT, MODE_RJ45, MODE_TURNTABLE, observe_loopback,
)


def status(run=1, count=0, state="IDLE", error="NONE"):
    row = dict.fromkeys(STATUS_FIELDS, 0)
    row.update(state=state, error=error, run_id=run, generation=run,
               count=8, accepted=count, completed=count, cycles=count // 8)
    return ','.join(str(row[key]) for key in STATUS_FIELDS)


class Device:
    def __init__(self, mode=MODE_INDEPENDENT, *, advance=79, fault=False, source="IN1"):
        self.mode, self.advance, self.fault, self.source = mode, advance, fault, source
        self.started = False
        self.commands = []
        self.now = 0

    def exchange(self, command):
        self.commands.append(command)
        if command == "TRIG:START":
            self.started = True
            return "1"
        if command == "TRIG:SEQ:NEXT?":
            if not self.started:
                return status()
            return status(2, self.advance, "FAULT" if self.fault else "IDLE" if self.advance else "READY",
                          "BAD" if self.fault else "NONE")
        if command == "READ:SEQ:LINK?":
            fields = [0] * 27
            fields[0] = int(self.mode != MODE_INDEPENDENT)
            fields[16] = 1
            return ','.join(map(str, fields))
        return {"*IDN?": "test,board,uid,1", "SYST:FW:BUILD?": '"test"',
                "READ:SEQ:SOUR?": f'"{self.source}","RISING"',
                "READ:SEQ:OUTPUT?": '7,8,"PULSE",10000,100,1,1',
                "READ:IO:STAT?": "0,0,0,0,0", "SYST:ERR?": '0,"No error"',
                "READ:SEQ:REPEAT?": "10,10,1" if self.advance else "0,0,0",
                "READ:SEQ:COUNTER?": f'{int(self.mode == MODE_TURNTABLE)},1,1,1000,0,0,0,0,0,0,0,0',
                }[command]

    def sleep(self, duration):
        self.now += duration

    def run(self, **kwargs):
        return observe_loopback(self.mode, self.exchange, lambda *args: None,
                                monotonic=lambda: self.now, sleep=self.sleep, **kwargs)


def test_independent_finite_loop_uses_only_start():
    device = Device()
    assert "有限轮次已结束" in device.run()
    assert [c for c in device.commands if '?' not in c] == ["TRIG:START"]


def test_absent_feedback_is_not_reported_as_success():
    device = Device(advance=0)
    result = device.run(duration=1)
    assert "未观察到推进" in result and "仍在运行" in result


def test_fault_aborts_observation():
    with pytest.raises(RuntimeError, match="序列异常"):
        Device(fault=True).run()


def test_manual_source_cannot_be_certified_as_physical_loop():
    device = Device(source="MANUAL")
    with pytest.raises(ValueError, match="外部输入"):
        device.run()
    assert not device.started


def test_cancel_does_not_inject_next():
    device = Device(advance=0)
    with pytest.raises(InterruptedError):
        device.run(cancelled=lambda: True)
    assert not device.started
    assert "TRIG:SEQ:NEXT" not in device.commands


def test_cancel_during_preflight_prevents_start():
    device = Device()
    with pytest.raises(InterruptedError):
        device.run(cancelled=lambda: "READ:SEQ:OUTPUT?" in device.commands)
    assert not device.started


@pytest.mark.parametrize("mode", [MODE_RJ45, MODE_TURNTABLE])
def test_link_modes_observe_ready_and_keep_counter_input_external(monkeypatch, mode):
    import tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui as module
    monkeypatch.setattr(module, "build_start_commands", lambda mode: ["TRIG:START"])
    device = Device(mode)
    assert "有限轮次" in device.run()
    assert "READ:SEQ:COUNTER?" in device.commands
    assert [c for c in device.commands if '?' not in c] == ["TRIG:START"]
