import sys, time
from pathlib import Path
sys.path.insert(0, r"D:\Work\PICO\RP2350_TRIG\tools")
from scpi_common.scpi_serial import open_serial_port, read_serial_line_idle

port = "COM5"
outdir = Path(r"D:\Work\PICO\RP2350_TRIG\build-rtos-multicore-smoke\tdma_trace_readback")
outdir.mkdir(parents=True, exist_ok=True)

with open_serial_port(port, 115200, 2.0, 1.0) as ser:
    def q(cmd, t=8.0):
        ser.reset_input_buffer()
        ser.write((cmd + "\n").encode("ascii")); ser.flush()
        end = time.monotonic() + t
        while time.monotonic() < end:
            line = read_serial_line_idle(ser, end)
            if line and not line.startswith("["):
                return line.strip()
        return "<timeout>"

    def read_file(path, size):
        data = bytearray()
        offset = 0
        while offset < size:
            chunk = min(128, size - offset)
            r = q('SYSTem:STORage:FILE:READ? "%s",%d,%d' % (path, offset, chunk))
            parts = r.split(",")
            if len(parts) < 10 or parts[0].strip('"') != "OK":
                print(path, "read failed at", offset, ":", r[:150])
                return None
            returned = int(parts[4])
            hexdata = ",".join(parts[9:]).strip('"')
            raw = bytes.fromhex(hexdata)
            data.extend(raw[:returned])
            offset += returned
            if returned < chunk:
                break
        return bytes(data)

    for name, size in [("fault_000001.bin", 740), ("fault_000001.idx", 144),
                       ("fault_000007.bin", 836), ("fault_000007.idx", 144),
                       ("fault_000006.bin", 1060), ("fault_000006.idx", 145)]:
        data = read_file("/traces/fault/" + name, size)
        if data is not None:
            (outdir / name).write_bytes(data)
            print(name, "saved", len(data), "bytes")
