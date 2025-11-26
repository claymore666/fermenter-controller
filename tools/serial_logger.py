#!/usr/bin/env python3
"""
Serial Logger - Logs ESP32 serial output to timestamped file
"""

import serial
import sys
import os
from datetime import datetime

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    # Create logs directory
    log_dir = os.path.join(os.path.dirname(__file__), "..", "logs")
    os.makedirs(log_dir, exist_ok=True)

    # Timestamped log file
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(log_dir, f"serial_{timestamp}.log")

    print(f"Logging {port} @ {baud} baud to: {log_file}")
    print("Press Ctrl+C to stop\n")

    try:
        ser = serial.Serial(port, baud, timeout=1)
        with open(log_file, "w") as f:
            while True:
                line = ser.readline()
                if line:
                    text = line.decode("utf-8", errors="replace").rstrip()
                    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    log_line = f"[{ts}] {text}"
                    print(log_line)
                    f.write(log_line + "\n")
                    f.flush()
    except KeyboardInterrupt:
        print(f"\nLog saved to: {log_file}")
    except serial.SerialException as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
