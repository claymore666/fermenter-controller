# C++ Fermentation Controller - Python MODBUS Simulator Integration

This document describes how to integrate the C++ fermentation controller with the Python MODBUS simulator for testing without hardware.

## Architecture

```
┌─────────────────────┐     Virtual Serial Port     ┌─────────────────────┐
│   C++ Controller    │ ◄─────────────────────────► │  Python Simulator   │
│                     │                             │                     │
│  SerialModbus       │     vport_client            │  pymodbus Server    │
│  (MODBUS RTU)       │ ◄──────────────────────────►│  vport_simulator    │
└─────────────────────┘                             └─────────────────────┘
```

## Prerequisites

- socat (for virtual serial ports)
- Python 3.10+ with pymodbus
- PlatformIO

## Quick Start

### 1. Start Virtual Serial Port

```bash
cd modbus_simulator
./setup_vport.sh start
```

This creates:
- `vport_simulator` - Python simulator connects here
- `vport_client` - C++ controller connects here

### 2. Start Python MODBUS Simulator

```bash
cd modbus_simulator
source venv/bin/activate
python main.py
```

Web UI available at http://localhost:8080

### 3. Build and Run C++ Controller

```bash
# Build
source venv/bin/activate
pio run -e simulator

# Run with MODBUS_PORT environment variable
MODBUS_PORT=/dev/pts/X .pio/build/simulator/program config/integration_test.json
```

Replace `/dev/pts/X` with the actual pts device shown by `./setup_vport.sh status`.

## Configuration

### C++ Config (config/integration_test.json)

```json
{
  "modbus": {
    "devices": [
      {
        "address": 1,
        "type": "analog_8ch",
        "name": "Waveshare Analog",
        "registers": [
          {"name": "fermenter_1_pressure", "reg": 0, "scale": 0.0000488, "priority": "high"},
          {"name": "fermenter_2_pressure", "reg": 1, "scale": 0.0000488, "priority": "high"}
        ]
      }
    ]
  }
}
```

**Scale Factor**: `0.0000488 = 1.6 / 32767` converts MODBUS value (0-32767) to bar (0-1.6).

### Python Simulator Config (.env)

```ini
MODBUS_PORT=./vport_simulator
MODBUS_BAUDRATE=57600
MODBUS_SLAVE_ID=1
```

**Important**: C++ and Python must use the same baud rate (57600).

## MODBUS Register Mapping

| Channel | Sensor | Register Address | Wire Address |
|---------|--------|------------------|--------------|
| 1 | Pressure 1 | 0x0000 | 0 |
| 2 | Pressure 2 | 0x0001 | 1 |

### pymodbus Addressing Fix

pymodbus uses 1-based addressing internally. When calling `setValues()`, use `i+1`:

```python
def update_registers(self) -> None:
    for i, sensor in enumerate(self.sensors):
        state = sensor.get_state()
        # Use i+1 for 1-based MODBUS addressing in pymodbus
        self.input_registers.setValues(i + 1, [state['modbus_value']])
```

This maps:
- Channel 1 → `setValues(1, ...)` → wire address 0
- Channel 2 → `setValues(2, ...)` → wire address 1

## Serial MODBUS Implementation

The C++ `SerialModbus` class (`include/hal/simulator/serial_modbus.h`) provides:

- MODBUS RTU protocol over serial port
- Function code 0x04 (Read Input Registers)
- Function code 0x06 (Write Single Register)
- Function code 0x10 (Write Multiple Registers)
- CRC-16 calculation

### Usage

```cpp
#include "hal/simulator/serial_modbus.h"

hal::simulator::SerialModbus modbus;
modbus.open_port("/dev/pts/3", 57600);

uint16_t values[2];
if (modbus.read_holding_registers(1, 0, 2, values)) {
    // values[0] = register 0
    // values[1] = register 1
}
```

### Environment Variable

Set `MODBUS_PORT` to enable serial MODBUS:

```bash
export MODBUS_PORT=/dev/pts/3
.pio/build/simulator/program config/integration_test.json
```

Without `MODBUS_PORT`, the simulator uses in-memory test values.

## Troubleshooting

### Both values are 0

