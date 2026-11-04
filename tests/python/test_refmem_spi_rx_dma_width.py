"""Guard the RX DMA lane width: one byte per PIO FIFO entry, both DMA sites.

The PIO RX program pushes one byte per FIFO entry (8 `in pins,1` then an
explicit `push`).  The DMA therefore transfers one byte per entry.  The buffer
must be `uint8_t` and BOTH capture paths (`capture_words` and `receive_begin`)
must use `DMA_SIZE_8`; changing only one site makes the 32-bit DMA write four
bytes per entry into a one-byte lane and corrupts adjacent `.bss`.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components" / "distributed_refmem" / "src" / "refmem_spi_physical_adapter.c"
FRAME = ROOT / "components" / "distributed_refmem" / "inc" / "refmem_sync_frame.h"


def test_rx_dma_lane_is_byte_wide() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    assert "static uint8_t s_refmem_spi_rx_dma_words[" in text
    assert "static uint32_t s_refmem_spi_rx_dma_words[" not in text
    assert text.count("DMA_SIZE_8") == 2
    assert "DMA_SIZE_32" not in text
    assert "DMA_SIZE_16" not in text
    # The three consumers index the lane directly; a mask on a DMA read would
    # reintroduce the 32-bit word assumption (and silently drop bytes on a
    # wrong-width DMA).  Other header-byte masks are unrelated to this lane.
    for line in text.splitlines():
        if "s_refmem_spi_rx_dma_words[" in line:
            assert "& 0xFFu" not in line


def test_rx_dma_capacity_matches_wire_bound() -> None:
    frame = FRAME.read_text(encoding="utf-8")
    # 4-byte packet header + 36-byte frame header + 256-byte payload = 296.
    assert "REFTMEM_SYNC_FRAME_HEADER_SIZE 36u" not in frame  # guard typo
    assert "REFMEM_SYNC_FRAME_HEADER_SIZE 36u" in frame
    assert "REFMEM_SYNC_FRAME_PAYLOAD_MAX 256u" in frame
    source = SOURCE.read_text(encoding="utf-8")
    assert "#define REFMEM_SPI_PACKET_HEADER_SIZE 4u" in source
