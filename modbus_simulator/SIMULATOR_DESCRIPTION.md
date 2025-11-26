# MODBUS RTU Simulator - Technical Description

A Python-based simulator that emulates a Waveshare Modbus RTU Analog Input 8CH module for fermenter controller development. Provides realistic ESP32 timing characteristics, sensor noise simulation, and a web interface for monitoring and control.

## Architecture Overview

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  Web Interface  │────▶│   WebServer      │────▶│  Simulators     │
│  (Browser)      │◀────│   (FastAPI)      │◀────│  (2 channels)   │
└─────────────────┘     └──────────────────┘     └─────────────────┘
                               │                         │
                               ▼                         ▼
                        ┌──────────────────┐     ┌─────────────────┐
                        │   ModbusHub      │     │  PressureSensor │
                        │   (Promiscuous)  │     │  (with noise)   │
                        └──────────────────┘     └─────────────────┘
                                                         │
                                                         ▼
                        ┌──────────────────┐     ┌─────────────────┐
                        │  MODBUS Client   │────▶│  ModbusDevice   │
                        │  (External)      │◀────│  (pymodbus)     │
                        └──────────────────┘     └─────────────────┘
```

## Directory Structure

```
modbus_simulator/
├── main.py                 # Entry point, orchestrates all components
├── src/
│   ├── config.py           # Pydantic settings, loads from .env
│   ├── sensor.py           # PressureSensor with noise simulation
│   ├── timing.py           # ESP32Timer for response delays
│   ├── pressure_simulator.py # Time-based pressure ramping
│   ├── modbus_device.py    # MODBUS RTU server (pymodbus)
│   ├── modbus_hub.py       # Promiscuous mode, pressure drop trigger
│   └── web_server.py       # FastAPI + WebSocket server
├── web/
│   ├── templates/
│   │   └── index.html      # Bootstrap 5 UI
│   └── static/
│       ├── css/custom.css
│       └── js/app.js       # WebSocket client, Chart.js
├── tests/                  # pytest test suite (47 tests)
├── .env                    # Configuration file
├── .env.example            # Configuration template
├── setup_vport.sh          # Virtual serial port management
└── requirements.txt        # Python dependencies
```

## Core Components

### 1. Configuration (`src/config.py`)

Uses Pydantic Settings to load configuration from environment variables and `.env` file.

**Key Configuration Classes:**
- `Config`: Main configuration with all settings
- `SensorConfig`: Per-channel sensor settings

**Important Settings:**
```python
# Serial Port
MODBUS_PORT = "/dev/ttyUSB0"  # or "./vport_simulator" for virtual
MODBUS_BAUDRATE = 9600
MODBUS_PARITY = 'N'  # N, E, O
MODBUS_SLAVE_ID = 1

# Timing Simulation
RESPONSE_DELAY_MS = 10      # Base ESP32 response delay
RESPONSE_JITTER_MS = 2      # Random jitter ±2ms
SENSOR_UPDATE_RATE_MS = 100

# Sensor Noise
SENSOR_NOISE_PERCENT = 5.0  # ±5% noise
NOISE_TYPE = 'gaussian'     # or 'uniform'

# Pressure Channels
CH1_TARGET_PRESSURE = 1.2   # bar
CH1_RATE_BAR_PER_MIN = 0.1

# Hub (Pressure Drop)
TRIGGER_SLAVE_ID = 2
PRESSURE_DROP_RATE = 0.5    # bar/s
```

**Runtime Persistence:**
The `persist_config(key, value)` function updates the `.env` file when settings change via the web UI.

### 2. Pressure Sensor (`src/sensor.py`)

Simulates a 4-20mA pressure sensor with configurable noise.

**Class: `PressureSensor`**

```python
sensor = PressureSensor(
    channel=1,
    noise_percent=5.0,
    noise_type='gaussian',
    seed=42
)

# Update with true pressure (bar)
sensor.update(0.8)

