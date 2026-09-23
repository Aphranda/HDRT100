"""Single-board SCK dual-endpoint evidence stays raw and diagnostic-only."""
from argparse import Namespace

import pytest

from tools.calibration_ring_validate import calibration_marker_loopback as marker
from tools.calibration_ring_validate import calibration_sck_loopback as loop


PINS = (25, 28, 29, 24)
DELAYS = (0, 13, 19, 32)
SM_START = 6
ORIGIN_LEAD = 64
ORIGIN_HIGH = 64
LAUNCH_GUARD = 128
CAPTURE_WORDS = 2048


def capture_fixture(codebook=1, offset=0, fault=False):
    waveform, vector = marker.marker_raw_waveform(
        codebook_id=codebook, epoch=7, source_node=0)
    stimulus = ([0] * ORIGIN_LEAD + [1] * ORIGIN_HIGH +
                [0] * (LAUNCH_GUARD + offset) + waveform + [0] * 64)
    samples = []
    for index in range(CAPTURE_WORDS * 4):
        byte = 0
        for pin, delay in zip(PINS, DELAYS):
            source = index - SM_START - delay
            value = stimulus[source] if not fault and 0 <= source < len(stimulus) else 0
            byte |= value << (pin - 24)
        samples.append(byte)
    status = dict.fromkeys(loop.FIELDS, 0)
    status.update(version=1, state=3 if fault else 2, scope=2,
                  flags=119 if fault else 127, reject_reason=1 if fault else 0,
                  epoch=7, codebook=codebook, sample_period_ns=4,
                  marker_samples=vector.raw_samples, tx_words=(len(stimulus) + 31) // 32,
                  tx_drained=1, requested_words=CAPTURE_WORDS,
                  produced_words=CAPTURE_WORDS, edge_mask=0 if fault else 15,
                  capture_generation=42, offset_samples=offset,
                  origin_lead_samples=ORIGIN_LEAD, origin_high_samples=ORIGIN_HIGH,
                  launch_guard_samples=LAUNCH_GUARD)
    for endpoint, delay in enumerate(DELAYS, 1):
        ticks = 0 if fault else (SM_START + ORIGIN_LEAD + ORIGIN_HIGH + delay) * 4
        status[f't{endpoint}_lo'] = status[f't{endpoint}_ns'] = ticks
    return status, samples, vector


def pack(samples):
    return [sum(value << (8 * byte) for byte, value in enumerate(samples[index:index + 4]))
            for index in range(0, len(samples), 4)]


def evaluate(status, samples, *, codebook=1, offset=0, fault=False):
    return loop.evaluate(status, pack(samples), epoch=7, codebook=codebook,
                         requested_words=CAPTURE_WORDS, offset_samples=offset,
                         fault_idle_low=fault)


@pytest.mark.parametrize('codebook', range(4))
@pytest.mark.parametrize('offset', [-10, 0, 10])
def test_four_pin_complete_codeword_and_physical_timing(codebook, offset):
    status, samples, _ = capture_fixture(codebook, offset)
    result = evaluate(status, samples, codebook=codebook, offset=offset)
    assert result['passed'], result['failures']
    assert result['forward_path_ns'] == result['return_path_ns'] == 52
    assert result['responder_residence_ns'] == 24
    assert result['origin_rtt_ns'] == 128
    assert result['raw_times_ns'] == [536, 588, 612, 664]
    assert result['full_codeword']['passed']
    assert not result['production_training_certified']


@pytest.mark.parametrize('field,value', [
    ('version', 2), ('scope', 1), ('epoch', 8), ('codebook', 0),
    ('offset_samples', 1), ('capture_generation', 0),
    ('origin_lead_samples', 63), ('origin_high_samples', 63),
    ('launch_guard_samples', 127), ('requested_words', 1024),
    ('produced_words', 2047), ('marker_samples', 0), ('tx_words', 1),
    ('tx_drained', 0), ('tx_dma_remaining', 1), ('edge_mask', 7),
    ('flags', 119), ('state', 3), ('reject_reason', 2),
])
def test_snapshot_configuration_or_capture_fault_fails_closed(field, value):
    status, samples, _ = capture_fixture()
    status[field] = value
    assert not evaluate(status, samples)['passed']


@pytest.mark.parametrize('pin', PINS)
@pytest.mark.parametrize('bit', [1, 40, 90, 200, 320])
def test_corrupt_header_payload_or_eof_is_not_accepted(pin, bit):
    status, samples, vector = capture_fixture()
    delay = dict(zip(PINS, DELAYS))[pin]
    start = (SM_START + ORIGIN_LEAD + ORIGIN_HIGH + LAUNCH_GUARD + delay +
             bit * 2 * vector.half_chip_samples)
    for index in range(start, start + 2 * vector.half_chip_samples):
        samples[index] ^= 1 << (pin - 24)
    assert not evaluate(status, samples)['passed']


