# MODBUS RTU Simulator

A modular, thread-based MODBUS RTU simulator in Python that emulates a Waveshare Modbus RTU Analog Input 8CH module on ESP32 with realistic timing characteristics for fermenter controller development.

## Features

- **ESP32-realistic timing**: ~10ms response delay with ±2ms jitter
- **Sensor noise simulation**: Configurable Gaussian/uniform noise (default ±5%)
- **Time-based pressure simulation**: Interpolation from start to target pressure
- **Pressure drop simulation**: Toggle on/off, configurable rate and target
- **MODBUS Hub**: Promiscuous mode for trigger events (e.g., solenoid valve)
- **Modern Web UI**: Bootstrap 5 dashboard with real-time Chart.js graphs
- **WebSocket API**: Live monitoring and control
- **Virtual serial port**: Development without hardware via socat

## Hardware Simulation

### Waveshare Modbus RTU Analog Input 8CH
- **Channel 1-2**: Pressure sensors 0-1.6 bar (4-20mA)
- **Channels 3-8**: Unused (0mA)
- **Slave ID**: Configurable (default: 1)

### 4-20mA to MODBUS Conversion
```
4mA  = 0 bar    → Register value 0
20mA = 1.6 bar  → Register value 32767
```

### MODBUS Register Map

Matches the real Waveshare hardware register addresses:

#### Input Registers (Function Code 0x04) - Sensor Data
| Address | Description |
|---------|-------------|
| 0x0000 | Channel 1 sensor value (0-32767) |
| 0x0001 | Channel 2 sensor value |
| 0x0002-0x0007 | Channels 3-8 (unused, returns 0) |

#### Holding Registers (Function Codes 0x03, 0x06, 0x10) - Configuration
| Address | Description | Default |
|---------|-------------|---------|
| 0x1000-0x1007 | Data type per channel | 0x0003 (4-20mA) |
| 0x2000 | UART parameters | 0x0000 |
| 0x4000 | Device address | Slave ID |
| 0x8000 | Software version | 0x0100 |

#### Data Type Modes
| Value | Mode |
|-------|------|
| 0x0000 | 0-5V voltage |
| 0x0001 | 1-5V voltage |
| 0x0002 | 0-20mA current |
| 0x0003 | 4-20mA current (default) |
| 0x0004 | Raw ADC value |

#### Baud Rate Encoding (for 0x2000 register)
| Value | Baud Rate |
|-------|-----------|
| 0x00 | 4800 |
| 0x01 | 9600 (default) |
| 0x02 | 19200 |
| 0x03 | 38400 |
| 0x04 | 57600 |
| 0x05 | 115200 |

#### Supported MODBUS Function Codes
| Code | Function | Description |
|------|----------|-------------|
| 0x03 | Read Holding Registers | Read configuration (data types, UART, address, version) |
| 0x04 | Read Input Registers | Read sensor data (channels 1-8) |
| 0x06 | Write Single Register | Set single config value |
| 0x10 | Write Multiple Registers | Set multiple config values |

#### Broadcast Address Support
The simulator responds to both its configured slave ID and broadcast address 0x00 for configuration commands (changing address, baud rate).

## Installation

### Requirements
- Python 3.10+
- socat (for virtual serial port)

### Setup

```bash
cd modbus_simulator

# Create virtual environment
python -m venv venv
source venv/bin/activate  # Linux/Mac

# Install dependencies
pip install -r requirements.txt

# Copy and configure environment
cp .env.example .env
```

### Virtual Serial Port Setup

For development without hardware:

```bash
# Start virtual serial port
./setup_vport.sh start

# Check status
./setup_vport.sh status

# Stop
./setup_vport.sh stop
```

Update `.env`:
```ini
MODBUS_PORT=./vport_simulator
```

## Configuration

All settings are in `.env`:

### Serial Port
```ini
MODBUS_PORT=./vport_simulator
MODBUS_BAUDRATE=9600
MODBUS_PARITY=N
MODBUS_STOPBITS=1
MODBUS_SLAVE_ID=1
```