- Check that Python simulator is running
- Verify virtual serial port is active: `./setup_vport.sh status`
- Set pressure values in web UI (http://localhost:8080)

### Register 1 always 0

This was caused by pymodbus 1-based addressing. Ensure `update_registers()` uses `i+1` for `setValues()`.

### Values swapped

Check register mapping matches between C++ config and Python simulator.

### Connection timeout

- Verify baud rate matches (57600)
- Check correct pts device is used
- Ensure simulator is running before C++ controller

## Debug Output

### Enable C++ Debug

The SerialModbus prints request/response when reading multiple registers:

```
[REQ] Slave 1, addr 0, count 2: [01 04 00 00 00 02 71 CB]
[DEBUG] Slave 1, regs 0-1: raw=[01 04 04 28 39 78 42 80 18] values=[10297 30786]
```

### Enable Python Debug

Add to `modbus_device.py`:

```python
import logging
logging.basicConfig(level=logging.DEBUG)
```

## Integration Test Script

An automated script is available:

```bash
./scripts/run_integration.sh
```

This:
1. Creates virtual serial port
2. Starts Python simulator
3. Builds C++ simulator
4. Runs integration test

## Files

- `include/hal/simulator/serial_modbus.h` - Serial MODBUS implementation
- `src/main.cpp` - Main with MODBUS_PORT support
- `config/integration_test.json` - Config matching Python simulator
- `scripts/run_integration.sh` - Automated integration test
- `modbus_simulator/` - Python MODBUS simulator
- `include/modules/debug_console.h` - USB serial debug console
- `include/hal/serial_interface.h` - HAL serial interface
- `docs/DEBUG_CONSOLE.md` - Debug console documentation

## 4-20mA Fault Detection

The system detects wire breaks and sensor faults by monitoring raw MODBUS values against configurable thresholds.

### How It Works

```
4-20mA Signal Range
═══════════════════════════════════════════════════════════════

Signal      MODBUS Value    Pressure    Detection
────────────────────────────────────────────────────────────────
<4mA        0-799           ~0 bar      WIRE BREAK → SensorQuality::BAD
4mA         800             0 bar       Minimum valid
20mA        32000           1.56 bar    Maximum valid
>20mA       >32000          >1.56 bar   SENSOR FAULT → SensorQuality::BAD
```

### Configuration

Add `min_raw` and `max_raw` to each register definition:

```json
{
  "registers": [
    {
      "name": "fermenter_1_pressure",
      "reg": 0,
      "scale": 0.0000488,
      "min_raw": 800,    // Below this = wire break (<4mA)
      "max_raw": 32000   // Above this = sensor fault (>20mA)
    }
  ]
}
```

### Threshold Calculation

For Waveshare analog module with 4-20mA input:

```
min_raw = 800   ≈ 2.4% of range  → catches wire break with margin
max_raw = 32000 ≈ 97.7% of range → catches sensor over-range
```

### Fault Response

When a fault is detected:
1. `SensorQuality` set to `BAD`
2. Alarm event published
3. Safety controller triggers `sensor_failure_alarm`
4. Sensor value not updated (holds last good value)

### Testing Wire Break

In Python simulator, set channel value to 0:
- Web UI → Set pressure to 0 bar
- C++ will detect `raw_modbus < min_raw` and trigger alarm

## Sensor Filtering

The C++ controller applies filters to smooth noisy sensor readings. The Python simulator adds configurable noise (default 5% Gaussian).

### Filter Types

| Filter | Config | Description |
|--------|--------|-------------|
| EMA | `"filter": "ema", "filter_alpha": 0.3` | Exponential Moving Average |
| Moving Avg | `"filter": "moving_avg", "filter_window": 5` | Simple Moving Average |
| Median | `"filter": "median", "filter_window": 5` | Median filter (outlier rejection) |
| Dual-Rate | `"filter": "dual_rate"` | Combines base + extra samples |
| None | `"filter": "none"` | Pass-through |

### Example Output

With 5% sensor noise:
```
Cycle 1:
  fermenter_1_pressure: raw=0.504 filtered=0.512 bar
  fermenter_2_pressure: raw=1.537 filtered=1.517 bar
Cycle 2:
  fermenter_1_pressure: raw=0.507 filtered=0.509 bar
  fermenter_2_pressure: raw=1.459 filtered=1.479 bar
```

Raw values jump around (±5%), but filtered values stay stable.

### Python Noise Configuration

In `.env`:
```ini
SENSOR_NOISE_PERCENT=5.0    # ±5% noise
NOISE_TYPE=gaussian         # 'gaussian' or 'uniform'
NOISE_SEED=42               # For reproducible tests
```

## Protocol Details

### Read Request (C++ → Python)

```
01 04 00 00 00 02 71 CB
│  │  │     │     └──── CRC-16 (little-endian)
│  │  │     └───────── Count: 2 registers
│  │  └─────────────── Start address: 0
│  └────────────────── Function: 0x04 (Read Input Registers)
└───────────────────── Slave ID: 1
```

### Read Response (Python → C++)

```
01 04 04 28 39 78 42 80 18
│  │  │  │     │     └──── CRC-16
│  │  │  │     └───────── Register 1: 0x7842 = 30786
│  │  │  └─────────────── Register 0: 0x2839 = 10297
│  │  └────────────────── Byte count: 4
│  └───────────────────── Function: 0x04
└──────────────────────── Slave ID: 1
```

### Value Conversion

```
Pressure (bar) = MODBUS_value × 0.0000488
               = MODBUS_value × (1.6 / 32767)

Example: 10297 × 0.0000488 = 0.50 bar
         30786 × 0.0000488 = 1.50 bar
```
