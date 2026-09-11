"""Instruction-word model of the flight DATA SM; DMA latency is not modeled."""
UINT32 = (1 << 32) - 1
LIVE, INVERT, ZERO, ONE = 0xA026, 0xA02E, 0xE020, 0xE021

def bytes_to_bits(data):
    return [(byte >> (7 - bit)) & 1 for byte in data for bit in range(8)]


def reverse32(value):
    value = ((value & 0x55555555) << 1) | ((value >> 1) & 0x55555555)
    value = ((value & 0x33333333) << 2) | ((value >> 2) & 0x33333333)
    value = ((value & 0x0F0F0F0F) << 4) | ((value >> 4) & 0x0F0F0F0F)
    return int.from_bytes(value.to_bytes(4, "little"), "big")


class Machine:
    def __init__(self, physical, tokens, *, program, entry, wrap, final, period=25, high=12, rx_drop=False):
        assert len(physical) == len(tokens) and len(tokens) % 2 == 0
        self.program, self.entry, self.wrap, self.final = program, entry, wrap, final
        self.input = bytes_to_bits(physical[0]) + bytes_to_bits(physical[1])
        self.frame_bits = len(physical[0]) * 8
        assert len(physical[0]) == len(physical[1])
        self.period, self.high = period, high
        self.starts = [100, 100 + self.frame_bits * period + 200]
        flat = tokens[0] + tokens[1]
        self.words = [(flat[i] << 16) | flat[i + 1] for i in range(0, len(flat), 2)]
        self.word_cursor = 1
        self.osr, self.out_count = self.words[0], 0  # owner primes at ARM
        self.x = self.y = self.isr = self.time = 0
        self.pc = entry
        self.pending = None
        self.output, self.samples, self.pushes, self.irqs = [], [], [], []
        self.waits = []
        self.rx_drop = rx_drop

    def position(self):
        frame = int(self.time >= self.starts[1])
        offset = self.time - self.starts[frame]
        active = 0 <= offset < self.frame_bits * self.period
        return frame, offset, active

    def wait(self, pin, level):
        frame, offset, active = self.position()
        if pin == 0:  # CS high only at physical frame end
            assert level == 1
            if active:
                self.time = self.starts[frame] + self.frame_bits * self.period
            return
        assert pin == 1
        if not active:
            if self.time < self.starts[frame]:
                self.time = self.starts[frame]
            else:
                assert frame == 0, "unexpected request for a third frame"
                self.time = self.starts[1]
        frame, offset, _ = self.position()
        phase = offset % self.period
        if level and phase >= self.high:
            self.time += self.period - phase
        elif not level and phase < self.high:
            self.time += self.high - phase

    def run(self):
        for _ in range(len(self.input) * 15 + 100):
            if len(self.irqs) == 2:
                return self
            # OUT autopull can refill during the following non-OUT cycles.
            # This model assumes DMA data is already available; arbitration
            # latency and FIFO starvation require a separate admission/HIL gate.
            if self.out_count >= 32 and self.word_cursor < len(self.words):
                self.osr = self.words[self.word_cursor]
                self.word_cursor += 1
                self.out_count = 0
            instruction = self.program[self.pc] if self.pending is None else self.pending
            injected = self.pending is not None
            if injected:
                self.pending = None
            else:
                self.pc = self.entry if self.pc == self.wrap else self.pc + 1
            major = instruction >> 13
            delay = (instruction >> 8) & 31
            arg = instruction & 255
            if major == 0:  # JMP
                condition, target = arg >> 5, arg & 31
                if condition == 0:
                    self.pc = target
                elif condition == 4:
                    branch = self.y != 0
                    self.y = (self.y - 1) & UINT32
                    if branch:
                        self.pc = target
                else:
                    raise AssertionError(("jmp", condition))
            elif major == 1:
                assert (arg >> 5) & 3 == 0
                self.wait(arg & 31, arg >> 7)
                if arg & 31 == 1 and arg >> 7:
                    self.waits.append(self.time)
            elif major == 2:  # IN right, manual PUSH
                source, count = arg >> 5, arg & 31 or 32
                if source == 0:
                    frame, offset, active = self.position()
                    assert active
                    ordinal = offset // self.period
                    bit = self.input[frame * self.frame_bits + ordinal]
                    self.samples.append((frame, ordinal, self.time, bit))
                else:
                    assert source == 3
                    bit = 0
                self.isr = (self.isr >> count) | (bit << (32 - count))
            elif major == 3:
                assert arg == 0xF0 and self.out_count <= 16
                token = self.osr >> 16
                assert token in (LIVE, INVERT, ZERO, ONE, self.final), hex(token)
                self.osr = (self.osr << 16) & UINT32
                self.out_count += 16
                self.pending = token
            elif major == 4:
                assert arg == 0, "PUSH must be nonblocking"
                if not self.rx_drop:
                    self.pushes.append(self.isr)
                self.isr = 0
            elif major == 5:
                dest, operation, source = arg >> 5, (arg >> 3) & 3, arg & 7
                value = {1: self.x, 6: self.isr}[source]
                if operation == 2:
                    value = reverse32(value)
                elif operation == 1:
                    value ^= UINT32
                else:
                    assert operation == 0
                if dest == 0:
                    self.output.append((self.time, value & 1))
                elif dest == 1:
                    self.x = value
                else:
                    assert dest == 6
                    self.isr = value
            elif major == 6:
                assert arg == 3
                self.irqs.append(self.time)
            elif major == 7:
                dest, value = arg >> 5, arg & 31
                if dest == 1:
                    self.x = value
                else:
                    assert dest == 2
                    self.y = value
            else:
                raise AssertionError(hex(instruction))
            self.time += 1 + delay
        raise AssertionError("instruction limit")