# Get state
state = sensor.get_state()
# Returns:
{
    'channel': 1,
    'true_pressure': 0.8,
    'measured_pressure': 0.812,  # with noise
    'current_ma': 12.0,          # 4-20mA value
    'modbus_value': 16383        # 0-32767 range
}
```

**Conversion Formula:**
- Pressure range: 0-1.6 bar
- Current range: 4-20 mA
- MODBUS range: 0-32767

```python
current_ma = 4.0 + (pressure / 1.6) * 16.0
modbus_value = int((current_ma - 4.0) / 16.0 * 32767)
```

### 3. ESP32 Timer (`src/timing.py`)

Simulates realistic ESP32 FreeRTOS timing characteristics.

**Class: `ESP32Timer`**

```python
timer = ESP32Timer(
    base_delay_ms=10,
    jitter_ms=2,
    seed=42
)

# Apply delay (blocks thread)
actual_delay = timer.delay()  # Returns actual delay applied
```

**Class: `TimingStats`**

Tracks response time statistics:
- `avg_response_ms`
- `min_response_ms`
- `max_response_ms`
- `jitter_ms` (standard deviation)
- `total_requests`

### 4. Pressure Simulator (`src/pressure_simulator.py`)

Time-based pressure ramping toward a target.

**Class: `PressureSimulator`**

```python
simulator = PressureSimulator(sensor, sensor_config)

# Control methods
simulator.start()
simulator.stop()
simulator.reset()
simulator.set_target(1.5)        # bar
simulator.set_rate(0.2)          # bar/min
simulator.set_pressure(0.5)      # Set directly

# Called periodically by main loop
simulator.update()

# Get state
state = simulator.get_state()
# Returns:
{
    'running': True,
    'target_pressure': 1.5,
    'rate_bar_per_min': 0.2,
    'current_pressure': 0.8
}
```

### 5. MODBUS Device (`src/modbus_device.py`)

Implements the Waveshare Modbus RTU Analog Input 8CH register map using pymodbus.

**Class: `ModbusDevice`**

**Register Map (matching real hardware):**

| Register Type | Address | Description |
|--------------|---------|-------------|
| Input (0x04) | 0x0000-0x0007 | Channel 1-8 sensor data |
| Holding (0x03/0x06) | 0x1000-0x1007 | Data type per channel |
| Holding | 0x2000 | UART parameters |
| Holding | 0x4000 | Device address |
| Holding | 0x8000 | Software version |

**Data Types (0x1000-0x1007):**
- 0x0000: 0-5V
- 0x0001: 1-5V
- 0x0002: 0-20mA
- 0x0003: 4-20mA (default)
- 0x0004: RAW ADC

**UART Parameters (0x2000):**
- High byte: Parity (0=None, 1=Even, 2=Odd)
- Low byte: Baud rate (0=4800, 1=9600, 2=19200, 3=38400, 4=57600, 5=115200)

**Supported MODBUS Functions:**
- 0x03: Read Holding Registers
- 0x04: Read Input Registers
- 0x06: Write Single Register
- 0x10: Write Multiple Registers

**Broadcast Support:**
Device responds to both its slave ID and broadcast address 0.

**Class: `DelayedDataBlock`**

Custom datastore that applies ESP32 timing delays when reading registers.

```python
# Internal usage
block = DelayedDataBlock(0, [0]*8, timer, use_timing=True)
values = block.getValues(0, 8)  # Applies delay before returning
```

### 6. MODBUS Hub (`src/modbus_hub.py`)

Monitors bus traffic in promiscuous mode and triggers pressure drops.

**Class: `ModbusHub`**

```python
hub = ModbusHub(
    simulators=[sim1, sim2],
    trigger_slave_id=2,
    trigger_register=0,
    trigger_value=1,
    pressure_drop_rate=0.5,
    affected_sensor=1
)

# Manual trigger (toggle behavior)
hub.trigger_pressure_drop()  # First call starts drop
hub.trigger_pressure_drop()  # Second call stops and restores

