"""Loopback test: GPIO22 (CLK_OUT) → jumper → GPIO16 (TRIG_IN)

Hardware setup:
  Jumper GPIO22 (physical pin 27) → GPIO16 (physical pin 21)

Usage:
  python tools/trigger_meas/loopback_test.py COM4 [freq_hz]
"""

from __future__ import annotations
import serial, time, sys

def scpi(ser, cmd, delay=0.25):
    ser.write((cmd + "\n").encode())
    time.sleep(delay)
    resp = []
    for _ in range(8):
        raw = ser.readline()
        if not raw: break
        line = raw.decode(errors="replace").strip()
        if not line or line.startswith("["): continue
        resp.append(line)
    return resp[0] if resp else ""

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    freq = sys.argv[2] if len(sys.argv) > 2 else "1000"
    ser = serial.Serial(port, 115200, timeout=1.0)
    time.sleep(0.5)
    ser.read(ser.in_waiting or 1)

    print(f"=== Loopback Test: GPIO22 → GPIO16 ===")
    print(f"Jumper: physical pin 27 → physical pin 21\n")

    # 1. Start clock output on GPIO22
    print(f"[1] Starting {freq}Hz clock on GPIO22...")
    scpi(ser, f"OUTP:CLOC:FREQ {freq}")
    r = scpi(ser, "OUTP:CLOC:STAT ON")
    print(f"    CLOC:STAT ON → {r}")
    time.sleep(0.1)

    # 2. Arm trigger on GPIO16
    print("[2] Arming trigger on GPIO16...")
    scpi(ser, "TRIG:DIS", 0.3)
    time.sleep(0.2)
    ser.read(ser.in_waiting or 1)

    scpi(ser, "TRIG:SEQ:LENG 64")
    scpi(ser, "TRIG:SEQ:WIDT 4")
    scpi(ser, "TRIG:MODE 1", 0.3)
    r = scpi(ser, "TRIG:ARM", 0.3)
    print(f"    ARM → {r}")

    # 3. Check trigger count changing
    print("[3] Checking trigger activity...")
    s0 = scpi(ser, "STAT:TRIG?")
    time.sleep(1.0)
    s1 = scpi(ser, "STAT:TRIG?")

    for label, s in [("t=0s", s0), ("t=1s", s1)]:
        parts = s.replace('"', "").split(",")
        if len(parts) >= 8:
            print(f"    {label}: trig_count={parts[6]}  seq_idx={parts[3]}  state={parts[1]}")

    # 4. Measure frequency
    print(f"[4] Measuring frequency...")
    freq_hz = scpi(ser, "MEAS:FREQ? 1000", delay=1.1)
    print(f"    MEAS:FREQ? 1000 → {freq_hz} Hz")
    if freq_hz.isdigit() and int(freq_hz) > 0:
        err = (int(freq_hz) - int(freq)) / int(freq) * 100
        print(f"    Expected: {freq} Hz  Measured: {freq_hz} Hz  Error: {err:.2f}%")

    # 5. Cleanup
    print("[5] Cleanup...")
    scpi(ser, "TRIG:DIS", 0.3)
    scpi(ser, "OUTP:CLOC:STAT OFF")
    print(f"    Done.")

    ser.close()

if __name__ == "__main__":
    main()
