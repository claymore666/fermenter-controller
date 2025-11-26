# ESP32-S3-POE-ETH-8DI-8DO Hardware Reference

**SKU:** 32108
**Part No:** ESP32-S3-POE-ETH-8DI-8DO
**Wiki:** https://www.waveshare.com/wiki/ESP32-S3-POE-ETH-8DI-8DO

## Core Specifications

| Parameter | Value |
|-----------|-------|
| Microcontroller | ESP32-S3 (ESP32-S3-WROOM-1U-N16R8) |
| Processor | Xtensa 32-bit LX7 dual-core @ 240MHz |
| Flash | 16MB |
| PSRAM | 8MB |
| Wireless | 2.4GHz WiFi (802.11 b/g/n), Bluetooth 5 (LE) |
| Dimensions | 175 x 90 x 40 mm |

## Power Supply

- **USB Type-C:** 5V
- **Screw Terminal:** 7-36V DC (wide voltage input)
- **PoE:** IEEE 802.3af compliant

## Digital Inputs (8 channels)

| Parameter | Value |
|-----------|-------|
| Channels | 8 (DI1-DI8) |
| Input Voltage | 5-36V |
| Input Type | Passive (dry contact) or Active (NPN/PNP) |
| Isolation | Bi-directional optocoupler |

### Wiring

**Dry Contact (passive):** Connect switch between DIx and COM
**Wet Contact (active):** Supply 5-36V to COM, connect sensor output to DIx

## Digital Outputs (8 channels)

| Parameter | Value |
|-----------|-------|
| Channels | 8 (DO1-DO8) |
| Output Type | Open-drain (Darlington transistor) |
| Load Voltage | 5-40V |
| Sink Current | 500mA max per channel |
| Isolation | Optocoupler |
| Protection | Built-in flyback diode |

### Wiring

Connect load between DOx and positive supply (5-40V). DGND connects to supply negative.

## Communication Interfaces

### RS485 (Isolated)

- Screw terminal connector
- Hardware automatic direction control
- TVS protection (surge/ESD)
- 120Ω matching resistor (NC default, enable via jumper)

### CAN (Isolated)

- Screw terminal connector
- Hardware automatic direction control
- TVS protection (surge/ESD)
- 120Ω matching resistor (NC default, enable via jumper)

### Ethernet

- W5500 chip (SPI interface)
- 10/100 Mbps
- Built-in PoE module (IEEE 802.3af)

## GPIO Pin Mapping

### Digital Inputs

| Channel | GPIO | Function |
|---------|------|----------|
| DI1 | GPIO4 | Digital input 1 |
| DI2 | GPIO5 | Digital input 2 |
| DI3 | GPIO6 | Digital input 3 |
| DI4 | GPIO7 | Digital input 4 |
| DI5 | GPIO8 | Digital input 5 |
| DI6 | GPIO9 | Digital input 6 |
| DI7 | GPIO10 | Digital input 7 |
| DI8 | GPIO11 | Digital input 8 |

**Input Behavior:**
- All inputs use optocoupler isolation (bi-directional)
- Internal pull-down resistors are enabled on all GPIO pins
- When disconnected/inactive: reads **LOW** (false)
- When active signal applied (5-36V): reads **HIGH** (true)

**Reading Digital Inputs:**
```cpp
// Setup - enable internal pull-down
for (int pin = 4; pin <= 11; pin++) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);
}

// Read individual input (true = active, false = inactive)
bool di1 = gpio_get_level(GPIO_NUM_4);   // DI1
bool di8 = gpio_get_level(GPIO_NUM_11);  // DI8
```

### Digital Outputs (via I2C I/O Expander)

Digital outputs are controlled via **TCA9554PWR** I/O expander (directly connected to Darlington transistors, not directly to ESP32 GPIO).

| Parameter | Value |
|-----------|-------|
| I2C Address | **0x20** |
| I2C SDA | GPIO42 |
| I2C SCL | GPIO41 |
| Pins | EXIO1-EXIO8 |

| Channel | EXIO Pin | Function |
|---------|----------|----------|
| DO1 | EXIO1 | Digital output 1 |
| DO2 | EXIO2 | Digital output 2 |
| DO3 | EXIO3 | Digital output 3 |
| DO4 | EXIO4 | Digital output 4 |
| DO5 | EXIO5 | Digital output 5 |
| DO6 | EXIO6 | Digital output 6 |
| DO7 | EXIO7 | Digital output 7 |
| DO8 | EXIO8 | Digital output 8 |

