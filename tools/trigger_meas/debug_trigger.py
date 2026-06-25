"""Quick debug: check trigger status and try different source pins."""
from __future__ import annotations
import serial, time, sys

def scpi(ser, cmd, delay=0.2):
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
    ser = serial.Serial(port, 115200, timeout=1.0)
    time.sleep(0.5)
    ser.read(ser.in_waiting or 1)

    print("=== 1. Firmware Info ===")
    print(f"  BUILD: {scpi(ser, 'SYST:FW:BUILD?')}")

    print("\n=== 2. Initial Status ===")
    print(f"  STAT:TRIG?  → {scpi(ser, 'STAT:TRIG?')}")
    print(f"  TRIG:SOUR?  → {scpi(ser, 'TRIG:SOUR?')}")
    print(f"  TRIG:EDGE?  → {scpi(ser, 'TRIG:EDGE?')}")
    print(f"  TRIG:GATE?  → {scpi(ser, 'TRIG:GATE?')}")

    print("\n=== 3. ARM Test ===")
    scpi(ser, "TRIG:DIS", 0.3)
    time.sleep(0.2)
    ser.read(ser.in_waiting or 1)

    scpi(ser, "TRIG:SEQ:LENG 64")
    scpi(ser, "TRIG:SEQ:WIDT 4")
    scpi(ser, "TRIG:MODE 1", 0.3)
    result = scpi(ser, "TRIG:ARM", 0.3)
    print(f"  ARM result: {result}")

    time.sleep(0.2)
    stat1 = scpi(ser, "STAT:TRIG?")
    print(f"  After ARM:  {stat1}")

    print("\n=== 4. Wait for edges (2 seconds) ===")
    time.sleep(2.0)
    stat2 = scpi(ser, "STAT:TRIG?")
    print(f"  After 2s:   {stat2}")

    # Parse trigger_count
    for label, s in [("Before", stat1), ("After", stat2)]:
        parts = s.replace('"', "").split(",")
        if len(parts) >= 8:
            print(f"  {label}: trigger_count={parts[6]}  state={parts[1]}  pin={parts[2]}")

    print("\n=== 5. Frequency Measurement ===")
    freq = scpi(ser, "MEAS:FREQ? 500", delay=0.6)
    print(f"  MEAS:FREQ? 500 → {freq} Hz")

    print("\n=== 6. DISARM ===")
    scpi(ser, "TRIG:DIS", 0.3)
    print(f"  Final: {scpi(ser, 'STAT:TRIG?')}")

    ser.close()

if __name__ == "__main__":
    main()
