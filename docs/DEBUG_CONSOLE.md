# Debug Console

USB serial debugging interface for monitoring and controlling the fermentation controller.

## Overview

The debug console provides real-time access to system state via serial terminal. Connect via USB and use a serial monitor (115200 baud) to interact with the system.

## Build Configuration

Enabled by default in `platformio.ini`:

```ini
[env:esp32]
build_flags =
    -DDEBUG_CONSOLE_ENABLED
    -DDEBUG_BAUD=115200

[env:simulator]
build_flags =
    -DDEBUG_CONSOLE_ENABLED
```

To disable, remove the `-DDEBUG_CONSOLE_ENABLED` flag.

## Connecting

### ESP32 Hardware

1. Connect ESP32 via USB
2. Open serial monitor: `pio device monitor` or use PuTTY/minicom
3. Baud rate: 115200, 8N1
4. Type `help` for commands

### Simulator

In simulator mode, the debug console uses stdin/stdout. Commands can be injected via the `SimulatorSerial::inject_input()` method for testing.

## Commands

### System Status

| Command | Description |
|---------|-------------|
| `status` | System overview (uptime, heap, WiFi, MODBUS stats) |
| `uptime` | Formatted uptime (days:hours:mins:secs) |
| `heap` | Free heap memory |
| `reboot` | Restart system (ESP32 only) |

### Sensors

| Command | Description |
|---------|-------------|
| `sensors` | List all sensors with current values |
| `sensor <name>` | Detailed sensor info (raw, filtered, quality, filter params) |

Example:
```
> sensors
Sensors:
  fermenter_1_pressure: 0.502 bar [GOOD]
  fermenter_2_pressure: 1.498 bar [GOOD]

> sensor fermenter_1_pressure
Sensor: fermenter_1_pressure
  Raw value: 0.498 bar
  Filtered: 0.502 bar
  Display: 0.502 bar
  Quality: 0
  Filter type: 2
  Alpha: 0.30
  Scale: 0.000049
  Last update: 5000 ms
```

### Relays

| Command | Description |
|---------|-------------|
| `relays` | List all relays with states |
| `relay <name>` | Relay details |
| `relay <name> on` | Turn relay on |
| `relay <name> off` | Turn relay off |

Example:
```
> relays
Relays:
  f1_cooling: OFF
  f1_spunding: OFF
  f2_cooling: ON
  f2_spunding: OFF

> relay f1_cooling on
Relay f1_cooling set to ON
```

### Fermenters

| Command | Description |
|---------|-------------|
| `fermenters` | List all fermenters |
| `fermenter <id>` | Fermenter details |
| `fermenter <id> setpoint <temp>` | Set target temperature |
| `fermenter <id> mode <off\|manual\|plan>` | Change operating mode |

Example:
```
> fermenters
Fermenters:
  F1: 18.5C -> 18.0C [MANUAL] PID=45%
  F2: 19.0C -> 18.0C [PLAN] PID=62%

> fermenter 1 setpoint 17.5
Fermenter 1 setpoint set to 17.5 C

> fermenter 1 mode plan
Fermenter 1 mode set to plan
```

### PID Control

| Command | Description |
|---------|-------------|
| `pid <id>` | Show PID parameters and state |
| `pid <id> tune <kp> <ki> <kd>` | Set PID tuning parameters |

Example:
```
> pid 1
PID for fermenter 1:
  Kp: 2.000
  Ki: 0.100
  Kd: 1.000
  Output: 45.2% (min: 0, max: 100)
  Integral: 12.340
  Last error: 0.500

> pid 1 tune 2.5 0.15 1.2
PID tuned: Kp=2.500 Ki=0.150 Kd=1.200
```

### Safety & Alarms

| Command | Description |
|---------|-------------|
| `alarms` | List active alarms |

Example:
```
> alarms
Alarms:
  Fermenter 2:
    - Temperature HIGH
    - Sensor FAILURE
```

### MODBUS Diagnostics

| Command | Description |
|---------|-------------|
| `modbus stats` | Transaction/error counts |
| `modbus read <addr> <reg> [count]` | Read raw MODBUS registers |

Example:
```
> modbus stats
MODBUS Statistics:
  Transactions: 1250
  Errors: 3
  Error rate: 0.24%

> modbus read 1 0 2
Read from 1 reg 0:
  [0]: 10240 (0x2800)
  [1]: 30720 (0x7800)
```

### CAN Bus

| Command | Description |
|---------|-------------|
| `can` | CAN bus status (TX/RX counts, errors, bus state) |
| `can send <id> <data...>` | Send CAN message (hex ID, space-separated data bytes) |

Example:
```
> can
CAN Bus Status:
  Initialized: Yes
  Bitrate: 500000 bps
  TX: 15
  RX: 42
  Errors: 0
  Bus: OK

> can send 0x123 0x01 0x02 0x03
Sent CAN ID 0x123 [3 bytes]
```

### Ethernet

| Command | Description |
|---------|-------------|
| `eth` | Ethernet status (IP, netmask, gateway, link speed) |
| `eth connect` | Start Ethernet interface and wait for DHCP |
| `eth disconnect` | Stop Ethernet interface |