### ESP32 Timing
```ini
RESPONSE_DELAY_MS=10        # Base response delay
RESPONSE_JITTER_MS=2        # ±jitter for FreeRTOS simulation
SENSOR_UPDATE_RATE_MS=100   # ADC cycle simulation
```

### Sensor Noise
```ini
SENSOR_NOISE_PERCENT=5.0    # ±5% noise on measurements
NOISE_TYPE=gaussian         # 'gaussian' or 'uniform'
NOISE_SEED=42               # For reproducible tests
```

### Pressure Sensors
```ini
# Sensor 1
CH1_ENABLED=true
CH1_START_PRESSURE=0.5
CH1_TARGET_PRESSURE=0.5
CH1_RATE_BAR_PER_MIN=100.0
CH1_AUTO_START=true

# Sensor 2
CH2_ENABLED=true
CH2_START_PRESSURE=1.5
CH2_TARGET_PRESSURE=1.5
CH2_RATE_BAR_PER_MIN=10.0
CH2_AUTO_START=true
```

### Pressure Drop (Hub)
```ini
HUB_ENABLED=true
TRIGGER_SLAVE_ID=2
TRIGGER_REGISTER=0
TRIGGER_VALUE=1
PRESSURE_DROP_RATE=0.025    # bar/sec (can be changed at runtime)
PRESSURE_DROP_DURATION_S=5.0
AFFECTED_SENSOR=1
```

### Web Server
```ini
WEB_HOST=0.0.0.0
WEB_PORT=8080
WEB_UPDATE_INTERVAL_MS=500
ENABLE_WEBSOCKET=true
```

## Usage

### Start Simulator

```bash
# Start virtual serial port first
./setup_vport.sh start

# Run simulator
python main.py
```

### Web Interface

Open http://localhost:8080

#### Workflow Example

1. **Auto-start**: Simulators start automatically with default values (CH1: 0.5 bar, CH2: 1.5 bar)
2. **Set parameters**: Adjust target pressure and rate as needed
3. **Drop**: Click "Trigger Pressure Drop" - pressure drops at configured rate
4. **Stop drop**: Click "Trigger Pressure Drop" again to stop the drop
5. **Resume**: Click Start - pressure rises again with your target/rate

### Connect MODBUS Client

Connect to `./vport_client` to read sensor values:

```python
from pymodbus.client import ModbusSerialClient

client = ModbusSerialClient(
    port='./vport_client',
    baudrate=9600,
    parity='N',
    stopbits=1
)
client.connect()

# Read INPUT registers (function code 0x04) - sensor data
result = client.read_input_registers(0x0000, 2, slave=1)
print(f"Channel 1: {result.registers[0]}")  # 0-32767
print(f"Channel 2: {result.registers[1]}")

# Convert to pressure
def to_bar(value):
    return (value / 32767) * 1.6

print(f"Pressure 1: {to_bar(result.registers[0]):.3f} bar")

# Read HOLDING registers (function code 0x03) - configuration
# Read data types
data_types = client.read_holding_registers(0x1000, 8, slave=1)
print(f"Data types: {data_types.registers}")

# Read software version
version = client.read_holding_registers(0x8000, 1, slave=1)
print(f"Version: {version.registers[0]:04x}")

client.close()
```

## API Reference

### REST API

#### Get Status
```
GET /api/status
```
Returns complete simulator state including sensors, simulators, timing, and hub.

#### Sensor Control
```
POST /api/simulator/{sensor}/start
POST /api/simulator/{sensor}/stop
POST /api/simulator/{sensor}/reset
```

#### Set Parameters
```
POST /api/simulator/target
{"sensor": 1, "target": 1.2}

POST /api/simulator/rate
{"sensor": 1, "rate": 0.1}

POST /api/simulator/pressure
{"sensor": 1, "pressure": 0.5}
```

