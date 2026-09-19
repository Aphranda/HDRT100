"""Exercise the real Tk controls without opening serial/VISA or running OTA."""
import os
import tkinter as tk
from tkinter import ttk

import pytest

from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
    MODE_INDEPENDENT, MODE_RJ45, MODE_TURNTABLE, ROLE_GATEWAY, ROLE_SEQUENCE, SequenceUi,
)
from tools.sequence_trigger_debug_ui import settings


@pytest.fixture
def ui(monkeypatch, capsys, tmp_path):
    monkeypatch.setattr(SequenceUi, "refresh_ports", lambda *args, **kwargs: None)
    monkeypatch.setattr(SequenceUi, "_operation_loop", lambda self: None)
    try:
        # Windows Tcl initializes native file descriptors; pytest's fd capture
        # can interfere with opening init.tcl when creating successive roots.
        with capsys.disabled():
            window = SequenceUi(settings_path=tmp_path / "settings.json")
    except tk.TclError as exc:
        if "no display name" in str(exc) or "couldn't connect to display" in str(exc):
            pytest.skip(f"Tk display unavailable: {exc}")
        raise
    window.port.set("TEST_PORT_A")
    window.update()
    yield window
    for callback in window.tk.call("after", "info"):
        window.after_cancel(callback)
    window.close()


@pytest.fixture
def ui_factory(monkeypatch, capsys):
    monkeypatch.setattr(SequenceUi, "refresh_ports", lambda *args, **kwargs: None)
    monkeypatch.setattr(SequenceUi, "_operation_loop", lambda self: None)
    windows = []
    def create(path):
        try:
            with capsys.disabled():
                window = SequenceUi(settings_path=path)
        except tk.TclError as exc:
            if "no display name" in str(exc) or "couldn't connect to display" in str(exc):
                pytest.skip(f"Tk display unavailable: {exc}")
            raise
        window.update()
        windows.append(window)
        return window
    yield create
    for window in windows:
        if not window._closing:
            close_test_window(window)


def close_test_window(window):
    for callback in window.tk.call("after", "info"):
        window.after_cancel(callback)
    window.close()


def test_settings_restore_drafts_across_windows_without_restoring_device_state(ui_factory, tmp_path):
    path = tmp_path / "settings.json"
    first = ui_factory(path)
    first.backend.set("USB TMC")
    first.port.set("USB0::TEST::INSTR")
    first.plan.set("INDEPENDENT")
    first.gateway_codes.set("7,3,1")
    first.turntable_angle_enabled.set(True)
    first.turntable_angle_start.set("2")
    first.turntable_angle_stop.set("4")
    first.turntable_angle_step.set("0.5")
    first.turntable_angle_speed.set("0.5")
    first.turntable_angle_ppd.set("200")
    first.turntable_rate_kind.set("周期 ms")
    first.turntable_rate_value.set("20")
    first.turntable_threshold.set("123")
    first.turntable_repeat_count.set("456")
    first.gateway_out_roles[2].set(ROLE_GATEWAY)
    first.gateway_out_roles[3].set(ROLE_SEQUENCE)
    first.out_enabled[3].set(False)
    first.auto_scroll.set(False)
    first.loopback_durations[MODE_TURNTABLE].set("19")
    select(first, first.turntable_page)
    first._configured_mode = MODE_TURNTABLE
    first._device_mode = MODE_TURNTABLE
    first.sequence_state.set("RUNNING")
    first.manual_command.set("TRIG:START")
    first.ota_file.set("dangerous-unrequested-ota.pkg")
    assert first._operations.empty()
    close_test_window(first)
    values = settings.load(path)
    assert not any(name in values for name in ("_configured_mode", "sequence_state", "manual_command", "ota_file"))
    second = ui_factory(path)
    assert second._resource_key() == ("USB TMC", "USB0::TEST::INSTR")
    assert second.plan.get() == "INDEPENDENT" and second.gateway_codes.get() == "7,3,1"
    assert second.run_mode.get() == MODE_TURNTABLE
    assert second.turntable_angle_enabled.get()
    assert second.turntable_angle_count.get() == "5"
    assert second.turntable_angle_threshold.get() == "100"
    assert second.turntable_angle_speed.get() == "0.5" and second.turntable_angle_ppd.get() == "200"
    assert second.turntable_threshold.get() == "123" and second.turntable_repeat_count.get() == "456"
    assert second.turntable_rate_kind.get() == "周期 ms" and second.turntable_rate_value.get() == "20"
    assert second.gateway_out_roles[2].get() == ROLE_GATEWAY
    assert not second.out_enabled[3].get() and not second.auto_scroll.get()
    assert second.loopback_durations[MODE_TURNTABLE].get() == "19"
    assert second.sequence_state.get() == "UNKNOWN"
    assert second._configured_mode is None and second._device_mode is None
    assert second.manual_command.get() == second.ota_file.get() == ""
    assert second._operations.empty()
    second.command("TRIG:START")
    assert second._operations.empty()


