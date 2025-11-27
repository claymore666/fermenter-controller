# Fermentation Controller - Project Overview

ESP32-S3 based fermentation controller with MODBUS RTU sensors, PID control, and REST API.

## Quick Start

```bash
# Run tests
source venv/bin/activate
pio test -e simulator

# Build ESP32 firmware
pio run -e esp32

# Run MODBUS simulator
cd modbus_simulator && python simulator.py
```

## Architecture

- **HAL Layer**: Hardware abstraction for ESP32 and simulator
- **Core**: State management, event bus, configuration
- **Modules**: MODBUS, PID, fermentation plans, safety, REST API, debug console

## Documentation

### Main Documentation

| Document | Description |
|----------|-------------|
| [CONTROLLER.md](CONTROLLER.md) | Full system architecture, HAL interfaces, module design |
| [docs/BUILD_FLAGS.md](docs/BUILD_FLAGS.md) | Compile-time flags (WiFi, MQTT, debug console) |
| [docs/DEBUG_CONSOLE.md](docs/DEBUG_CONSOLE.md) | USB serial debug commands reference |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | Component integration and file structure |
| [docs/WIFI_NTP.md](docs/WIFI_NTP.md) | WiFi provisioning, NTP time sync, SmartConfig |
| [docs/HTTP_ADMIN.md](docs/HTTP_ADMIN.md) | HTTP admin interface, REST API, security |
| [docs/STATUS_LED.md](docs/STATUS_LED.md) | WS2812 RGB status LED indicator |
| [docs/NVS_STORAGE.md](docs/NVS_STORAGE.md) | NVS key-value storage reference |
| [docs/DATA_MODEL.md](docs/DATA_MODEL.md) | Core data structures and JSON representation |
| [docs/MDNS_HTTPS.md](docs/MDNS_HTTPS.md) | mDNS device discovery and HTTPS/TLS configuration |

### Hardware Reference

| Document | Description |
|----------|-------------|
| [docs/ESP32-S3-POE-ETH-8DI-8DO_REFERENCE.md](docs/ESP32-S3-POE-ETH-8DI-8DO_REFERENCE.md) | Waveshare board pinout and capabilities |

### MODBUS Simulator

| Document | Description |
|----------|-------------|
| [modbus_simulator/README.md](modbus_simulator/README.md) | Simulator setup and usage |
| [modbus_simulator/SIMULATOR_DESCRIPTION.md](modbus_simulator/SIMULATOR_DESCRIPTION.md) | Simulator architecture and protocol details |

## Key Files

### Configuration
- `platformio.ini` - Build configuration, environments, flags
- `config/default_config.json` - Runtime configuration

### Source Code
- `src/main.cpp` - Entry point for ESP32 and simulator
- `include/core/` - State manager, event bus, types, config
- `include/modules/` - MODBUS, PID, plans, safety, REST API, debug console
- `include/security/` - Security utilities (hashing, rate limiting, cert generation)
- `include/hal/` - Hardware abstraction interfaces
- `include/hal/esp32/` - ESP32 implementations
- `include/hal/simulator/` - Simulator implementations

### Tests
- `test/test_core/` - State manager, event bus tests
- `test/test_modules/` - Filter, PID, fault detection tests
- `test/test_config/` - Configuration loader tests
- `test/test_integration/` - MODBUS, plan manager tests
- `test/test_api/` - REST API, safety controller tests
- `test/test_debug_console/` - Debug console command tests

## Current Status

