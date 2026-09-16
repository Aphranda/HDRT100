"""Physical-return delivery uses the complete production TDMA adapter."""
from pathlib import Path
import pytest

from test_tdma_diagnostic_burst import compile_run

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("capacity", [2, 6, 8])
def test_physical_local_return(capacity, tmp_path):
    names = ["tdma_pio_spi_ring_adapter", "tdma_adapter_comm_fsm", "tdma_flight_fifo",
             "tdma_flight_engine", "tdma_flight_overlay", "tdma_overlay_prepare", "tdma_rx_prepare",
             "tdma_receive_health", "tdma_process_image_map", "tdma_ring_runtime",
             "tdma_transport_frame", "tdma_profile"]
    compile_run(tmp_path, "local_return", [ROOT / "tests/unit/test_tdma_local_return.c",
                *[ROOT / f"components/tdma/src/{name}.c" for name in names]],
                includes=[f"-DPROJECT_NODE_CAPACITY={capacity}"])