@pytest.mark.parametrize("content", ["{not-json", '{"version":999,"values":{}}'])
def test_settings_corrupt_or_wrong_version_loads_defaults_and_warns(ui_factory, tmp_path, content):
    path = tmp_path / "settings.json"
    path.write_text(content, encoding="utf-8")
    window = ui_factory(path)
    assert window.plan.get() == "SP8T"
    assert window._operations.empty()
    assert "读取失败" in window.output.get("1.0", "end")


def test_settings_invalid_types_enums_and_unallowlisted_runtime_are_ignored(ui_factory, tmp_path):
    path = tmp_path / "settings.json"
    settings.save(path, dict(backend="HACK", port=[], plan="RESTORED", codes=123,
        turntable_angle_enabled="true", auto_scroll=1, edge="INVALID", selected_tab="bad",
        out_enabled=[True, True, True, 1], gateway_out_roles=["invalid"] * 4,
        loopback_durations={MODE_TURNTABLE: "9" * 5000},
        _configured_mode=MODE_TURNTABLE, sequence_state="RUNNING", manual_command="TRIG:START"))
    window = ui_factory(path)
    assert window.backend.get() == "Serial" and window.port.get() == ""
    assert window.plan.get() == "RESTORED" and window.codes.get() == "0,1,2,3,4,5,6,7"
    assert not window.turntable_angle_enabled.get() and window.auto_scroll.get()
    assert window.edge.get() == "RIS" and window.run_mode.get() == MODE_INDEPENDENT
    assert window.loopback_durations[MODE_TURNTABLE].get() == "60"
    assert all(variable.get() for variable in window.out_enabled)
    assert window._configured_mode is None and window.sequence_state.get() == "UNKNOWN"
    assert window._operations.empty()
    assert "无效字段" in window.output.get("1.0", "end")


def test_settings_explicitly_disabled_and_save_failure_do_not_block_close(ui_factory, tmp_path, monkeypatch):
    warnings = []
    monkeypatch.setattr("tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui.messagebox.showwarning",
                        lambda *args, **kwargs: warnings.append(args))
    window = ui_factory(None)
    monkeypatch.setattr(settings, "save", lambda *_: pytest.fail("disabled persistence wrote settings"))
    close_test_window(window)
    enabled = ui_factory(tmp_path / "settings.json")
    logs = []
    enabled.log = lambda text, *_: logs.append(text)
    def fail_save(*_):
        raise OSError("read-only path")
    monkeypatch.setattr(settings, "save", fail_save)
    close_test_window(enabled)
    assert enabled._closing and any("保存失败" in text for text in logs)
    assert warnings and warnings[0][0] == "配置未保存"


def test_settings_not_written_when_ota_blocks_close(ui, monkeypatch):
    monkeypatch.setattr(settings, "save", lambda *_: pytest.fail("must not save during blocked close"))
    monkeypatch.setattr("tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui.messagebox.showwarning", lambda *_: None)
    ui._ota_running = True
    ui.close()
    assert not ui._closing and not ui._settings_path.exists()
    ui._ota_running = False
    ui._settings_path = None


