import pytest

from tools.flash_park_timeout_hil_validate.flash_park_timeout_hil_validate import (
    core1_heartbeat,
)


def test_core1_heartbeat_parses_online_core() -> None:
    assert core1_heartbeat("1,0,938,7") == 938


@pytest.mark.parametrize("response", ["0,0,938", "1,0", "invalid"])
def test_core1_heartbeat_rejects_invalid_status(response: str) -> None:
    with pytest.raises(ValueError, match="invalid core1 status"):
        core1_heartbeat(response)
