from __future__ import annotations

import pytest

from tools.flash_scratch_validate.flash_scratch_validate import (
    CONFIRM_TOKEN,
    parse_validation,
)


def test_parse_validation_requires_all_evidence_fields():
    result = parse_validation("1,1,0x12345678,0x12345678,1,1,1")
    assert result["expected_hash"] == 0x12345678
    assert result["readback_hash"] == 0x12345678
    assert result["hash_match"] == 1
    assert result["erased_ok"] == 1


def test_parse_validation_rejects_truncated_response():
    with pytest.raises(ValueError, match="fields"):
        parse_validation("1,1,1")


def test_confirmation_token_is_explicit_and_stable():
    assert CONFIRM_TOKEN == int("53435254", 16)