#### Pressure Drop Control
```
# Trigger drop (toggle on/off)
POST /api/hub/trigger
{}

# With options
POST /api/hub/trigger
{
  "target_pressure": 0.5,      # Drop to specific pressure
  "duration_seconds": 3.0,     # OR drop for duration
  "rate": 0.1,                 # Override drop rate
  "sensor": 1                  # Override affected sensor
}

# Set default drop rate
POST /api/hub/rate
{"rate": 0.025}
```

### WebSocket API

Connect to `ws://localhost:8080/ws`

#### Client → Server Messages

```javascript
// Control simulation
{type: "start_simulation", sensor: 1}
{type: "stop_simulation", sensor: 1}
{type: "reset_simulation", sensor: 1}

// Set parameters
{type: "set_target", sensor: 1, target: 1.2}
{type: "set_rate", sensor: 1, rate: 0.1}

// Pressure drop
{type: "trigger_hub"}
{type: "trigger_hub", target_pressure: 0.5, rate: 0.1}
{type: "trigger_hub", duration_seconds: 3.0}
{type: "set_drop_rate", rate: 0.025}

// Fault simulation
{type: "set_fault", sensor: 1, fault_mode: "wire_break"}
{type: "set_fault", sensor: 1, fault_mode: "sensor_defect"}
{type: "set_fault", sensor: 1, fault_mode: null}  // Clear fault
```

### Fault Simulation

The simulator supports fault injection to test controller fault detection logic. Each sensor has three buttons in the web UI:

| Button | Fault Mode | Behavior |
|--------|------------|----------|
| **0mA** | `wire_break` | Simulates broken wire - outputs 0mA (register value 0) |
| **25mA** | `sensor_defect` | Simulates sensor defect - outputs 25mA (exceeds 20mA range) |
| **Normal** | `null` | Clears fault, returns to normal operation |

#### Fault Mode Details

- **Wire Break (0mA)**: The sensor outputs 0mA regardless of pressure. This simulates a disconnected or broken sensor wire. Controllers should detect this as an under-range fault.

- **Sensor Defect (25mA)**: The sensor outputs 25mA, which exceeds the 4-20mA range. This simulates a failed sensor that's stuck high. Controllers should detect this as an over-range fault. Register value: `(25-4)/16 * 32767 ≈ 43008`.

#### Usage

1. Click the fault button (0mA or 25mA) for the desired sensor
2. The active fault button highlights in red
3. The sensor immediately switches to fault mode
4. Click "Normal" to restore normal operation

This feature is useful for testing that your fermenter controller properly detects and handles sensor faults before deployment.

#### Server → Client Messages

```javascript
{
  "type": "status_update",
  "timestamp": 1234567890,
  "sensors": [
    {
      "channel": 1,
      "true_pressure": 0.450,
      "measured_pressure": 0.457,
      "current_ma": 8.45,
      "modbus_value": 5432,
      "fault_mode": null
    },
    ...
  ],
  "simulators": [
    {
      "channel": 1,
      "start_pressure": 0.0,
      "target_pressure": 1.2,
      "current_pressure": 0.450,
      "rate_bar_per_min": 0.1,
      "running": true,
      "at_target": false
    },
    ...
  ],
  "timing": {
    "avg_response_ms": 10.2,
    "min_response_ms": 8.1,
    "max_response_ms": 12.4,
    "jitter_ms": 1.8,
    "total_requests": 150
  },
  "hub": {
    "running": true,
    "trigger_active": false,
    "pressure_drop_rate": 0.025,
    "affected_sensor": 1
  }
}
```

## Architecture

### Thread Model

```
┌─────────────────────────────────────────────────┐
│                  Main Thread                     │
│           (Signal handling, shutdown)            │
└─────────────────────────────────────────────────┘
                       │
       ┌───────────────┼───────────────┐
       ▼               ▼               ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│   MODBUS    │ │   Sensor    │ │    Web      │
│   Server    │ │   Update    │ │   Server    │
│  (pymodbus) │ │   Loop      │ │  (uvicorn)  │
└─────────────┘ └─────────────┘ └─────────────┘
```

### Virtual Serial Port