def test_resource_scan_preserves_saved_endpoint_when_device_is_absent(ui, monkeypatch):
    from tools.sequence_trigger_debug_ui import sequence_trigger_debug_ui as module
    monkeypatch.setattr(module, "discover_serial_ports", lambda: [])
    ui.port.set("COM_KEPT")
    # The fixture replaces refresh_ports; invoke the preserved class implementation.
    ORIGINAL_REFRESH_PORTS(ui, log_result=False)
    assert ui.port.get() == "COM_KEPT" and ui._operations.empty()


ORIGINAL_REFRESH_PORTS = SequenceUi.refresh_ports


def select(ui, page):
    ui.mode_notebook.select(page)
    ui.update()


def descendants(widget):
    for child in widget.winfo_children():
        yield child
        yield from descendants(child)


def complete_configuration(ui, mode):
    ui.configure_mode(mode)
    kind, payload = ui._operations.get_nowait()
    assert kind == "configuration"
    backend, resource, commands, mode, count, generation = payload
    ui._configuration_finished(mode, count, generation, (backend, resource))
    return commands


def test_tabs_separate_mode_controls_and_keep_common_io(ui):
    assert [ui.mode_notebook.tab(tab, "text") for tab in ui.mode_notebook.tabs()] == [
        "独立 SP8T 序列", "RJ45 物理回环", "转台脉冲计数", "手动 SP8T", "设备维护"]
    select(ui, ui.loopback_page)
    texts = [widget.cget("text") for widget in descendants(ui.loopback_page)
             if isinstance(widget, ttk.Button)]
    assert "下一步" in texts and "软件单步" not in texts
    assert ui.gateway_group.winfo_ismapped()
    assert ui.gateway_output_group.winfo_ismapped()
    assert len(ui.gateway_out_role_boxes) == 4
    assert all(int(box.cget("width")) == 7 for box in
               [*ui.out_role_boxes, *ui.gateway_out_role_boxes])
    for group, widgets in (
            (ui.gateway_output_group,
             [*ui.gateway_out_checkbuttons, *ui.gateway_out_role_boxes]),
            (ui.independent_output_group,
             [*ui.out_checkbuttons, *ui.out_role_boxes])):
        assert all(widget.winfo_rootx() + widget.winfo_width() <=
                   group.winfo_rootx() + group.winfo_width() for widget in widgets)
        assert max(widget.winfo_rooty() for widget in widgets) - \
            min(widget.winfo_rooty() for widget in widgets) <= 5
    assert not ui.source_box.winfo_ismapped()
    assert not ui.status_mode_box.winfo_ismapped()
    for page in (ui.independent_page, ui.loopback_page, ui.manual_switch_page, ui.maintenance_page):
        select(ui, page)
        assert ui.io_panel.winfo_ismapped() and ui.output.winfo_ismapped()
        assert ui.connection_panel.winfo_ismapped()
    assert ui._operations.empty()


@pytest.mark.parametrize("size", ["1380x900", "1120x820"])
def test_independent_layout_matches_loopback_groups(ui, size):
    ui.geometry(size)
    select(ui, ui.independent_page)
    output_widgets = [*ui.out_checkbuttons, *ui.out_role_boxes]
    assert max(widget.winfo_rooty() for widget in output_widgets) - \
        min(widget.winfo_rooty() for widget in output_widgets) <= 5
    group_right = (ui.independent_output_group.winfo_rootx() +
                   ui.independent_output_group.winfo_width())
    assert all(widget.winfo_rootx() + widget.winfo_width() <= group_right
               for widget in output_widgets)

    parameter_widgets = [widget for widget in descendants(ui.independent_input_group)
                         if isinstance(widget, (ttk.Entry, ttk.Combobox))]
    assert len(parameter_widgets) == 4
    assert max(widget.winfo_rooty() for widget in parameter_widgets) - \
        min(widget.winfo_rooty() for widget in parameter_widgets) <= 5


def test_switching_tabs_preserves_independent_drafts_and_never_sends_commands(ui):
    pairs = [(ui.plan, ui.gateway_plan), (ui.codes, ui.gateway_codes),
             (ui.settle, ui.gateway_settle), (ui.pulse, ui.gateway_pulse),
             (ui.repeat_count, ui.gateway_repeat_count), (ui.edge, ui.gateway_edge)]
    for index, (independent, gateway) in enumerate(pairs):
        independent.set(f"independent-{index}")
        gateway.set(f"gateway-{index}")
    for page in (ui.loopback_page, ui.manual_switch_page, ui.independent_page):
        select(ui, page)
    for index, (independent, gateway) in enumerate(pairs):
        assert independent.get() == f"independent-{index}"
        assert gateway.get() == f"gateway-{index}"
    assert ui._operations.empty()


