# Fermentation Controller

ESP32-S3 based fermentation controller for brewery automation with MODBUS RTU sensors, PID temperature control, and REST API.

## Hardware

### Main Controller
- **Board:** [Waveshare ESP32-S3-POE-ETH-8DI-8DO](https://www.waveshare.com/wiki/ESP32-S3-POE-ETH-8DI-8DO)
- **MCU:** ESP32-S3-WROOM-1U-N16R8 (dual-core 240MHz, 16MB flash, 8MB PSRAM)
- **Connectivity:** WiFi, RS485, CAN, Ethernet with PoE
- **I/O:** 8 digital inputs, 8 digital outputs (500mA sink)

### Communication
- **MODBUS RTU:** RS485 interface for industrial sensors
- **CAN Bus:** TWAI at 500kbps for expansion
- **WiFi:** SmartConfig provisioning, mDNS discovery
- **HTTPS:** TLS 1.3 with self-signed certificates

### Purchase Links
- [Waveshare Store](https://www.waveshare.com/esp32-s3-poe-eth-8di-8do.htm)
- [Amazon](https://www.amazon.com/dp/B0C4XVPVQN) (availability varies)

## Features

- Multi-zone fermentation temperature control
- PID with auto-tuning (Ziegler-Nichols)
- Fermentation plan scheduler
- Safety alarms and watchdog
- Web admin interface with REST API
- USB debug console (20+ commands)
- NTP time synchronization

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/install) (VS Code extension or CLI)
- Python 3.8+ with venv

### Setup

```bash
# Clone repository
git clone https://github.com/claymore666/fermenter-controller.git
cd fermenter-controller

# Create Python virtual environment
python3 -m venv venv
source venv/bin/activate

# Install mDNS component (required for WiFi builds)
./scripts/setup_mdns.sh

# Build firmware
pio run -e esp32_wifi

# Upload to device
pio run -e esp32_wifi -t upload
```

### Build Environments

| Environment | Description | Command |
|-------------|-------------|---------|
| `simulator` | Native tests with MODBUS simulator | `pio test -e simulator` |
| `esp32` | Basic firmware (no WiFi) | `pio run -e esp32` |
| `esp32_wifi` | WiFi + NTP + HTTPS | `pio run -e esp32_wifi` |
| `esp32_full` | Full features (WiFi + HTTP + MQTT) | `pio run -e esp32_full` |

## Device Access

After WiFi provisioning, access the device at:
```
https://fermenter-XXXXXX.local/admin/
```
Where `XXXXXX` is the last 3 bytes of the WiFi MAC address (uppercase hex).

First boot requires password setup (8+ chars, complexity requirements).

### Debug Console

Connect via USB serial at 115200 baud:
```bash
# Linux
screen /dev/ttyACM0 115200

# Or use PlatformIO
pio device monitor
```

Type `help` for available commands.

## Documentation

| Document | Description |
|----------|-------------|
| [CONTROLLER.md](CONTROLLER.md) | System architecture |
| [docs/DEBUG_CONSOLE.md](docs/DEBUG_CONSOLE.md) | Serial commands reference |
| [docs/WIFI_NTP.md](docs/WIFI_NTP.md) | WiFi provisioning guide |
| [docs/HTTP_ADMIN.md](docs/HTTP_ADMIN.md) | REST API reference |
| [docs/MDNS_HTTPS.md](docs/MDNS_HTTPS.md) | mDNS and certificate setup |
| [docs/DATA_MODEL.md](docs/DATA_MODEL.md) | Core data structures |
| [docs/NVS_STORAGE.md](docs/NVS_STORAGE.md) | Non-volatile storage keys |

## Configuration

Runtime configuration in `config/default_config.json`. See [docs/BUILD_FLAGS.md](docs/BUILD_FLAGS.md) for compile-time options.

## License

MIT License - see [LICENSE](LICENSE) for details.