Example:
```
> eth
Ethernet Status:
  Connected: Yes
  IP: 192.168.0.140
  Netmask: 255.255.255.0
  Gateway: 192.168.0.1
  Speed: 100 Mbps
  MAC: 30:ED:A0:23:07:78

Commands: eth connect | eth disconnect

> eth disconnect
Stopping Ethernet...
Ethernet stopped

> eth connect
Starting Ethernet...
Ethernet started. Waiting for DHCP...
Connected! IP: 192.168.0.140
```

### WiFi

| Command | Description |
|---------|-------------|
| `wifi` | Show WiFi status (SSID, IP, signal strength, standby state) |
| `wifi connect` | Connect to WiFi (uses stored credentials) |
| `wifi disconnect` | Disconnect from WiFi (persistent) |
| `wifi set <ssid> <password>` | Set WiFi credentials |
| `wifi clear` | Clear stored credentials and start provisioning |

Example:
```
> wifi
WiFi Status:
  State: CONNECTED
  SSID: Braustube
  IP: 192.168.0.139
  RSSI: -45 dBm

> wifi
WiFi Status:
  State: STANDBY (Ethernet primary)
  SSID: Braustube
```

**Note**: When Ethernet is connected on the same network, WiFi enters STANDBY mode. It remains ready for fast failover if Ethernet link goes down.

### SSL Certificate

| Command | Description |
|---------|-------------|
| `ssl` | Show SSL command help |
| `ssl status` | Show certificate status and size |
| `ssl clear` | Delete certificate (regenerates on reboot) |
| `ssl debug on` | Enable TLS debug logging |
| `ssl debug off` | Disable TLS debug logging |

Example:
```
> ssl status
Certificate: stored (cert=1193 bytes, key=1676 bytes)

> ssl debug on
TLS debug logging enabled

> ssl debug off
TLS debug logging disabled

> ssl clear
Clearing SSL certificate from NVS...
SSL certificate cleared
Reboot to regenerate certificate
```

### NVS Storage

| Command | Description |
|---------|-------------|
| `nvs` | Show NVS usage and available namespaces |
| `nvs list` | List known namespaces |
| `nvs list <namespace>` | List all keys within a namespace |
| `nvs get <ns>:<key>` | Read a specific key |
| `nvs set <ns>:<key> <value>` | Write a value |
| `nvs erase <ns>:<key>` | Delete a specific key |

**Namespaces:**
- `wifi` - WiFi credentials (ssid, password, auto_connect)
- `fermenter` - Device config (admin_pw, ssl:cert, ssl:key)
- `config` - User settings
- `nvs.net80211` - WiFi driver state (internal)

Example:
```
> nvs list
NVS namespaces:
  wifi       - WiFi credentials
  fermenter  - SSL certs, auth, plans
  nvs.net80211 - WiFi driver state

Use 'nvs list <namespace>' to show keys

> nvs list wifi
Keys in namespace 'wifi':
  ssid (STR)
  password (STR)
  auto_connect (I32)

> nvs get wifi:ssid
wifi:ssid = "Braustube"
```

### CPU Usage

| Command | Description |
|---------|-------------|
| `cpu` | Show CPU usage percentage |

Example:
```
> cpu
CPU Usage: 0.2%
```

### Factory Reset

| Command | Description |
|---------|-------------|
| `factory_reset` | Erase all NVS data (WiFi, password, SSL cert) and reboot |

**Warning**: This erases all stored credentials and settings!

## Architecture

```
include/
  hal/
    serial_interface.h        # HAL interface
  hal/esp32/
    esp32_serial.h            # ESP32 USB Serial/JTAG & UART
  hal/simulator/
    hal_simulator.h           # Contains SimulatorSerial
  modules/
    debug_console.h           # Command parser and handlers

src/
  main.cpp                    # Integration point

test/
  test_debug_console/
    test_debug_console.cpp    # 27 unit tests
```

## ESP32 Serial Options

Two ESP32 serial implementations are provided:

### USB Serial/JTAG (Default)
```cpp
hal::esp32::ESP32Serial g_serial;
```
Uses the built-in USB interface on ESP32-S3. No baud rate configuration needed.

### UART
```cpp
hal::esp32::ESP32UartSerial g_serial(UART_NUM_0, TX_PIN, RX_PIN);
```
Alternative for boards without USB Serial/JTAG or when USB is needed for other purposes.

## Adding New Commands

1. Add command handler method in `debug_console.h`:
```cpp
void cmd_mycommand(int argc, char** args) {
    // Implementation
}
```

2. Add routing in `execute_command()`:
```cpp
} else if (strcmp(args[0], "mycommand") == 0) {
    cmd_mycommand(argc, args);
}
```

3. Add to help text in `cmd_help()`.

## Thread Safety

On ESP32, the debug console runs in the main loop or its own FreeRTOS task. When accessing `StateManager`, be aware of concurrent access from the control task. Critical operations should use appropriate synchronization.

## Troubleshooting

### No response to commands

- Check baud rate (115200)
- Verify `DEBUG_CONSOLE_ENABLED` is defined
- Check serial cable/connection

### Commands not recognized

- Commands are case-sensitive
- Type `help` to see available commands

### Garbled output

- Baud rate mismatch
- Try 115200, 8N1, no flow control