@pytest.mark.parametrize("mode", [MODE_INDEPENDENT, MODE_RJ45])
def test_configuration_does_not_validate_hidden_mode_draft(ui, mode):
    if mode == MODE_INDEPENDENT:
        ui.gateway_codes.set("invalid")
        ui.gateway_timeout.set("invalid")
        ui.gateway_pulse.set("invalid")
        ui.gateway_out_roles[0].set("invalid")
    else:
        select(ui, ui.loopback_page)
        ui.codes.set("invalid")
        ui.settle.set("invalid")
        ui.pulse.set("invalid")
        ui.out_roles[0].set("invalid")
    commands = complete_configuration(ui, mode)
    assert "TRIG:START" not in commands
    assert ui._configured_mode == mode
    if mode == MODE_RJ45:
        assert "CONF:SEQ:OUTPUT 7,0,NONE,10,0" in commands
        assert "CONF:SEQ:LINK LOOPBACK,2,3,MANUAL,OUT4,10,5000,RIS" in commands


def test_loopback_out_attributes_drive_both_sequence_mask_and_gateway_output(ui):
    select(ui, ui.loopback_page)
    ui.gateway_codes.set("0,1,2,3,8,9,10,11")
    ui.gateway_out_roles[2].set(ROLE_GATEWAY)
    ui.gateway_out_roles[3].set(ROLE_SEQUENCE)

    commands = complete_configuration(ui, MODE_RJ45)

    assert "CONF:SEQ:OUTPUT 11,0,NONE,10,0" in commands
    assert "CONF:SEQ:LINK LOOPBACK,2,3,MANUAL,OUT3,10,5000,RIS" in commands


def test_start_requires_completed_configuration_and_draft_edits_invalidate_it(ui):
    ui.command("TRIG:START")
    assert ui._operations.empty()
    complete_configuration(ui, MODE_INDEPENDENT)
    ui.command("TRIG:START")
    assert "TRIG:START" in ui._operations.get_nowait()[1][2]
    ui.repeat_count.set("10")
    ui.command("TRIG:START")
    assert ui._operations.empty()


def test_tab_change_retains_device_mode_for_stop_refresh_and_step_rejection(ui):
    select(ui, ui.loopback_page)
    complete_configuration(ui, MODE_RJ45)
    select(ui, ui.independent_page)
    ui.source.set("MANUAL")
    ui.command("TRIG:SEQ:NEXT")
    assert "TRIG:SEQ:NEXT" in ui._operations.get_nowait()[1][2]
    ui.command("TRIG:START")
    assert ui._operations.empty()
    ui.command("TRIG:SEQ:NEXT?")
    assert "READ:SEQ:LINK?" in ui._operations.get_nowait()[1][2]
    ui.command("TRIG:STOP")
    assert "SYST:TDMA:RING:STOP" in ui._operations.get_nowait()[1][2]


def test_late_configuration_tracks_device_but_does_not_authorize_changed_draft(ui):
    select(ui, ui.loopback_page)
    ui.configure()
    _, payload = ui._operations.get_nowait()
    backend, resource, _, mode, count, generation = payload
    select(ui, ui.independent_page)
    ui._configuration_finished(mode, count, generation, (backend, resource))
    assert ui._device_mode == MODE_RJ45
    assert ui._configured_mode is None
    ui.command("TRIG:STOP")
    assert "SYST:TDMA:RING:STOP" in ui._operations.get_nowait()[1][2]