# Get state
state = hub.get_state()
# Returns:
{
    'enabled': True,
    'trigger_active': False,
    'trigger_slave_id': 2,
    'trigger_register': 0,
    'pressure_drop_rate': 0.5,
    'affected_sensor': 1
}
```

**Toggle Behavior:**
1. First trigger: Stores current target/rate, starts pressure drop
2. Second trigger: Stops drop, restores original target/rate
3. User can then hit "Start" to resume with their custom values

### 7. Web Server (`src/web_server.py`)

FastAPI server with REST API and WebSocket for real-time updates.

**Class: `WebServer`**

**REST API Endpoints:**

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Serve web UI |
| GET | `/api/status` | Full simulator status |
| GET | `/api/sensors` | Sensor states |
| GET | `/api/simulators` | Simulator states |
| GET | `/api/timing` | Timing statistics |
| GET | `/api/hub` | Hub state |
| POST | `/api/simulator/{sensor}/start` | Start simulator |
| POST | `/api/simulator/{sensor}/stop` | Stop simulator |
| POST | `/api/simulator/{sensor}/reset` | Reset simulator |
| POST | `/api/simulator/target` | Set target pressure |
| POST | `/api/simulator/rate` | Set pressure rate |
| POST | `/api/simulator/pressure` | Set pressure directly |
| POST | `/api/hub/trigger` | Trigger pressure drop |
| POST | `/api/hub/rate` | Set drop rate |
| GET | `/api/device/config` | Get device config |
| POST | `/api/device/uart` | Set UART params |
| POST | `/api/device/address` | Set device address |

**WebSocket (`/ws`):**

Sends status updates every 500ms (configurable).

**Incoming Messages:**
```javascript
// Set target pressure
{type: "set_target", sensor: 1, target: 1.5}

// Set rate
{type: "set_rate", sensor: 1, rate: 0.2}

// Control simulation
{type: "start_simulation", sensor: 1}
{type: "stop_simulation", sensor: 1}
{type: "reset_simulation", sensor: 1}

// Hub trigger
{type: "trigger_hub"}

// Device config
{type: "set_uart_params", baudrate: 9600, parity: "N"}
{type: "set_device_address", address: 5}
```

**Outgoing Messages:**
```javascript
{
    type: "status_update",
    timestamp: 1700000000.123,
    sensors: [...],
    simulators: [...],
    timing: {...},
    hub: {...},
    device: {...}
}
```

## Main Entry Point (`main.py`)

**Class: `ModbusSimulator`**

Orchestrates all components and manages threads.

**Threads:**
1. **SensorUpdate**: Periodically updates pressure simulators and MODBUS registers
2. **WebServer**: Runs uvicorn FastAPI server
3. **ModbusServer**: pymodbus serial server (started by ModbusDevice)

**Startup Sequence:**
1. Load configuration from `.env`
2. Create timer, sensors, simulators
3. Create MODBUS device with register map
4. Create hub for pressure drop monitoring
5. Create web server
6. Auto-start simulators if configured
7. Start MODBUS server on serial port
8. Start hub monitoring
9. Start sensor update loop
10. Start web server

**Signal Handling:**
Catches SIGINT/SIGTERM for graceful shutdown.

## Virtual Serial Port Setup

The `setup_vport.sh` script creates a virtual serial port pair using `socat`.

```bash
# Start virtual ports
./setup_vport.sh start
# Creates: ./vport_simulator <-> ./vport_client

# Stop virtual ports
./setup_vport.sh stop

