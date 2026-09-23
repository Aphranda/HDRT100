import tkinter as tk

import pytest

from tools.sequence_trigger_debug_ui.realtime_budget import (
    PREFIX, PHASES, BudgetEditor, BudgetSnapshot, BudgetTable, microseconds,
    parse_snapshot, run_budget_operation, table_from_microseconds,
)


DEFAULT = BudgetTable(375000, (
    (0, 217500, 212500), (217500, 260500, 42000), (260500, 295500, 34000),
    (295500, 310500, 14000), (310500, 315500, 4000), (315500, 340500, 24000),
    (340500, 343500, 3000), (343500, 365500, 21800), (365500, 367500, 2000),
    (367500, 375000, 0)))
BASELINE = BudgetSnapshot(250000000, DEFAULT)


def snapshot_response(table=DEFAULT, clock=250000000):
    values = [2, clock, table.cycle, len(PHASES), 1023, 0, 57, 0]
    for phase in table.phases:
        values.extend((*phase, 0, 0, 0, 0, 0, 0, 0, 0))
    return ",".join(map(str, values))


def changed_table():
    phases = list(DEFAULT.phases)
    start, end, wcet = phases[1]
    phases[1] = (start, end, wcet - 1000)
    return BudgetTable(DEFAULT.cycle, tuple(phases))


class Device:
    def __init__(self):
        self.table = DEFAULT
        self.saved = DEFAULT
        self.commands = []
        self.stops = 0
        self.capabilities = "1,1"
        self.validation = None
        self.error_command = None
        self.last_write = None
        self.apply_wrong = False
        self.periods = []

    def stop(self):
        self.stops += 1

    def exchange(self, command):
        self.commands.append(command)
        if command == PREFIX + ":CAPABILITIES?":
            return self.capabilities
        if command == "SYST:TDMA:PERIOD?":
            return self.periods.pop(0) if self.periods else f"{self.table.cycle},1,0,1,0"
        if command == PREFIX + "?":
            return snapshot_response(self.table)
        if command == PREFIX + ":STATUS?":
            return f"1,375000,{self.table.cycle},1,1,{int(self.table == self.saved)},0,0,1,0,NONE,10"
        if command == "SYST:ERR?":
            return '-200,"Rejected"' if self.last_write == self.error_command else '0,"No error"'
        if command == PREFIX + ":VALIDATE?":
            return self.validation or f"1,NONE,10,{self.draft_cycle}"
        self.last_write = command
        if command == PREFIX + ":BEGIN":
            self.draft_phases = list(self.table.phases)
            self.draft_cycle = self.table.cycle
        elif command.startswith(PREFIX + ":PHASE "):
            index, start, end, wcet = map(int, command.split()[1].split(","))
            self.draft_phases[index] = (start, end, wcet)
        elif command.startswith(PREFIX + ":CYCLE "):
            self.draft_cycle = int(command.split()[1])
        elif command == PREFIX + ":APPLY":
            if not self.apply_wrong:
                self.table = BudgetTable(self.draft_cycle, tuple(self.draft_phases))
        elif command == PREFIX + ":SAVE":
            self.saved = self.table
        elif command != PREFIX + ":DISCARD":
            raise AssertionError(command)
        return '"OK"'

    def run(self, action, table=DEFAULT, **kwargs):
        return run_budget_operation(action, {"baseline": BASELINE, "table": table},
                                    self.exchange, lambda *_: None, self.stop, **kwargs)


def test_snapshot_clock_table_and_microsecond_units():
    assert parse_snapshot(snapshot_response()) == BASELINE
    assert microseconds(375000, 250000000) == "1500"
    assert microseconds(43000, 250000000) == "172"
    assert microseconds(1, 250000000) == "0.004"
    assert microseconds(0, 250000000) == "0"


@pytest.mark.parametrize("response", ["", "2,250000000,375000,10", snapshot_response().replace("2,250000000", "1,250000000", 1)])
def test_bad_snapshot_rejected(response):
    with pytest.raises((ValueError, StopIteration)):
        parse_snapshot(response)