def test_previous_resource_responses_cannot_update_new_device_or_authorize_start(ui):
    original = ui._resource_key()
    generation = ui._configuration_generation
    ui.update_io("READ:IO:STAT?", "15,15,15,1,1")
    ui.port.set("TEST_PORT_B")
    ui._configuration_finished(MODE_RJ45, 8, generation, original)
    ui.log_exchange("READ:IO:STAT?", "15,15,15,1,1", original)
    assert ui._configured_mode is None
    assert ui._device_mode is None
    assert ui.sequence_state.get() == "UNKNOWN"
    assert all(lamp.cget("bg") == "#e5e7eb" for lamp in ui.output_lamps)
    assert "15" not in ui.io_owned.get()
    ui.command("TRIG:START")
    assert ui._operations.empty()


@pytest.mark.parametrize("size", ["1380x900", "1120x820"])
def test_visible_controls_fit_window_and_log_remains_usable(ui, size, tmp_path):
    ui.geometry(size)
    for page in (ui.independent_page, ui.loopback_page, ui.turntable_page,
                 ui.manual_switch_page, ui.maintenance_page):
        select(ui, page)
        assert ui.output.winfo_height() >= 90
        for widget in descendants(ui):
            if not widget.winfo_ismapped() or not isinstance(widget, (ttk.Button, ttk.Entry, ttk.Combobox)):
                continue
            left = widget.winfo_rootx() - ui.winfo_rootx()
            top = widget.winfo_rooty() - ui.winfo_rooty()
            assert left >= 0 and top >= 0, str(widget)
            assert left + widget.winfo_width() <= ui.winfo_width(), str(widget)
            assert top + widget.winfo_height() <= ui.winfo_height(), str(widget)
            assert widget.winfo_width() >= widget.winfo_reqwidth() - 2, str(widget)
        if os.environ.get("SEQUENCE_UI_SCREENSHOTS") == "1":
            from PIL import ImageGrab
            bounds = (ui.winfo_rootx(), ui.winfo_rooty(),
                      ui.winfo_rootx() + ui.winfo_width(), ui.winfo_rooty() + ui.winfo_height())
            ImageGrab.grab(bbox=bounds).save(tmp_path / f"page-{ui.mode_notebook.index(page)}.png")


def test_turntable_drafts_are_separate_and_device_counter_readback_survives_tab_switch(ui):
    ui.codes.set("invalid")
    ui.gateway_codes.set("invalid")
    select(ui, ui.turntable_page)
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert "CONF:SEQ:LINK POSITION,1,2,3,IN1,1000,IN2,OUT4,10,5000,RIS" in commands
    assert ui.next_buttons[MODE_TURNTABLE].instate(["disabled"])
    ui.command("TRIG:SEQ:NEXT")
    assert ui._operations.empty()
    ui.command("TRIG:START")
    assert "READ:SEQ:COUNTER?" in ui._operations.get_nowait()[1][2]
    select(ui, ui.independent_page)
    ui.command("TRIG:STOP")
    stopped = ui._operations.get_nowait()[1][2]
    assert "SYST:TDMA:RING:STOP" in stopped and "READ:SEQ:COUNTER?" in stopped
    ui.update_io("READ:SEQ:COUNTER?", "1,1,1,1000,45,0,45,0,0,0,9,0")
    assert "45/1000" in ui.counter_status.get()
    ui.port.set("OTHER")
    assert ui.counter_status.get() == "转台计数：未读取"


@pytest.mark.parametrize("mode,page", [(MODE_INDEPENDENT, "independent_page"),
    (MODE_RJ45, "loopback_page"), (MODE_TURNTABLE, "turntable_page")])
def test_loopback_tools_presets_and_start_gate(ui, mode, page):
    select(ui, getattr(ui, page))
    assert any(isinstance(widget, ttk.Button) and widget.cget("text") == "回环工具"
               for widget in descendants(getattr(ui, page)))
    ui.open_loopback(mode)
    ui.turntable_threshold.set("1234")
    ui.apply_loopback_preset(mode)
    assert ui._operations.empty()
    assert ui.turntable_threshold.get() == "1234"
    assert ui.turntable_input.get() == "IN1"
    assert ui.turntable_ready_input.get() == "IN2"
    ui.start_loopback(mode)
    assert ui._operations.empty()
    complete_configuration(ui, mode)
    ui.start_loopback(mode)
    resource = ui._resource_key()
    assert ui._operations.get_nowait() == ("loopback", (*resource, mode, 60 if mode == MODE_TURNTABLE else 10))
    ui.port.set("TEST_PORT_B")
    ui.command_mode(mode, "TRIG:STOP")
    assert ui._loopback_cancel.is_set()
    stop = ui._operations.get_nowait()[1]
    assert stop[:2] == resource
    assert "TRIG:STOP" in stop[2]