**Controlling Digital Outputs:**
```cpp
#include <Wire.h>

#define TCA9554_ADDR 0x20
#define TCA9554_OUTPUT_REG 0x01
#define TCA9554_CONFIG_REG 0x03

void setupOutputs() {
    Wire.begin(42, 41);  // SDA=GPIO42, SCL=GPIO41

    // Configure all pins as outputs (0 = output)
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_CONFIG_REG);
    Wire.write(0x00);  // All outputs
    Wire.endTransmission();
}

void setOutputs(uint8_t value) {
    // Set all 8 outputs at once (bit 0 = DO1, bit 7 = DO8)
    Wire.beginTransmission(TCA9554_ADDR);
    Wire.write(TCA9554_OUTPUT_REG);
    Wire.write(value);
    Wire.endTransmission();
}

// Example: Turn on DO1 and DO3
setOutputs(0b00000101);
```

### RS485

| GPIO | Function |
|------|----------|
| GPIO17 | RS485 TX (UART TX) |
| GPIO18 | RS485 RX (UART RX) |
| GPIO21 | RS485 RTS (direction control) |

### CAN

| GPIO | Function |
|------|----------|
| GPIO2 | CAN TX (TWAI TX) |
| GPIO3 | CAN RX (TWAI RX) |

### Ethernet (W5500 SPI)

| GPIO | Function |
|------|----------|
| GPIO12 | ETH_INT (interrupt) |
| GPIO13 | ETH_MOSI |
| GPIO14 | ETH_MISO |
| GPIO15 | ETH_SCLK |
| GPIO16 | ETH_CS |
| GPIO39 | ETH_RST |

### TF Card (SD - 1-bit mode)

| GPIO | Function |
|------|----------|
| GPIO45 | SD_D0 |
| GPIO47 | SD_CMD |
| GPIO48 | SD_SCK |

Note: SD_D1, SD_D2, SD_D3 are NC (not connected) - use 1-bit SD mode.

### RTC (I2C)

| GPIO | Function |
|------|----------|
| GPIO40 | RTC_INT (interrupt) |
| GPIO41 | RTC_SCL (I2C clock) |
| GPIO42 | RTC_SDA (I2C data) |

### Other Peripherals

| GPIO | Function |
|------|----------|
| GPIO38 | WS2812 RGB LED control |
| GPIO46 | Buzzer control |
| GPIO0 | BOOT button |

## MODBUS Notes

This board is designed as a **MODBUS RTU master/controller**. It does not have predefined MODBUS slave registers.

### Important: I/O Access Methods

The board's own digital I/O is **NOT** accessed via MODBUS registers:

| I/O Type | Access Method |
|----------|---------------|
| Digital Inputs (DI1-8) | Direct GPIO read (GPIO4-11) |
| Digital Outputs (DO1-8) | I2C to TCA9554PWR (address 0x20) |

MODBUS is used to communicate with **external devices** (sensors, relays) connected via RS485.

### RS485 MODBUS Configuration

- **TX:** GPIO17
- **RX:** GPIO18
- **RTS:** GPIO21 (direction control)
- Baud rate, parity, and protocol details are defined by your firmware

### External Modbus RTU Relay Control Commands

Commands for controlling external Modbus RTU Relay modules (via Bluetooth/RS485):

| Command | Function |
|---------|----------|
| 06 01 | Toggle CH1 relay |
| 06 02 | Toggle CH2 relay |
| 06 03 | Toggle CH3 relay |
| 06 04 | Toggle CH4 relay |
| 06 05 | Toggle CH5 relay |
| 06 06 | Toggle CH6 relay |
| 06 07 | Toggle CH7 relay |
| 06 08 | Toggle CH8 relay |
| 06 09 | Turn ON all relays |
| 06 0A | Turn OFF all relays |

Note: Enable `Extension_Enable = 1` in `WS_imformification.h` to use external relay control.

## Onboard Components

| # | Component | Description |
|---|-----------|-------------|
| 1 | ESP32-S3-WROOM-1U-N16R8 | Main MCU module |
| 2 | W5500 | Ethernet controller |
| 3 | RESET Button | System reset |
| 4 | TF Card Slot | SD card storage |
| 14 | Buzzer | Audio feedback |
| 15 | TCA9554PWR | I/O expander for digital outputs |
| 16 | RTC Battery Holder | Backup for RTC |
| 24 | WS2812 RGB LED | Programmable status LED |
| 25 | BOOT Button | Firmware download mode |

## LED Indicators

| LED | Color | Function |
|-----|-------|----------|
| PWR | Red | Power indicator (USB connected) |
| TXD | Green | RS485/CAN transmit |
| RXD | Blue | RS485/CAN receive |
| RGB | Programmable | WS2812 status LED |

## Pin Header (28-pin, 5.0mm pitch)

Multi-function terminal with voltage level selection for communication logic and output voltage.

## Protection Features

- **Power Isolation:** Isolated DC-DC for stable voltage
- **Optocoupler Isolation:** DI/DO isolation from MCU
- **Digital Isolation:** RS485/CAN signal isolation
- **TVS Protection:** Surge and ESD protection on RS485/CAN

## Terminal Layout