# Check status
./setup_vport.sh status
```

**Usage:**
- Simulator connects to `./vport_simulator`
- Your MODBUS client connects to `./vport_client`

## Web UI Features

The web interface (`web/templates/index.html`) provides:

1. **Sensor Cards (2x)**
   - True pressure vs Measured pressure
   - Current (mA) and MODBUS value
   - Target pressure input
   - Rate (bar/min) input
   - Start/Stop/Reset buttons

2. **Pressure History Chart**
   - Real-time Chart.js line graph
   - Shows all 4 traces (S1/S2 true/measured)

3. **Timing Statistics**
   - Average, min/max response times
   - Jitter (standard deviation)
   - Total request count

4. **Hub Control**
   - Shows trigger configuration
   - "Trigger Pressure Drop" button (toggle)

5. **Device Configuration**
   - Current slave ID and version
   - Device address input
   - Baud rate dropdown
   - Parity dropdown
   - Apply buttons

6. **Channel Data Types**
   - Shows data type for each channel

## Testing

The test suite uses pytest with 47 tests covering:

- `test_sensor.py`: Sensor conversions, noise
- `test_timing.py`: Timer delays, statistics
- `test_modbus.py`: Register operations, Waveshare-specific registers
- `test_simulator.py`: Pressure ramping
- `test_sensor_noise.py`: Noise distribution verification

Run tests:
```bash
source venv/bin/activate
python -m pytest -v
```

## Dependencies

- `pymodbus>=3.6.1`: MODBUS RTU server
- `pyserial>=3.5`: Serial port handling
- `fastapi>=0.104.0`: Web API framework
- `uvicorn>=0.24.0`: ASGI server
- `websockets>=12.0`: WebSocket support
- `pydantic>=2.5.0`: Configuration validation
- `pydantic-settings>=2.1.0`: Environment loading
- `numpy>=1.26.0`: Noise generation

## Usage

```bash
# Setup virtual environment
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Configure
cp .env.example .env
# Edit .env as needed

# Start virtual serial port (optional)
./setup_vport.sh start

# Run simulator
python main.py

# Access web UI at http://localhost:8080
# Connect MODBUS client to configured port
```

## Client Development Guide

This section describes how to build a MODBUS RTU client that communicates with the simulator.

### Connection Parameters

```python
# Default settings (configurable in .env)
PORT = "/dev/ttyUSB0"  # or "./vport_client" for virtual port
BAUDRATE = 9600
PARITY = 'N'  # None
STOPBITS = 1
BYTESIZE = 8
SLAVE_ID = 1
TIMEOUT = 1.0  # seconds
```

### Reading Sensor Data

Use **Function Code 0x04** (Read Input Registers) to read sensor values.

| Channel | Register Address | Description |
|---------|-----------------|-------------|
| 1 | 0x0000 | Pressure sensor 1 |
| 2 | 0x0001 | Pressure sensor 2 |
| 3-8 | 0x0002-0x0007 | Unused (returns 0) |

**Value Interpretation:**
```python
# MODBUS value range: 0-32767
# Current range: 4-20 mA
# Pressure range: 0-1.6 bar

def modbus_to_pressure(modbus_value):
    current_ma = 4.0 + (modbus_value / 32767.0) * 16.0
    pressure_bar = (current_ma - 4.0) / 16.0 * 1.6
    return pressure_bar

# Example: modbus_value = 16383 → ~0.8 bar
```

### Reading Device Configuration

Use **Function Code 0x03** (Read Holding Registers).

| Address | Description | Format |
|---------|-------------|--------|
| 0x1000-0x1007 | Data type per channel | 0=0-5V, 1=1-5V, 2=0-20mA, 3=4-20mA |
| 0x2000 | UART parameters | High byte=parity, Low byte=baud code |
| 0x4000 | Device address | 1-247 |
| 0x8000 | Software version | e.g., 100 = V1.00 |

**UART Parameter Encoding:**
```python
# Reading 0x2000
value = 0x0102  # Example
parity_code = (value >> 8) & 0xFF  # 0x01 = Even
baud_code = value & 0xFF           # 0x02 = 19200

BAUD_RATES = {0: 4800, 1: 9600, 2: 19200, 3: 38400, 4: 57600, 5: 115200}
PARITY = {0: 'N', 1: 'E', 2: 'O'}
```

### Writing Configuration

Use **Function Code 0x06** (Write Single Register) or **0x10** (Write Multiple Registers).

```python
# Set device address to 5
write_register(0x4000, 5)