def test_finished_observation_does_not_redirect_normal_stop_on_new_device(ui):
    complete_configuration(ui, MODE_INDEPENDENT)
    ui.start_loopback(MODE_INDEPENDENT)
    original = ui._resource_key()
    ui._operations.get_nowait()
    ui._ui_events.put(("loopback-finish", ()))
    ui._poll_ui_events()
    ui.port.set("TEST_PORT_B")
    select(ui, ui.loopback_page)
    complete_configuration(ui, MODE_RJ45)
    ui.command("TRIG:STOP")
    _, (backend, resource, commands) = ui._operations.get_nowait()
    assert resource == "TEST_PORT_B" and "SYST:TDMA:RING:STOP" in commands
    ui.stop_loopback(MODE_INDEPENDENT)
    assert ui._operations.get_nowait()[1][:2] == original


def test_old_mode_window_stop_cancels_observation_on_same_physical_device(ui):
    ui._loopback_targets[MODE_INDEPENDENT] = (*ui._resource_key(), MODE_INDEPENDENT)
    ui._loopback_target = (*ui._resource_key(), MODE_RJ45)
    ui._loopback_running = True
    ui.stop_loopback(MODE_INDEPENDENT)
    assert ui._loopback_cancel.is_set()
    assert "SYST:TDMA:RING:STOP" in ui._operations.get_nowait()[1][2]


def test_rate_dialog_fills_threshold_without_sending_and_requires_reconfigure(ui):
    select(ui, ui.turntable_page)
    complete_configuration(ui, MODE_TURNTABLE)
    ui.open_turntable_rate()
    ui.turntable_rate_kind.set("周期 ms")
    ui.turntable_rate_value.set("20")
    ui.apply_turntable_rate()
    assert ui.turntable_threshold.get() == "50"
    assert ui._configured_mode is None and ui._operations.empty()
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert any(",IN1,50,IN2,OUT4," in command for command in commands)
    ui.turntable_rate_value.set("invalid")
    ui.apply_turntable_rate()
    assert ui.turntable_threshold.get() == "50" and ui._operations.empty()


def test_angle_dialog_four_primary_fields_bind_scpi_and_preserve_raw_drafts(ui):
    select(ui, ui.turntable_page)
    ui.turntable_threshold.set("1234")
    ui.turntable_repeat_count.set("99")
    ui.open_turntable_angle()
    scan_group = next(widget for widget in descendants(ui._turntable_angle_window)
                      if isinstance(widget, ttk.LabelFrame) and widget.cget("text") == "扫描配置")
    assert len([widget for widget in descendants(scan_group) if isinstance(widget, ttk.Entry)]) == 4
    ui.turntable_angle_enabled.set(True)
    ui.turntable_angle_start.set("0")
    ui.turntable_angle_stop.set("0.05")
    ui.turntable_angle_step.set("0.05")
    ui.turntable_angle_speed.set("0.05")
    assert ui.turntable_repeat_entry.instate(["readonly"])
    assert ui.turntable_threshold_entry.instate(["readonly"])
    assert ui.turntable_angle_count.get() == "2"
    assert ui.turntable_angle_threshold.get() == "50"
    assert ui.turntable_threshold.get() == "1234" and ui.turntable_repeat_count.get() == "99"
    assert ui._operations.empty()
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert "CONF:ANGLE:SWEEP 0,0.05,0.05,0.05" in commands
    assert "CONF:ANGLE:INPUT IN1,1000" in commands
    assert "CONF:SEQ:REPEAT 2" in commands
    assert any(",IN1,50,IN2," in command for command in commands)
    ui.command("TRIG:START")
    assert "READ:ANGLE:POSITION?" in ui._operations.get_nowait()[1][2]
    ui.turntable_angle_speed.set("0.1")
    assert ui._configured_mode is None
    assert "100.0 Hz" in ui.turntable_angle_preview.get()
    ui.command("TRIG:START")
    assert ui._operations.empty()
    ui.turntable_angle_enabled.set(False)
    assert ui.turntable_repeat_entry.instate(["!readonly"])
    assert ui.turntable_threshold_entry.instate(["!readonly"])
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert not any("ANGLE" in command for command in commands)
    assert "CONF:SEQ:REPEAT 99" in commands
    assert any(",IN1,1234,IN2," in command for command in commands)