### Implemented
- Core state machine and event bus
- MODBUS RTU communication (simulator + serial)
- PID controller with autotuner
- Fermentation plan manager
- Safety controller with alarms
- Sensor filtering (EMA, moving average, median, dual-rate)
- 4-20mA fault detection (wire break, overrange)
- Debug console (20+ commands including WiFi/NVS management)
- ESP32 HAL (serial, time, GPIO, MODBUS, storage, WiFi)
- HTTP server with REST API and admin web interface
- Security: first-boot password setup, SHA-256 hashing, rate limiting, HTTPS with per-device SSL certs, session tokens
- CPU usage monitoring
- WiFi connectivity with provisioning (SmartConfig + Captive Portal)
- NTP time synchronization with timezone support
- NVS storage for credentials and configuration
- Status LED (WS2812 RGB on GPIO38)
- CAN bus communication (TWAI at 500kbps)
- Ethernet connectivity (W5500 SPI on GPIO12-16, GPIO39)
- Network failover (WiFi standby when Ethernet on same network)

### Not Implemented
- MQTT client

## Device Access

| Access Method | URL |
|---------------|-----|
| mDNS (HTTPS) | `https://fermenter-XXXXXX.local/admin/` |
| IP (HTTPS) | `https://192.168.0.139/admin/` |
| IP (HTTP→redirect) | `http://192.168.0.139/` (redirects to HTTPS) |

Where `XXXXXX` is the last 3 bytes of the device MAC address (e.g., `fermenter-230778`).

**First-boot password setup**: Device requires password creation on initial access (8+ chars, 2 of: uppercase/lowercase/digit). No default password.

**Self-signed certificate**: Browser will show warning. See [docs/MDNS_HTTPS.md](docs/MDNS_HTTPS.md) for trusting the certificate.

## Build Targets

| Environment | Purpose | Command |
|-------------|---------|---------|
| `simulator` | Native testing with MODBUS simulator | `pio test -e simulator` |
| `esp32` | ESP32-S3 firmware (no WiFi) | `pio run -e esp32` |
| `esp32_wifi` | ESP32-S3 with WiFi + NTP | `pio run -e esp32_wifi` |
| `esp32_full` | ESP32-S3 full (WiFi + HTTP + MQTT) | `pio run -e esp32_full` |

## Resource Usage (ESP32-S3)

```
RAM:   18.2%  (59 KB / 320 KB)
Flash: 29.4%  (1232 KB / 4 MB OTA partition)
```

Note: 16MB flash with dual 4MB OTA partitions (app0/app1) + 8MB SPIFFS.

## HTTPS/TLS Configuration

| Parameter | Value |
|-----------|-------|
| Protocol | TLS 1.3 (fallback to TLS 1.2) |
| Cipher Suite | TLS_AES_256_GCM_SHA384 |
| Key Exchange | x25519 |
| Signature | RSASSA-PSS |
| Certificate | RSA-2048, SHA-256, self-signed |
| Validity | 10 years |
| SSL Buffer Size | 4KB in/out (reduced from 16KB for concurrent sessions) |
| Max Open Sockets | 7 (default) |

Certificate is generated per-device on first boot (~9 seconds) and stored in NVS.

**SSL Buffer Optimization**: The SSL buffers are reduced to 4KB (from default 16KB) to allow more concurrent TLS sessions with available memory. This saves ~12KB per connection, enabling the ESP32 to handle multiple browser connections during page loads without TLS allocation failures (`-0x7780`).

## Power Management

The ESP32 runs in power-saving mode to reduce heat and energy consumption:

| Parameter | Value |
|-----------|-------|
| Max CPU Frequency | 80 MHz |
| Min CPU Frequency | 10 MHz (during light sleep) |
| Light Sleep | Enabled (automatic during idle) |
| WiFi Power Save | Enabled (maintains connection during sleep) |

Power management is configured in `sdkconfig.defaults` and runtime via `esp_pm_configure()`.

Typical CPU usage when idle: ~0.2%

## Compile Flags

| Flag | Description |
|------|-------------|
| `DEBUG_CONSOLE_ENABLED` | USB serial debug console |
| `WIFI_NTP_ENABLED` | WiFi + NTP + Provisioning |
| `ETHERNET_ENABLED` | Ethernet connectivity (W5500 SPI) |
| `CAN_ENABLED` | CAN bus communication (TWAI) |
| `HTTP_ENABLED` | HTTP server + REST API |
| `CERT_GENERATION_ENABLED` | Per-device SSL certificate generation |
| `MQTT_ENABLED` | MQTT client (not implemented) |

