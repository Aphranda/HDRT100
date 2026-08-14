from __future__ import annotations

from argparse import Namespace

from tools.biss_wavegen import biss_wave_validate, biss_wavegen


def _wave_args(**overrides) -> Namespace:
    defaults = {
        "clock_hz": 1_000_000,
        "frame_bits": 16,
        "position_offset": 4,
        "position_bits": 8,
        "idle_bits": 2,
        "anchor_offset": 0,
        "anchor_bits": 2,
        "anchor_value": 0x2,
        "error_bit": -1,
        "warning_bit": -1,
        "crc_offset": 12,
        "crc_bits": 4,
        "crc_cover_offset": 0,
        "crc_cover_bits": 12,
        "crc_polynomial": 0x3,
        "crc_init": 0,
        "crc_xor": 0,
        "crc_invert": False,
    }
    defaults.update(overrides)
    return Namespace(**defaults)


def test_biss_wavegen_round_trips_generated_frames() -> None:
    args = _wave_args()
    positions = [10, 20, 30]
    frames = [biss_wavegen.build_frame(args, position) for position in positions]
    rows = biss_wavegen.generate_rows(args, frames)

    profile = biss_wave_validate.Profile(
        frame_bits=args.frame_bits,
        position_offset=args.position_offset,
        position_bits=args.position_bits,
        modulo=256,
        target=20,
        anchor_offset=args.anchor_offset,
        anchor_bits=args.anchor_bits,
        anchor_mask=0x3,
        anchor_value=args.anchor_value,
        error_bit=args.error_bit,
        warning_bit=args.warning_bit,
        status_gate="ignore",
        crc_offset=args.crc_offset,
        crc_bits=args.crc_bits,
        crc_cover_offset=args.crc_cover_offset,
        crc_cover_bits=args.crc_cover_bits,
        crc_polynomial=args.crc_polynomial,
        crc_init=args.crc_init,
        crc_xor=args.crc_xor,
        crc_invert=args.crc_invert,
        sample_edge="rising",
    )

    decoded, decode_failures = biss_wave_validate.decode_frames(rows, profile)
    report, validation_failures = biss_wave_validate.validate_frames(decoded, profile, positions, frames)

    assert decode_failures == []
    assert validation_failures == []
    assert decoded == frames
    assert report["positions"] == positions
    assert report["crossing_count"] == 1


def test_biss_crossed_position_handles_wraparound() -> None:
    assert biss_wave_validate.crossed_position(250, 5, 2, 256)
    assert biss_wave_validate.crossed_position(10, 20, 15, 256)
    assert not biss_wave_validate.crossed_position(10, 20, 25, 256)
