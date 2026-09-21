"""Layout compatibility checks for the classified SD trace decoder."""

from tools.domains.storage.sd_trace_decode.sd_trace_decode import decode_trace
from tools.sd_trace_decode.sd_trace_decode import decode_trace as legacy_decode_trace


def test_legacy_import_forwards_to_classified_implementation():
    assert legacy_decode_trace is decode_trace