# Set UART to 19200 baud, even parity
# value = (parity_code << 8) | baud_code
value = (0x01 << 8) | 0x02  # 0x0102
write_register(0x2000, value)
```

### Example Client Code (pymodbus)

```python
from pymodbus.client import ModbusSerialClient
from pymodbus.framer import FramerType

# Connect
client = ModbusSerialClient(
    port="./vport_client",  # or "/dev/ttyUSB0"
    baudrate=9600,
    parity='N',
    stopbits=1,
    bytesize=8,
    timeout=1.0,
    framer=FramerType.RTU
)
client.connect()

# Read both pressure sensors (Input Registers)
result = client.read_input_registers(
    address=0x0000,
    count=2,
    slave=1
)

if not result.isError():
    for i, value in enumerate(result.registers):
        current_ma = 4.0 + (value / 32767.0) * 16.0
        pressure = (current_ma - 4.0) / 16.0 * 1.6
        print(f"Channel {i+1}: {pressure:.3f} bar ({value})")

# Read device address (Holding Register)
result = client.read_holding_registers(
    address=0x4000,
    count=1,
    slave=1
)
if not result.isError():
    print(f"Device address: {result.registers[0]}")

# Read software version
result = client.read_holding_registers(
    address=0x8000,
    count=1,
    slave=1
)
if not result.isError():
    version = result.registers[0]
    print(f"Version: V{version // 100}.{version % 100:02d}")

client.close()
```

### Polling Strategy

The simulator updates sensor registers every 100ms (configurable via `SENSOR_UPDATE_RATE_MS`).

**Recommended polling interval:** 200-500ms

```python
import time

while True:
    result = client.read_input_registers(0x0000, 2, slave=1)
    if not result.isError():
        process_readings(result.registers)
    time.sleep(0.5)  # 500ms
```

### Timing Expectations

The simulator adds realistic ESP32 delays:
- Base delay: 10ms (configurable)
- Jitter: ±2ms (configurable)
- Total response time: typically 8-12ms

Your client timeout should be at least 100ms to handle worst-case scenarios.

### Error Handling

The simulator returns standard MODBUS exceptions:
- Invalid slave ID: No response (timeout)
- Invalid register address: Exception code 0x02 (Illegal Data Address)
- Invalid function code: Exception code 0x01 (Illegal Function)

```python
result = client.read_input_registers(0x9999, 1, slave=1)
if result.isError():
    print(f"Error: {result}")
```

### Broadcast Address

The simulator responds to broadcast address 0 for write operations:

```python
# Write to all devices (no response expected)
client.write_register(0x4000, 10, slave=0)
```

### Integration with Simulator Web API

For testing, you can control the simulator via HTTP while your client reads MODBUS:

```bash
# Set sensor 1 to specific pressure
curl -X POST http://localhost:8080/api/simulator/pressure \
  -H "Content-Type: application/json" \
  -d '{"sensor": 1, "pressure": 1.0}'

# Start pressure ramping
curl -X POST http://localhost:8080/api/simulator/1/start

# Trigger pressure drop
curl -X POST http://localhost:8080/api/hub/trigger
```

## Key Implementation Details

### Thread Safety
- All shared data protected with `threading.Lock()`
- Sensor updates and MODBUS reads are synchronized

### Noise Generation
- Uses numpy for Gaussian/uniform distribution
- Seed configurable for reproducibility
- Applied as percentage of full scale

### Pressure Ramping
- Time-based calculation using `time.monotonic()`
- Independent direction detection (rising/falling)
- Automatic stop when target reached

### Register Addressing
- Input registers (0x04) for sensor data: sparse block at 0x0000
- Holding registers (0x03/0x06) for config: large sequential block to 0x8000
- Broadcast address (0) always responds

### WebSocket Updates
- Non-blocking receive with 100ms timeout
- Automatic reconnection on disconnect
- JSON message format