See [docs/BUILD_FLAGS.md](docs/BUILD_FLAGS.md) for details.

## SSL Certificate Generation

On first boot (or after `ssl clear`), the device generates a unique RSA-2048 key pair and self-signed X.509 certificate. This takes ~9 seconds and the status LED blinks blue during generation.

| Item | Value |
|------|-------|
| Key size | RSA-2048 |
| Validity | 10 years |
| Generation time | ~9 seconds |
| Storage | NVS (`ssl:cert`, `ssl:key`) |

Debug console commands:
- `ssl status` - Show certificate status and size
- `ssl clear` - Delete certificate (regenerates on reboot)

## Network Failover

When both WiFi and Ethernet are enabled, the device implements automatic network failover:

| Scenario | Behavior |
|----------|----------|
| Both on same network | WiFi enters standby, Ethernet is primary |
| Different networks | Both interfaces stay active (dual-homed) |
| Ethernet link down | WiFi resumes from standby (fast failover) |
| Ethernet link restored | WiFi returns to standby |

**Same network detection**: Compares gateway IP addresses. If both interfaces have the same gateway, they're considered on the same network.

**WiFi Hot Standby mode**: WiFi connection remains active (keeps IP) but route priority is lowered so traffic uses Ethernet. When Ethernet fails, traffic switches instantly to WiFi with no reconnection delay. The web interface shows a "Hot Standby" badge (cyan) for WiFi in this state.

Debug console commands:
- `wifi` - Shows WiFi status including standby state
- `eth` - Shows Ethernet status (IP, gateway, speed)

## Code Robustness Guidelines

All code must be robust and handle failures gracefully without crashing. Follow these paradigms:

### Null Pointer Safety
- Always check pointers before dereferencing: `if (!ptr) return false;`
- Validate constructor parameters before storing
- Check memory allocations: `if (!pvPortMalloc(...)) return false;`

### Communication Bus Resilience
- **MODBUS**: Flush RX buffer before transactions, validate count bounds, handle timeouts gracefully
- **CAN**: Auto-recover from bus-off state, limit message processing per cycle
- Never let communication failures crash the main loop

### Buffer and Arithmetic Safety
- Bounds check all array accesses
- Validate division denominators: `if (divisor == 0) return;`
- Use 64-bit arithmetic for overflow-prone calculations: `(uint64_t)a * b / c`

### Error Handling
- Return false/error codes instead of crashing
- Log errors with ESP_LOGW/ESP_LOGE
- Degrade gracefully (mark sensors as BAD quality, not crash)

### Initialization
- Clean up on partial init failure (e.g., `uart_driver_delete()` if pin setup fails)
- Check `initialized_` flag before operations
- Return false from initialize() on any failure

### Watchdog and Recovery
- Enable task watchdog timer (TWDT)
- Periodically yield in long operations
- Implement recovery mechanisms for stuck states

## Serial Debugging

### Reading Boot Output

Use Python with pyserial to read serial output during boot and debug issues:

```bash
source venv/bin/activate
python3 << 'EOF'
import serial
import time

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
# Reset device via DTR/RTS toggling
ser.dtr = False
ser.rts = True
time.sleep(0.1)
ser.dtr = True
ser.rts = False
time.sleep(0.5)
ser.dtr = False

# Read boot output
for i in range(30):  # Read for ~30 seconds
    data = ser.read(2048)
    if data:
        print(data.decode('utf-8', errors='replace'), end='')
    time.sleep(0.5)
ser.close()
EOF
```

### Debug Console Commands

Send commands to the debug console:

```bash
source venv/bin/activate
python3 << 'EOF'
import serial
import time

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
ser.write(b'\nstatus\n')  # Send command
time.sleep(1)
data = ser.read(4096)
print(data.decode('utf-8', errors='replace'))
ser.close()
EOF
```

### Decoding Backtraces