```
┌─────────────────┐          ┌─────────────────┐
│  MODBUS Server  │ ◄──────► │  MODBUS Client  │
│   (Simulator)   │          │   (Your code)   │
│                 │          │                 │
│ vport_simulator │          │  vport_client   │
└─────────────────┘          └─────────────────┘
```

### Data Flow

```
User Input (Web UI)
       ↓
Pressure Simulator → Sensor (+ noise) → MODBUS Registers
                                              ↓
                                     MODBUS RTU Client
```

## Testing

### Run Test Suite

```bash
source venv/bin/activate
pytest tests/ -v

# With coverage
pytest --cov=src tests/
```

### Test Categories
- `test_timing.py` - ESP32 timing accuracy (13 tests)
- `test_sensor_noise.py` - Noise distribution statistics (11 tests)
- `test_modbus.py` - MODBUS protocol compliance (10 tests)

## Troubleshooting

### pymodbus 1-Based Addressing

**Important**: pymodbus internally uses 1-based addressing for input registers (following Modicon convention where input registers are 30001-3XXXX). This means:

- When a client requests address 0, pymodbus internally queries address 1
- The simulator stores channel 1 at index 1, channel 2 at index 2 (not 0 and 1)
- The wire protocol uses 0-based addresses, but the datastore is 1-based

If you see sensor values appearing in the wrong registers (e.g., channel 2 value in register 0), check that the simulator is using 1-based indexing in `update_registers()`.

### Serial Port Permission (Linux)

```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

### Port Not Found

```bash
# Check virtual port status
./setup_vport.sh status

# Restart if needed
./setup_vport.sh restart
```

### Timing Statistics Not Showing

Timing stats only update when a MODBUS client reads from the server. Connect a client to `./vport_client` and poll registers to see timing data.

### WebSocket Connection Failed

- Check firewall settings for port 8080
- Verify WEB_HOST=0.0.0.0 in .env

## Project Structure

```
modbus_simulator/
├── main.py                 # Entry point
├── setup_vport.sh          # Virtual serial port setup
├── requirements.txt
├── .env                    # Configuration (create from .env.example)
├── .env.example
├── README.md
├── src/
│   ├── __init__.py
│   ├── config.py           # Pydantic settings validation
│   ├── timing.py           # ESP32 timing simulation
│   ├── sensor.py           # Pressure sensor with noise
│   ├── pressure_simulator.py # Time-based pressure changes
│   ├── modbus_device.py    # MODBUS RTU server
│   ├── modbus_hub.py       # Promiscuous mode & triggers
│   └── web_server.py       # FastAPI + WebSocket
├── web/
│   ├── templates/
│   │   └── index.html      # Bootstrap 5 dashboard
│   └── static/
│       ├── css/custom.css
│       └── js/app.js       # WebSocket client & Chart.js
└── tests/
    ├── test_timing.py
    ├── test_sensor_noise.py
    └── test_modbus.py
```

## Pressure Drop Feature

The pressure drop simulates events like opening a solenoid valve. It can be controlled via the web UI or API.

### Behavior

- **First click**: Starts pressure drop at configured rate
- **Second click**: Stops drop, restores original target/rate
- **Auto-restore**: If not stopped manually, restores after calculated duration

### Configuration Options

| Parameter | Description | Default |
|-----------|-------------|---------|
| `target_pressure` | Drop to specific pressure (bar) | - |
| `duration_seconds` | Drop for time period | 5.0 |
| `rate` | Drop rate (bar/sec) | 0.025 |
| `sensor` | Which sensor to affect | 1 |

### Examples

```python
# Drop to 0.3 bar
hub.trigger_pressure_drop(target_pressure=0.3)

# Drop for 10 seconds at 0.05 bar/s
hub.trigger_pressure_drop(duration_seconds=10, rate=0.05)

# Change default rate
hub.set_drop_rate(0.1)
```

## License

MIT License

## Contributing

1. Fork the repository
2. Create a feature branch
3. Write tests for new functionality
4. Ensure all tests pass: `pytest tests/ -v`
5. Submit a pull request