def test_real_device_schedule_v2_packet():
    # Captured from USBTMC build 20260920075326, not a generated protocol mock.
    packet = ("2,250000000,375000,10,91,8,1397473,118944,0,217500,212500,383,34392,77480,1397473,0,0,0,0,"
              "217500,260500,42000,217510,2044,10561,1397473,0,0,0,0,260500,295500,34000,260503,5181,15959,1397473,0,0,0,0,"
              "295500,310500,14000,0,0,0,0,1397473,0,0,0,310500,315500,4000,310563,4512,4512,28,1397445,0,1,1,"
              "315500,340500,24000,315508,541,4883,1397468,5,5,0,0,340500,343500,3000,0,0,0,0,1397472,0,0,0,"
              "343500,365500,21800,343501,2653,19796,1278534,118938,118938,0,0,365500,367500,2000,0,0,0,0,1397472,0,0,0,"
              "367500,375000,0,0,0,0,0,0,0,0,0")
    assert parse_snapshot(packet) == BudgetSnapshot(250000000, DEFAULT, 8, 118944)


@pytest.mark.parametrize("period", [500, 1000])
def test_short_cycle_keeps_explicit_phase_budgets(period):
    table = table_from_microseconds(250000000, str(period),
                                   ["370", "20", "15", "10", "5", "10", "5", "20", "5"],
                                   ["366", "19", "11", "8", "4", "8", "4", "18", "4"])
    assert table.cycle == period * 250
    assert table.phases[0] == (0, 92500, 91500)
    assert table.phases[1] == (92500, 97500, 4750)
    assert table.phases[-1] == (115000, period * 250, 0)


@pytest.mark.parametrize("value", ["NaN", "Infinity", "-1", "oops", "0.001", "1e999999"])
def test_invalid_numeric_budgets(value):
    with pytest.raises(ValueError):
        table_from_microseconds(250000000, value, ["1"] * 9, ["0"] * 9)


def test_short_period_does_not_silently_shrink_other_phases():
    with pytest.raises(ValueError):
        table_from_microseconds(250000000, "500", ["870", "172", "140", "60", "20", "100", "12", "88", "8"],
                                ["850", "168", "136", "56", "16", "96", "12", "87.2", "8"])


def test_wcet_must_fit_window():
    with pytest.raises(ValueError):
        table_from_microseconds(250000000, "1000", ["10"] * 9, ["11"] * 9)


def test_read_only_sends_queries_and_does_not_stop():
    device = Device()
    result = device.run("read")
    assert result["snapshot"] == BASELINE
    assert result["persisted"]
    assert all(command.endswith("?") for command in device.commands)
    assert device.stops == 0


@pytest.mark.parametrize("action", ["validate", "apply", "save"])
@pytest.mark.parametrize("capability", ["0,0", "1,0", "1", '"UNAVAILABLE"', "2,1"])
def test_old_firmware_never_receives_setters(action, capability):
    device = Device()
    device.capabilities = capability
    with pytest.raises(RuntimeError, match="固件"):
        device.run(action)
    assert device.commands == [PREFIX + ":CAPABILITIES?"]
    assert device.stops == 0


def test_validate_discards_temporary_draft_without_apply_or_flash():
    device = Device()
    result = device.run("validate", changed_table())
    assert device.table == device.saved == DEFAULT
    assert result["snapshot"].table == DEFAULT
    assert PREFIX + ":DISCARD" in device.commands
    assert PREFIX + ":APPLY" not in device.commands
    assert PREFIX + ":SAVE" not in device.commands
    assert device.stops == 0


def test_device_validation_failure_never_applies_or_saves():
    device = Device()
    device.validation = "0,TDMA_WCET,0,375000"
    with pytest.raises(RuntimeError, match="TDMA_WCET"):
        device.run("apply", changed_table())
    assert device.table == device.saved == DEFAULT
    assert PREFIX + ":APPLY" not in device.commands
    assert PREFIX + ":SAVE" not in device.commands