When a crash or watchdog timeout occurs, decode the backtrace:

```bash
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp-elf-addr2line \
  -e .pio/build/esp32_wifi/firmware.elf \
  0x42026AF2 0x42026E60 0x4037AF9E
```

### Common Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Watchdog timeout during boot | Long operation blocking IDLE task | Add `vTaskDelay()` yields in long loops |
| No serial output | Wrong baud rate or port | Verify 115200 baud on /dev/ttyACM0 |
| Boot loop | Crash during init | Read boot log, decode backtrace |
| Connection refused | Server not started | Check boot log for errors |

## Development Notes

- Approve GUI/terminal formatting before flashing - don't wear out ESP32 flash memory with frequent writes
- Batch multiple small changes into single flash operations
- Build and verify compilation without flashing for minor tweaks
- ESP32 flash has ~10,000-100,000 write cycles - minimize unnecessary uploads
- Always source venv before using pio: `source venv/bin/activate && pio run -e esp32_wifi`

## Commit and Release Workflow

**IMPORTANT**: All commits to GitHub must pass compilation and tests. Follow this workflow:

### Pre-Commit Checklist (Mandatory)

Before every commit, run these steps in order:

```bash
source venv/bin/activate

# 1. Run test suite (must pass)
pio test -e simulator

# 2. Build ESP32 firmware (must compile without errors)
pio run -e esp32_wifi
```

Both steps must succeed before committing. Do not commit code that fails tests or compilation.

### Flashing Workflow (When User Requests)

If the user wants to flash the device after changes:

```bash
# 1. Upload firmware
pio run -e esp32_wifi -t upload

# 2. Upload SPIFFS if data/ files changed (HTML, CSS, JS, config)
pio run -e esp32_wifi -t uploadfs

# 3. Verify boot via serial console
python3 << 'EOF'
import serial
import time
import sys

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
print("Waiting for device boot...")
time.sleep(2)  # Wait for reboot

boot_output = ""
start_time = time.time()
boot_success = False

while time.time() - start_time < 30:  # 30 second timeout
    data = ser.read(2048)
    if data:
        text = data.decode('utf-8', errors='replace')
        boot_output += text
        print(text, end='')

        # Check for successful boot indicators
        if "HTTP server started" in boot_output or "WiFi connected" in boot_output:
            boot_success = True
            break
        # Check for crash indicators
        if "Guru Meditation" in boot_output or "panic" in boot_output.lower():
            print("\n\n*** BOOT FAILED - CRASH DETECTED ***")
            ser.close()
            sys.exit(1)

ser.close()

if boot_success:
    print("\n\n*** BOOT SUCCESSFUL ***")
else:
    print("\n\n*** BOOT VERIFICATION TIMEOUT - Check manually ***")
EOF
```

### What to Upload

| Changed Files | Upload Commands |
|---------------|-----------------|
| `src/`, `include/` (C++ code) | `pio run -e esp32_wifi -t upload` |
| `data/` (HTML, CSS, JS) | `pio run -e esp32_wifi -t uploadfs` |
| Both | Both commands |

### Release Workflow

For releases (merging dev → main):

1. **Run full test suite**: `pio test -e simulator`
2. **Build all targets**:
   ```bash
   pio run -e esp32_wifi
   pio run -e esp32
   ```
3. **Flash and verify boot** (see above)
4. **Test key functionality** via web interface
5. **Create PR** from `dev` to `main`
6. **Tag release** on main: `git tag v0.1.0 && git push --tags`

### Boot Success Indicators

The boot verification script looks for these in serial output:

| Indicator | Meaning |
|-----------|---------|
| `HTTP server started` | Web server initialized |
| `WiFi connected` | Network connectivity OK |
| `NTP synced` | Time synchronization OK |
| `Guru Meditation` | **CRASH** - boot failed |
| `panic` | **CRASH** - boot failed |
| `watchdog` | **TIMEOUT** - possible hang |
- remember that we have a sdkconfig of our own not a default from platformio