### Front (Bottom edge, left to right)

**Digital Outputs:** COM, GND, DO8, DO7, DO6, DO5, DO4, DO3, DO2, DO1
**Digital Inputs:** COM, GND, DI8, DI7, DI6, DI5, DI4, DI3, DI2, DI1
**Power:** 7-36V (+/-)

### Back (Top edge)

- RS485 terminal
- CAN terminal
- Multi-function pin header
- USB Type-C
- Ethernet/PoE port
- Antenna connector (SMA female)

## Application Notes

- Supports Arduino IDE development
- Can directly drive relays (up to 500mA sink)
- Suitable for controlling: solenoid valves, motors, water pumps
- Input sources: switches, proximity sensors, smoke detectors
- Supports MQTT for cloud connectivity (Waveshare.Cloud demos available)

## Related Models

| Model | DI | DO | Relay | RS485 | CAN | PoE |
|-------|----|----|-------|-------|-----|-----|
| ESP32-S3-POE-ETH-8DI-8DO | 8 | 8 | - | ✓ | ✓ | ✓ |
| ESP32-S3-POE-ETH-8DI-8RO | 8 | - | 8 | ✓ | - | ✓ |
| ESP32-S3-ETH-8DI-8RO | 8 | - | 8 | ✓ | - | - |

## Development Resources

### Arduino IDE Setup

1. Install Arduino IDE from https://www.arduino.cc/en/software
2. Add ESP32 board support via Board Manager
3. Install required libraries:
   - **ArduinoJson** - JSON parsing

### Required Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoJson | v6.21.4 | Lightweight JSON library |
| PubSubClient | v2.8.0 | MQTT message subscription/publishing |
| NTPClient | v3.2.1 | Network time synchronization |

### Board Selection

- **Board:** ESP32S3 Dev Module
- **USB CDC On Boot:** Enabled (for serial monitor)
- **Flash Size:** 16MB
- **PSRAM:** OPI PSRAM

### Demo Resources

Official demos available at: https://www.waveshare.com/wiki/ESP32-S3-POE-ETH-8DI-8DO#Resources

| Demo | Description | Notes |
|------|-------------|-------|
| 01_MAIN_WIFI_AP | RS485, Bluetooth, Web control | WiFi AP mode - connect to device's WiFi |
| 02_MAIN_WIFI_STA | RS485, Bluetooth, Web control | WiFi STA mode - modify SSID/password in code |
| 03_MAIN_ALL | All features + Waveshare cloud | Requires cloud device setup |

**Control Methods in Demos:**
- RS485 interface control
- Bluetooth control (can send IP via BT)
- Web page control (local network)
- Waveshare cloud control (remote, demo 03 only)

### WiFi Modes

- **AP Mode:** Board creates its own access point
- **STA Mode:** Board connects to existing WiFi network

Example AP setup:
```cpp
WiFi.softAP(ssid, password);
```

### Datasheets

- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-S3-WROOM-1 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)

## Safety Instructions

### Electrical Safety

- **Always turn off power** before installing, maintaining, or replacing devices
- Do not disassemble the device
- Do not use in humid, high-temperature, flammable, or explosive environments
- Ensure rated voltage and current match the load
- Install **fuse or circuit breaker** in circuit for overload/short-circuit protection

### Relay/Output Safety

- Inductive loads (motors, solenoids) may have startup current much higher than rated
- Relay switching generates arcs - consider arc suppression circuits for inductive loads
- After power-on, confirm or reset output status to prevent uncertain states
- Periodically inspect contacts, terminals, and insulation

## Troubleshooting

### Enter Download Mode

If flashing fails or serial port won't connect:
1. Long press **BOOT** button
2. While holding BOOT, press and release **RESET**
3. Release **BOOT** button
4. Board is now in download mode

### No Serial Output

- Enable **USB CDC On Boot** in Arduino IDE board settings
- Or declare HWCDC in code
- Serial baud rate: **115200**

### RS485 Communication Issues

If RS485 is not sensitive or communication fails:
- Enable the **120Ω termination resistor** by moving jumper cap to "120R" position
- Some RS485 devices require this termination

### Module Keeps Resetting

Usually caused by blank Flash:
- Enter download mode (see above)
- Flash firmware to resolve

## Quick Reference - Pin Summary

| Function | GPIO Pins |
|----------|-----------|
| Digital Inputs | 4, 5, 6, 7, 8, 9, 10, 11 |
| Digital Outputs | I2C expander @ 0x20 |
| RS485 | 17 (TX), 18 (RX), 21 (RTS) |
| CAN | 2 (TX), 3 (RX) |
| Ethernet SPI | 12-16, 39 |
| SD Card | 45, 47, 48 |
| RTC I2C | 40 (INT), 41 (SCL), 42 (SDA) |
| RGB LED | 38 |
| Buzzer | 46 |