def test_raw_timestamp_and_truncated_data_cannot_pass():
    status, samples, _ = capture_fixture()
    status['t4_ns'] += 4
    assert not evaluate(status, samples)['passed']
    status, samples, _ = capture_fixture()
    assert not evaluate(status, samples[:-4])['passed']


@pytest.mark.parametrize('pin', [28, 29, 24])
def test_return_path_guard_contains_no_unexpected_high(pin):
    status, samples, _ = capture_fixture()
    samples[220] |= 1 << (pin - 24)
    assert not evaluate(status, samples)['passed']


@pytest.mark.parametrize('pin', PINS)
def test_every_channel_has_complete_idle_low_tail(pin):
    status, samples, _ = capture_fixture()
    samples[7600] |= 1 << (pin - 24)
    assert not evaluate(status, samples)['passed']


def test_sck_trial_does_not_depend_on_toggling_cs():
    status, samples, _ = capture_fixture()
    samples[220] |= 1 << (26 - 24)
    assert not evaluate(status, samples)['passed']


def test_origin_requires_complete_leading_low_capture():
    status, samples, _ = capture_fixture()
    samples = samples[60:] + [0] * 60
    for endpoint in range(1, 5):
        status[f't{endpoint}_lo'] -= 240
        status[f't{endpoint}_ns'] -= 240
    result = evaluate(status, samples)
    assert not result['passed']
    assert any('origin_lead_truncated' in failure for failure in result['failures'])


def test_idle_low_is_only_an_expected_missing_edge_result():
    status, samples, _ = capture_fixture(fault=True)
    result = evaluate(status, samples, fault=True)
    assert result['passed'], result['failures']
    assert not result['production_training_certified']
    samples[100] |= 1 << (PINS[0] - 24)
    assert not evaluate(status, samples, fault=True)['passed']


@pytest.mark.parametrize('field,value', [('reject_reason', 2), ('tx_drained', 0),
                                        ('flags', 63), ('edge_mask', 1)])
def test_idle_low_rejects_unrelated_resource_faults(field, value):
    status, samples, _ = capture_fixture(fault=True)
    status[field] = value
    assert not evaluate(status, samples, fault=True)['passed']


def test_status_width_signed_offset_and_u64():
    status, _, _ = capture_fixture(offset=-10)
    status['t1_hi'] = 1
    parsed = loop.parse_status(','.join(str(status[field]) for field in loop.FIELDS))
    assert len(loop.FIELDS) == 28
    assert parsed['offset_samples'] == -10
    assert parsed['t1_ns'] == (1 << 32) + 536
    with pytest.raises(ValueError):
        loop.parse_status('1,2,3')


@pytest.mark.parametrize('failure', [None, 'start_offset', 'chunk_epoch', 'chunk_offset',
                                    'chunk_count', 'generation', 'status_timeout'])
def test_trial_generation_bound_chunks_and_cleanup(failure):
    status, samples, _ = capture_fixture(offset=-10)
    words = pack(samples)
    idle = dict.fromkeys(loop.FIELDS, 0)
    idle['version'] = 1
    active = False
    downloads = []
    calls = []

    def exchange(command):
        nonlocal active
        calls.append(command)
        normalized = command.upper().replace('CALIBRATION', 'CAL')
        if normalized.endswith(':STOP'):
            active = False
            return 'OK'
        if ':START ' in normalized:
            assert command.split()[1] == '1,7,2048,-10,0'
            active = True
            return '1,7,2048,0,0' if failure == 'start_offset' else '1,7,2048,-10,0'
        if ':DATA? ' in normalized:
            epoch, offset, count, generation = map(int, command.split()[1].split(','))
            assert generation == 42
            downloads.append(offset)
            return ','.join(map(str, [epoch + (failure == 'chunk_epoch'), CAPTURE_WORDS,
                offset + (failure == 'chunk_offset'), count - (failure == 'chunk_count')] +
                words[offset:offset + count]))
        assert normalized == 'READ:CAL:SCK:LOOP?'
        if active and failure == 'status_timeout':
            raise RuntimeError('SCPI timeout: READ:CAL:SCK:LOOP?')
        source = dict(status if active else idle)
        if active and downloads and failure == 'generation':
            source['capture_generation'] += 1
        return ','.join(str(source[field]) for field in loop.FIELDS)

    args = Namespace(capture_timeout=1., capture_words=CAPTURE_WORDS,
                     codebook=1, offset_samples=-10, fault_idle_low=False)
    result = loop.run_trial(exchange, args, 7)
    assert result['passed'] is (failure is None), result.get('error')
    assert result['cleanup']['state'] == 0
    assert sum(command.endswith(':STOP') for command in calls) == 2
    if failure == 'status_timeout':
        assert result['error'] == 'RuntimeError: SCPI timeout: READ:CAL:SCK:LOOP?'
        assert not downloads
        assert calls.count('READ:CAL:SCK:LOOP?') == 3
    if failure is None:
        assert downloads == list(range(0, CAPTURE_WORDS, 32))
        assert result['capture']['words'] == words
