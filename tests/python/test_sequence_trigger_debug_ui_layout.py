"""Exercise the real Tk controls without opening serial/VISA or running OTA."""
import os
import tkinter as tk
from tkinter import ttk

import pytest

from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
    MODE_INDEPENDENT, MODE_RJ45, SequenceUi,
)


@pytest.fixture
def ui(monkeypatch, capsys):
    monkeypatch.setattr(SequenceUi, "refresh_ports", lambda *args, **kwargs: None)
    monkeypatch.setattr(SequenceUi, "_operation_loop", lambda self: None)
    try:
        # Windows Tcl initializes native file descriptors; pytest's fd capture
        # can interfere with opening init.tcl when creating successive roots.
        with capsys.disabled():
            window = SequenceUi()
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
        "独立 SP8T 序列", "RJ45 物理回环", "手动 SP8T", "设备维护"]
    select(ui, ui.loopback_page)
    texts = [widget.cget("text") for widget in descendants(ui.loopback_page)
             if isinstance(widget, ttk.Button)]
    assert "下一步" in texts and "软件单步" not in texts
    assert ui.gateway_group.winfo_ismapped()
    assert not ui.source_box.winfo_ismapped()
    assert not ui.status_mode_box.winfo_ismapped()
    for page in (ui.independent_page, ui.loopback_page, ui.manual_switch_page, ui.maintenance_page):
        select(ui, page)
        assert ui.io_panel.winfo_ismapped() and ui.output.winfo_ismapped()
        assert ui.connection_panel.winfo_ismapped()
    assert ui._operations.empty()


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
        assert "CONF:SEQ:LINK LOOPBACK,2,3,IN1,OUT4,10,5000,RIS" in commands


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
    ui.source.set("BUS")
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
    for page in (ui.independent_page, ui.loopback_page, ui.manual_switch_page, ui.maintenance_page):
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