def test_angle_invalid_draft_never_sends_stop_or_partial_config(ui):
    select(ui, ui.turntable_page)
    ui.turntable_angle_enabled.set(True)
    ui.turntable_angle_step.set("0.7")
    ui.configure_mode(MODE_TURNTABLE)
    assert ui._operations.empty()
    assert ui.turntable_angle_count.get() == "—"
    ui.turntable_angle_enabled.set(False)
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert "CONF:SEQ:REPEAT 1" in commands


def test_angle_active_rate_helper_cannot_silently_override_scan(ui):
    select(ui, ui.turntable_page)
    ui.turntable_angle_enabled.set(True)
    complete_configuration(ui, MODE_TURNTABLE)
    ui.open_turntable_rate()
    ui.turntable_rate_value.set("50")
    ui.apply_turntable_rate()
    assert ui.turntable_threshold.get() == "1000"
    assert ui.turntable_angle_threshold.get() == "1000"
    assert "先关闭角度扫描" in ui.turntable_rate_preview.get()
    assert ui._configured_mode == MODE_TURNTABLE
    assert ui._operations.empty()


def test_angle_loopback_preset_preserves_scan_and_reopened_dialog(ui):
    select(ui, ui.turntable_page)
    ui.turntable_angle_enabled.set(True)
    ui.turntable_angle_stop.set("2")
    ui.apply_loopback_preset(MODE_TURNTABLE)
    assert ui.turntable_angle_count.get() == "3"
    assert "保留角度扫描" in ui.loopback_results[MODE_TURNTABLE].get()
    ui.open_turntable_angle()
    ui._turntable_angle_window.destroy()
    ui.open_turntable_angle()
    ui.turntable_angle_stop.set("3")
    assert ui.turntable_angle_count.get() == "4"
    assert ui._operations.empty()


def test_angle_dialog_fields_and_readback_fit(ui, tmp_path):
    select(ui, ui.turntable_page)
    ui.open_turntable_angle()
    window = ui._turntable_angle_window
    window.update()
    for widget in descendants(window):
        if not widget.winfo_ismapped():
            continue
        assert widget.winfo_rootx() >= window.winfo_rootx()
        assert widget.winfo_rooty() >= window.winfo_rooty()
        assert widget.winfo_rootx() + widget.winfo_width() <= window.winfo_rootx() + window.winfo_width()
        assert widget.winfo_rooty() + widget.winfo_height() <= window.winfo_rooty() + window.winfo_height()
        if isinstance(widget, (ttk.Entry, ttk.Button)):
            assert widget.winfo_height() >= widget.winfo_reqheight()
    button = next(widget for widget in descendants(window)
                  if isinstance(widget, ttk.Button) and "读取设备" in widget.cget("text"))
    button.invoke()
    assert ui._operations.get_nowait()[1][2] == ["READ:ANGLE:SWEEP?", "READ:ANGLE:INPUT?",
                                               "READ:ANGLE:SPEED?", "READ:ANGLE:POSITION?"]
    if os.environ.get("SEQUENCE_UI_SCREENSHOTS") == "1":
        from PIL import ImageGrab
        ImageGrab.grab(bbox=(window.winfo_rootx(), window.winfo_rooty(),
            window.winfo_rootx() + window.winfo_width(), window.winfo_rooty() + window.winfo_height()
        )).save(tmp_path / "angle-scan.png")


def test_turntable_manual_ready_and_threshold_draft_invalidate_start(ui):
    select(ui, ui.turntable_page)
    ui.turntable_ready_input.set("MANUAL")
    ui.update_mode_hint()
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert ui.next_buttons[MODE_TURNTABLE].instate(["!disabled"])
    assert any(",MANUAL,OUT4," in command for command in commands)
    ui.turntable_threshold.set("2000")
    ui.command("TRIG:START")
    assert ui._operations.empty()


