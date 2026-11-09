"""Framing rules for result-returning set commands (2026-11-09 TRN-03B)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.common.scpi.scpi_serial import scpi_response_matches_command  # noqa: E402


def test_apply_refusal_is_surfaced():
    # A refused APPLY answers -200,"<info>"; returning it lets the caller
    # report the firmware reason instead of a misleading <timeout>.
    assert scpi_response_matches_command(
        "SYST:TDMA:OPMode:APPLy", '-200,"Execution error"')


def test_link_echo_is_consumed():
    # CAL:TRAIN:STAGe:LINK is a set command whose 2-field echo has no
    # terminator; declaring the field count consumes it in-transaction.
    assert scpi_response_matches_command("CAL:TRAIN:STAGe:LINK", "2,1")


def test_apply_noise_is_still_rejected():
    assert not scpi_response_matches_command(
        "SYST:TDMA:OPMode:APPLy", "boot: starting services")
