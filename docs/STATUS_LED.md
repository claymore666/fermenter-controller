# Status LED Module

WS2812 RGB LED status indicator for the fermentation controller.

## Hardware

| Component | GPIO | Description |
|-----------|------|-------------|
| WS2812 RGB LED | 38 | Programmable status LED |

## Color Coding

| Color | Pattern | Meaning |
|-------|---------|---------|
| **Blue** | Solid | Booting / Provisioning mode |
| **Blue** | Slow blink | SSL certificate generation in progress |
| **Blue** | Fast blink | OTA firmware download in progress |
| **Green** | Solid | All OK - WiFi connected, NTP synced, no errors |
| **Yellow** | Slow blink | Warning - NTP not synced, WiFi disconnected |
| **Red** | Solid | Error - sensor failure |
| **Red** | Fast blink | Critical alarm active |

## Boot Sequence

| Phase | Color | Description |
|-------|-------|-------------|
| Starting | Blue | System initializing |
| WiFi connecting | Blue | Connecting to stored network |
| NTP syncing | Blue | Waiting for time sync |
| SSL cert gen | Blue blink | Generating RSA-2048 key (~9 seconds on first boot) |
| Boot complete | Green | All systems OK |
| Boot failed | Yellow | WiFi or NTP failed |

### Priority

```
Red (Error/Alarm) > Blue (Provisioning) > Blue (Cert Gen) > Blue (OTA) > Yellow (Warning) > Green (OK)
```

## Conditions

### Error (Red)
- Sensor failure (wire break, over-range)
- Critical system fault
- Safety alarm triggered

### Warning (Yellow)
- WiFi not connected
- NTP not synchronized
- Non-critical warnings

### Provisioning (Blue breathing)
- Device in WiFi setup mode
- Waiting for SmartConfig or Captive Portal

### OTA Download (Blue fast blink)
- Firmware download in progress
- System paused during update
- Returns to normal after reboot

### OK (Green)
- All systems operational
- WiFi connected
- NTP synced
- No errors or warnings

### Shutdown
- LED turns off when `shutdown` command is executed
- All digital outputs also turn off
- Device enters deep sleep (requires RESET to wake)

## Usage

### Basic Usage

```cpp
#include "modules/status_led.h"

hal::esp32::ESP32Time time_hal;
modules::StatusLed led(&time_hal, 38);  // GPIO38

// Initialize
led.init();
led.set_brightness(32);  // Low brightness (0-255)

// Main loop
void loop() {
    // Update state from system
    led.set_provisioning(wifi_prov.is_provisioning());
    led.set_wifi_connected(wifi.is_connected());
    led.set_ntp_synced(ntp.is_synced());
    led.set_has_errors(safety.has_sensor_failures());
    led.set_has_alarms(safety.has_active_alarms());
    led.set_ota_downloading(ota.is_downloading());

    // Update LED (call every 50ms)
    led.update();
}
```

### Integration with System

```cpp
// In main control loop
void update_status_led() {
    modules::StatusLed::State state;

    // WiFi/Network
    state.provisioning = g_wifi_prov.is_provisioning();
    state.wifi_connected = g_wifi_prov.is_connected();
    state.ntp_synced = g_ntp.is_synced();

    // Safety
    state.has_alarms = g_safety.has_active_alarms();
    state.has_errors = false;
    state.has_warnings = false;

    // Check all fermenters for sensor failures
    for (uint8_t i = 1; i <= core::MAX_FERMENTERS; i++) {
        auto* alarm = g_safety.get_alarm_state(i);
        if (alarm && alarm->sensor_failure_alarm) {
            state.has_errors = true;
            break;
        }
    }

    // Check for warnings (non-critical)
    if (!state.wifi_connected || !state.ntp_synced) {
        state.has_warnings = true;
    }

    g_status_led.set_state(state);
    g_status_led.update();
}
```

### Manual Control

```cpp
// Force specific color
led.set_color(StatusLed::Color::CYAN);

// Turn off
led.off();

// Adjust brightness
led.set_brightness(128);  // 50% brightness
```

## Patterns

| Pattern | Description | Used For |
|---------|-------------|----------|
| `SOLID` | Constant on | Normal states |
| `BLINK_SLOW` | 1 Hz (500ms) | Warnings, SSL cert gen |
| `BLINK_FAST` | 4 Hz (125ms) | Critical alarms, OTA download |
| `PULSE` | Fade in/out | - |
| `BREATHE` | Slow fade | Provisioning (WiFi setup) |

## Debug Console Integration

Add to debug console for testing:

```cpp
// In cmd_help():
serial_->println("  led <color>         - Set LED color");
serial_->println("  led brightness <n>  - Set brightness 0-255");

// In execute_command():
} else if (strcmp(args[0], "led") == 0) {
    cmd_led(argc, args);
}

// Command handler:
void cmd_led(int argc, char** args) {
    if (argc < 2) {
        printf("LED: %s\r\n", StatusLed::color_to_string(led_->get_color()));
        return;
    }

    if (strcmp(args[1], "brightness") == 0 && argc >= 3) {
        led_->set_brightness(atoi(args[2]));
    } else if (strcmp(args[1], "red") == 0) {
        led_->set_color(StatusLed::Color::RED);
    } else if (strcmp(args[1], "green") == 0) {
        led_->set_color(StatusLed::Color::GREEN);
    } else if (strcmp(args[1], "blue") == 0) {
        led_->set_color(StatusLed::Color::BLUE);
    } else if (strcmp(args[1], "yellow") == 0) {
        led_->set_color(StatusLed::Color::YELLOW);
    } else if (strcmp(args[1], "off") == 0) {
        led_->off();
    }
}
```

## ESP-IDF Dependencies

The module uses the ESP-IDF LED Strip component:

```ini
# In platformio.ini or sdkconfig
CONFIG_SOC_RMT_SUPPORTED=y
```

The `led_strip` component is included in ESP-IDF v5.x by default.

## Files

| File | Description |
|------|-------------|
| `include/modules/status_led.h` | Status LED module |
| `docs/STATUS_LED.md` | This documentation |

## Troubleshooting

### LED not working
- Verify GPIO38 is correct for your board
- Check WS2812 data line connection
- Ensure LED strip component is enabled in ESP-IDF

### Colors look wrong
- WS2812 uses GRB format (not RGB)
- Adjust color values in `set_color_brightness()`

### LED too bright/dim
- Use `set_brightness()` to adjust (0-255)
- Default is 32 (low brightness)