def test_turntable_invalid_counter_boundary_queues_no_mutations(ui):
    select(ui, ui.turntable_page)
    ui.turntable_threshold.set(str(0xffffffdf))
    ui.configure_mode(MODE_TURNTABLE)
    assert ui._operations.empty()
    ui.turntable_threshold.set(str(0xffffffde))
    commands = complete_configuration(ui, MODE_TURNTABLE)
    assert any(",4294967262," in command for command in commands)


def test_counter_history_queries_only_newest_on_explicit_action(ui):
    select(ui, ui.turntable_page)
    ui.update_io("READ:SEQ:COUNTER?", "1,1,1,1000,1000,1,0,0,70,64,9,0")
    assert ui._operations.empty()
    ui.read_counter_history()
    assert ui._operations.get_nowait()[1][2] == ["READ:SEQ:COUNTER?"]
    ui.read_counter_history()
    assert ui._operations.empty()
    ui.update_io("READ:SEQ:COUNTER?", "1,1,1,1000,1000,1,0,0,71,64,9,0")
    assert ui._operations.get_nowait()[1][2] == ["READ:SEQ:COUNTER:HIST? 71"]
    assert ui._operations.empty()
    ui.update_io("READ:SEQ:COUNTER:HIST? 71", "71,1,1,10,6,10000,10002,7")
    assert "记录 71" in ui.counter_history.get()
    assert "请求时计数快照 10002" in ui.counter_history.get()
    ui.read_counter_history()
    ui._operations.get_nowait()
    ui.port.set("OTHER")
    assert ui.counter_history.get() == "历史记录：未读取"
    assert not ui._counter_history_pending


def test_counter_history_empty_run_and_bad_response_do_not_query_invalid_ordinal(ui):
    ui.read_counter_history()
    ui._operations.get_nowait()
    ui.update_io("READ:SEQ:COUNTER?", "1,1,1,1000,0,0,0,0,0,0,9,0")
    assert "尚无" in ui.counter_history.get()
    assert ui._operations.empty()
    ui.read_counter_history()
    ui._operations.get_nowait()
    ui.update_io("READ:SEQ:COUNTER?", "<timeout>")
    assert not ui._counter_history_pending
    assert ui._operations.empty()
    ui.update_io("READ:SEQ:COUNTER:HIST? 1", "<timeout>")
    assert "已被覆盖" in ui.counter_history.get()


def test_long_response_keeps_connection_controls_visible(ui):
    ui.geometry("1120x820")
    ui.log_exchange("READ:SEQ:LINK?", ",".join(["1234567890"] * 22))
    ui.update()
    assert ui.port_box.winfo_width() >= 180
    assert ui.port_box.winfo_ismapped()


@pytest.mark.parametrize("command", ["*RST", "CONF:SEQ:REPEAT 10"])
def test_manual_mutation_revokes_start_permission(ui, command):
    complete_configuration(ui, MODE_INDEPENDENT)
    ui.manual_command.set(command)
    ui.send_manual_command()
    assert command in ui._operations.get_nowait()[1][2]
    assert ui._configured_mode is None
    ui.command("TRIG:START")
    assert ui._operations.empty()


def test_manual_query_keeps_start_permission(ui):
    complete_configuration(ui, MODE_INDEPENDENT)
    ui.manual_command.set("TRIG:SEQ:NEXT?")
    ui.send_manual_command()
    assert "TRIG:SEQ:NEXT?" in ui._operations.get_nowait()[1][2]
    assert ui._configured_mode == MODE_INDEPENDENT


def test_reset_blocks_late_configuration_completion(ui):
    ui.configure()
    _, payload = ui._operations.get_nowait()
    backend, resource, _, mode, count, generation = payload
    ui.manual_command.set("*RST")
    ui.send_manual_command()
    ui._operations.get_nowait()
    ui._configuration_finished(mode, count, generation, (backend, resource))
    assert ui._device_mode is None
    assert ui._configured_mode is None
    ui.command("TRIG:START")
    assert ui._operations.empty()