def test_partial_stage_error_aborts():
    device = Device()
    device.error_command = PREFIX + ":PHASE 1,217500,260500,41000"
    with pytest.raises(RuntimeError, match="被拒绝"):
        device.run("apply", changed_table())
    assert PREFIX + ":VALIDATE?" not in device.commands
    assert PREFIX + ":APPLY" not in device.commands
    assert device.table == device.saved == DEFAULT


def test_apply_is_volatile_and_checks_actual_table():
    device = Device()
    result = device.run("apply", changed_table())
    assert device.table == changed_table()
    assert device.saved == DEFAULT
    assert not result["persisted"]
    assert PREFIX + ":SAVE" not in device.commands
    assert device.stops == 1


def test_apply_wrong_readback_is_not_success():
    device = Device()
    device.apply_wrong = True
    with pytest.raises(RuntimeError, match="实际预算"):
        device.run("apply", changed_table())


def test_stale_baseline_blocks_any_stage_or_save():
    device = Device()
    device.table = changed_table()
    with pytest.raises(RuntimeError, match="已变化"):
        device.run("save")
    assert all(command.endswith("?") for command in device.commands)


def test_save_rejects_unapplied_edits_before_stop():
    device = Device()
    with pytest.raises(ValueError, match="尚未临时应用"):
        device.run("save", changed_table())
    assert device.commands == [PREFIX + ":CAPABILITIES?"]
    assert device.stops == 0


def test_only_save_persists_actual_applied_budget():
    device = Device()
    device.saved = changed_table()
    result = device.run("save")
    assert device.saved == DEFAULT
    assert result["persisted"]
    assert [c for c in device.commands if not c.endswith("?")] == [PREFIX + ":SAVE"]
    assert device.stops == 1


def test_pending_period_waits_even_when_generations_equal():
    device = Device()
    device.periods = ["375000,1,375000,1,0", "375000,1,0,1,0"]
    sleeps = []
    device.run("read", sleep=sleeps.append)
    assert sleeps == [.05]


def test_cancelled_legacy_period_generation_gap_is_settled():
    device = Device()
    device.periods = ["375000,1,0,2,0"]
    sleeps = []
    result = device.run("read", sleep=sleeps.append)
    assert result["snapshot"] == BASELINE
    assert sleeps == []


def test_period_timeout_never_stages():
    device = Device()
    device.periods = ["375000,1,375000,2,1"] * 4
    ticks = iter([0, 1, 6])
    with pytest.raises(RuntimeError, match="超时"):
        device.run("apply", changed_table(), sleep=lambda _: None, monotonic=lambda: next(ticks))
    assert PREFIX + ":BEGIN" not in device.commands


@pytest.fixture
def editor(capsys):
    try:
        with capsys.disabled():
            root = tk.Tk()
    except tk.TclError as exc:
        if "no display name" in str(exc) or "couldn't connect to display" in str(exc):
            pytest.skip(str(exc))
        raise
    root.withdraw()
    submitted = []
    view = BudgetEditor(root, lambda *args: submitted.append(args))
    yield view, submitted
    root.destroy()


def test_editor_save_disabled_for_unapplied_draft_and_after_connection_change(editor):
    view, submitted = editor
    view.receive_result("read", Device().run("read"))
    buttons = dict(view._buttons)
    assert str(buttons["save"]["state"]) == "normal"
    view.wcets_us[1].set("164")
    assert str(buttons["save"]["state"]) == "disabled"
    assert str(buttons["apply"]["state"]) == "normal"
    view.request("apply")
    assert submitted[0][0] == "apply"
    assert submitted[0][1]["table"] == changed_table()
    assert all(str(button["state"]) == "disabled" for _, button in view._buttons)
    view.invalidate()
    assert str(buttons["save"]["state"]) == "disabled"
    assert str(buttons["read"]["state"]) == "normal"


def test_editor_validate_keeps_edit_without_marking_applied(editor):
    view, _ = editor
    view.receive_result("read", Device().run("read"))
    view.wcets_us[1].set("164")
    view.receive_result("validate", Device().run("validate", changed_table()))
    assert view.wcets_us[1].get() == "164"
    assert view._snapshot.table == DEFAULT
    assert str(dict(view._buttons)["save"]["state"]) == "disabled"
    assert "尚未应用" in view.message.get()
