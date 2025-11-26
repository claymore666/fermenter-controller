# WiFi Provisioning - Lessons Learned

## Summary

This document captures issues encountered during ESP32-S3 WiFi provisioning implementation and their solutions.

---

## Issue 1: Bootloop - NULL Pointer in printf

**Symptom**: Guru Meditation Error (LoadProhibited at 0x00000000) during boot

**Cause**: `start_provisioning()` returned `true` even when device wasn't connected (was starting provisioning mode), causing main.cpp to call `printf("WiFi connected: %s", get_ip_address())` with NULL IP.

**Solution**: Changed `start_provisioning()` to `return false;` since device isn't actually connected yet.

**File**: `include/modules/wifi_provisioning.h:218`

---

## Issue 2: Credential Submission Shows "Connecting" But Nothing Happens

**Symptom**: User submits WiFi credentials through captive portal, sees "Connecting..." spinner, but nothing happens.

**Cause**: `provision()` was called synchronously in the HTTP handler, which:
1. Stopped the HTTP server immediately
2. Changed WiFi mode from APSTA to STA
3. Browser never received the full response

**Solution**: Added 500ms delay timer before applying credentials:
```cpp
esp_timer_start_once(self->provision_timer_, 500000);  // 500ms delay
```

**File**: `include/modules/wifi_provisioning.h` - `configure_handler()` and `provision_timer_callback()`

---

## Issue 3: Connection Timeout After Credential Submission

**Symptom**: Credentials received and saved, but connection times out after 10 seconds with no `wifi:state:` transitions.

**Cause**: When switching from APSTA to STA mode, `esp_wifi_start()` doesn't trigger `WIFI_EVENT_STA_START` (since STA was already running), so `esp_wifi_connect()` was never called.

**Solution**: Explicitly call `esp_wifi_connect()` after `esp_wifi_start()`:
```cpp
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
ESP_ERROR_CHECK(esp_wifi_start());
ESP_ERROR_CHECK(esp_wifi_connect());  // MUST explicitly connect
```

**File**: `include/modules/wifi_provisioning.h:348-351` - `connect_sta()`

---

## Issue 4: Password Not Decoded from URL Encoding

**Symptom**: Correct password submitted but authentication fails (4-way handshake timeout).

**Cause**: Form data is URL-encoded (spaces become `+`, special chars become `%XX`), but we weren't decoding it.

**Solution**: Added URL decode function:
```cpp
static void url_decode(char* str) {
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, nullptr, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
```

**File**: `include/modules/wifi_provisioning.h:603-618`

---

## Issue 5: SmartConfig Crash

**Symptom**: Device crashes when SmartConfig starts scanning.

**Cause**: SmartConfig not enabled in sdkconfig.

**Solution**: Add to `sdkconfig.defaults`:
```
CONFIG_ESP_SMARTCONFIG_ENABLE=y
```

---

## Issue 6: Captive Portal Not Detected by Phone/Browser

**Symptom**: Connect to AP but captive portal doesn't automatically appear.

**Cause**: Missing DNS server to redirect all DNS queries to the AP IP.

**Solution**: Added UDP DNS server that responds to all queries with 192.168.4.1:
- Creates UDP socket on port 53
- Runs in separate FreeRTOS task
- Responds to all DNS queries with AP IP

**File**: `include/modules/wifi_provisioning.h` - `start_dns_server()` and `dns_server_task()`

---

## Issue 7: HTTPS Certificate Embedding Fails

**Symptom**: Undefined reference to `_binary_certs_server_crt_start`.

**Cause**: PlatformIO's `embed_txtfiles` doesn't work properly with ESP-IDF framework.

**Solution**: Reverted to HTTP for now. HTTPS requires proper cmake configuration:
```cmake
# In CMakeLists.txt
idf_component_register(
    ...
    EMBED_TXTFILES certs/server.crt certs/server.key
)
```

**Status**: Deferred - marked as TODO in platformio.ini

---

## Issue 8: Serial Buffer Overflow Errors

**Symptom**: `E (xxxxx) usb_serial_jtag: usb_serial_jtag_write_bytes(267): invalid buffer or size`

**Cause**: USB Serial/JTAG driver installed twice (by ESP-IDF VFS and our ESP32Serial class), causing conflicts.

**Solution**: Check if driver already installed before installing, increase buffer, add validation:
```cpp
bool begin(uint32_t baud) override {
    // Check if driver is already installed (by ESP-IDF console)
    if (usb_serial_jtag_is_driver_installed()) {
        initialized_ = true;
        return true;
    }
    // ... install driver with larger buffers (512 instead of 256)
}

size_t write(const uint8_t* data, size_t len) override {
    if (!initialized_ || !data || len == 0) return 0;  // Validate inputs
    // ...
}
```

**File**: `include/hal/esp32/esp32_serial.h`

---

## Issue 9: LED Colors Swapped (Red/Green)

**Symptom**: LED shows RED when it should show GREEN and vice versa.

**Cause**: The WS2812 LED on the Waveshare ESP32-S3 board has R and G channels swapped compared to standard GRB format.

**Solution**: Swap R and G values in the led_strip_set_pixel call:
```cpp
// Swap R and G - LED appears to be RGB despite GRB format
led_strip_set_pixel(led_strip_, 0, g, r, b);  // NOT (r, g, b)
```

**File**: `include/modules/status_led.h:324`

---

## Issue 10: Uninitialized State Struct

**Symptom**: Status LED shows wrong color (e.g., red instead of green).

**Cause**: `State` struct members weren't explicitly initialized, could contain garbage values.

**Solution**: Add default initializers to struct members:
```cpp
struct State {
    bool provisioning = false;
    bool ap_client_connected = false;
    bool wifi_connected = false;
    bool ntp_synced = false;
    bool has_errors = false;
    bool has_warnings = false;
    bool has_alarms = false;
};
```

**File**: `include/modules/status_led.h:46-54`

---

## Issue 11: Duplicate esp_wifi_connect Calls

**Symptom**: `E (595) wifi:sta is connecting, return error` during boot

**Cause**: `esp_wifi_connect()` called both in `connect_sta()` and in WIFI_EVENT_STA_START handler.

**Solution**: Skip event handler's connect when already in CONNECTING state:
```cpp
if (event_id == WIFI_EVENT_STA_START) {
    // Only connect if not already connecting (connect_sta handles it explicitly)
    if (self->state_ != State::CONNECTING) {
        esp_wifi_connect();
    }
}
```

**File**: `include/modules/wifi_provisioning.h` - `wifi_event_handler()`

---

## Issue 12: Status Command Shows Zero Values

**Symptom**: `status` command shows Free heap: 0, WiFi RSSI: 0, NTP synced: no

**Cause**: StateManager values not updated in control loop.

**Solution**: Add update calls in main.cpp control_loop:
```cpp
g_state.update_free_heap(esp_get_free_heap_size());
g_state.update_wifi_rssi(g_wifi_prov->get_rssi());
g_state.update_system_ntp_status(g_ntp->is_synced());
```

**Files**:
- `src/main.cpp` - control_loop
- `include/core/state_manager.h` - added `update_free_heap()`
- `include/modules/wifi_provisioning.h` - added `get_rssi()`

---

## Issue 13: DI6-8 Floating HIGH When Disconnected

**Symptom**: Digital inputs DI6, DI7, DI8 show HIGH when nothing is connected (should be LOW).

**Cause**: GPIO9, 10, 11 are ESP32-S3 strapping pins with external pull-ups on the Waveshare board. Internal pull-down resistors (~45kΩ) cannot overcome the external pull-ups (~10kΩ).

**Solution**: Software inversion for these specific pins:
```cpp
bool get_digital_input(uint8_t input_id) const override {
    if (input_id >= MAX_INPUTS) return false;
    bool state = gpio_get_level((gpio_num_t)DI_PINS[input_id]) == 1;
    // Invert DI6-8 (indices 5-7) - these have external pull-ups
    if (input_id >= 5) {
        state = !state;
    }
    return state;
}
```

**File**: `include/hal/esp32/esp32_gpio.h`

---

## Issue 14: RSSI Shows 0 After `wifi set`

**Symptom**: After connecting via `wifi set <ssid> <pass>`, `status` shows WiFi RSSI: 0 dBm

**Cause**: `provision()` method overwrites state after `connect_sta()` succeeds:
```cpp
if (connect_sta(ssid, password)) {
    state_ = State::PROVISIONING_DONE;  // Overwrites CONNECTED!
    return true;
}
```

The `get_rssi()` method checks for `state_ == State::CONNECTED` and returns 0 otherwise.

**Solution**: Don't overwrite the CONNECTED state set by `connect_sta()`:
```cpp
if (connect_sta(ssid, password)) {
    provision_method_ = ProvisionMethod::CAPTIVE_PORTAL;
    // state_ is already CONNECTED from connect_sta()
    return true;
}
```

**File**: `include/modules/wifi_provisioning.h` - `provision()`

---

## Issue 15: `wifi clear` Requires Reboot

**Symptom**: After `wifi clear`, device prints "Reboot to enter provisioning mode" instead of starting provisioning.

**Cause**: The command only cleared credentials but didn't start provisioning mode.

**Solution**: Call `start()` after clearing credentials:
```cpp
} else if (strcmp(args[1], "clear") == 0) {
    wifi_prov_->clear_credentials();
    serial_->println("WiFi credentials cleared");
    serial_->println("Starting provisioning mode...");
    wifi_prov_->start();  // Start provisioning immediately
}
```

**File**: `include/modules/debug_console.h` - `cmd_wifi()`

---

## Issue 16: WiFi Disconnect Not Persistent

**Symptom**: Need ability to disconnect WiFi and stay disconnected across reboots.

**Solution**: Added auto-connect flag persisted to NVS:
- `wifi disconnect` - disconnects and sets `auto_connect=false` in NVS
- `wifi connect` - sets `auto_connect=true` and connects
- On boot, `start()` checks `auto_connect_` before connecting

```cpp
void disconnect() {
    esp_wifi_disconnect();
    esp_wifi_stop();
    state_ = State::IDLE;
    set_auto_connect(false);  // Persists to NVS
}
```

**File**: `include/modules/wifi_provisioning.h` - added `disconnect()`, `set_auto_connect()`, `auto_connect_` member

---

## Issue 17: GPIO/TCA9554 Not Initialized

**Symptom**: Digital outputs (DO1-DO8) don't persist state - shows ON but reads back as OFF.

**Cause**: `g_gpio.initialize()` was never called in main.cpp. The TCA9554 I2C expander was never configured.

**Solution**: Add GPIO initialization in `system_init()`:
```cpp
// Initialize GPIO (TCA9554 for outputs, GPIO4-11 for inputs)
if (!g_gpio.initialize()) {
    printf("WARNING: GPIO initialization failed\n");
}
```

**File**: `src/main.cpp` - `system_init()`

---

## Issue 18: modules Command Shows Wrong WiFi Status

**Symptom**: `modules` command shows `[ ] WiFi + HTTP Server` even when WiFi is working.

**Cause**: Command checked for `WIFI_HTTP_ENABLED` flag instead of `WIFI_NTP_ENABLED`.

**Solution**: Separate WiFi/NTP from HTTP Server in modules output:
```cpp
#ifdef WIFI_NTP_ENABLED
    serial_->println("  [x] WiFi + NTP");
#else
    serial_->println("  [ ] WiFi + NTP");
#endif

#ifdef HTTP_ENABLED
    serial_->println("  [x] HTTP Server");
#else
    serial_->println("  [ ] HTTP Server");
#endif
```

**File**: `include/modules/debug_console.h` - `cmd_modules()`

---

## Issue 19: NTP Not Resyncing After WiFi Reconnect

**Symptom**: LED stays yellow after `wifi disconnect` then `wifi connect`. NTP shows not synced.

**Cause**: NTP sync only happens at boot. When WiFi disconnects and reconnects, NTP doesn't automatically resync.

**Solution**: Detect WiFi reconnection in control loop and trigger NTP resync:
```cpp
static bool was_connected = false;
if (g_wifi_prov) {
    bool is_connected = g_wifi_prov->is_connected();

    // Trigger NTP resync when WiFi reconnects
    if (is_connected && !was_connected && g_ntp) {
        ESP_LOGI("Main", "WiFi reconnected, triggering NTP resync");
        g_ntp->resync();
    }
    was_connected = is_connected;
}
```

**File**: `src/main.cpp` - `control_loop()`

---

## Issue 20: SmartConfig Error Spam After Disconnect

**Symptom**: After `wifi disconnect`, console fills with `smartconfig errno 12290` errors.

**Cause**: SmartConfig not stopped when disconnecting WiFi.

**Solution**: Stop SmartConfig in `disconnect()`:
```cpp
void disconnect() {
    esp_smartconfig_stop();  // Stop SmartConfig
    esp_wifi_disconnect();
    esp_wifi_stop();
    // ...
}
```

**File**: `include/modules/wifi_provisioning.h` - `disconnect()`

---

## Issue 21: `wifi connect` Fails When Already Connected

**Symptom**: `wifi connect` while already connected causes disconnect and provisioning mode.

**Cause**: `start()` tries to reconnect even when already connected.

**Solution**: Check if already connected at start of `start()`:
```cpp
if (state_ == State::CONNECTED) {
    ESP_LOGI("Prov", "Already connected");
    return true;
}
```

**File**: `include/modules/wifi_provisioning.h` - `start()`

---

## Best Practices

### 1. WiFi Event Handling
- When calling `esp_wifi_connect()` explicitly (e.g., in `connect_sta()`), skip the STA_START event handler
- Check state before calling connect to avoid duplicate calls

### 2. HTTP Handlers
- Never do long-running operations synchronously in HTTP handlers
- Always use timers or tasks to delay operations that affect the connection

### 3. Form Data Processing
- Always URL-decode form data before use
- Handle both `+` (space) and `%XX` encodings

### 4. State Initialization
- Always initialize struct members with default values
- Don't rely on `memset` alone for class members

### 5. Captive Portal
- DNS server is essential for automatic portal detection
- Redirect common detection URLs: `/generate_204`, `/hotspot-detect.html`, etc.

### 6. Testing
- Use serial logger to capture boot and connection sequences
- Check for `wifi:state:` transitions to verify connection attempts
- Log credentials received (SSID only, not password) to verify form parsing

### 7. StateManager Updates
- Update all relevant system values in control_loop (heap, RSSI, NTP status)
- Create getter methods in modules (e.g., `get_rssi()`) for StateManager to query
- Use `#ifdef ESP32_BUILD` for ESP32-specific functions like `esp_get_free_heap_size()`

### 8. USB Serial/JTAG
- Check `usb_serial_jtag_is_driver_installed()` before installing
- Use larger buffers (512 bytes) to avoid overflow
- Validate buffer and length before write calls

### 9. State Management in WiFi
- Don't overwrite state after calling methods that set it (e.g., `connect_sta()` sets CONNECTED)
- Verify state checks match the actual states used (CONNECTED vs PROVISIONING_DONE)
- Use consistent state transitions throughout the code

### 10. Hardware-Specific GPIO Workarounds
- Some GPIO pins have external pull-ups/pull-downs that override internal resistors
- Document pin-specific inversions clearly in code comments
- Apply inversions at the HAL layer for clean abstraction

---

## Key Files Modified

| File | Changes |
|------|---------|
| `include/modules/wifi_provisioning.h` | All provisioning logic, DNS server, URL decoding, timer-based provisioning, `get_rssi()`, duplicate connect fix, `disconnect()`, `set_auto_connect()`, state overwrite fix |
| `include/modules/status_led.h` | State struct initialization, R/G color swap fix, AP client connected state |
| `include/modules/debug_console.h` | WiFi commands, `output` command, `modules` fix, build hash (6-char), I2C check, date/time display, reconnection detection (30s timeout) |
| `include/hal/esp32/esp32_gpio.h` | DI6-8 inversion, `check_i2c()` method |
| `src/main.cpp` | GPIO initialization, CET timezone, WiFi/NTP/LED integration, StateManager updates |
| `include/hal/esp32/esp32_serial.h` | USB Serial/JTAG driver check, buffer size increase, input validation |
| `include/core/state_manager.h` | Added `update_free_heap()` |
| `src/core/state_manager.cpp` | Implemented `update_free_heap()` |
| `sdkconfig.defaults` | SmartConfig enable |

---

## Debug Commands

```bash
# Serial logger
python tools/serial_logger.py /dev/ttyACM0 115200

# Debug console commands
status              # System overview (I2C, time, heap, RSSI, NTP)
wifi                # WiFi state, IP, SSID
wifi connect        # Connect to stored network (enables auto-connect)
wifi disconnect     # Disconnect (persistent across reboots)
wifi set <s> <p>    # Set credentials and connect
wifi clear          # Clear credentials and start provisioning
inputs              # Show digital inputs DI1-8
output <1-8> [on|off] # Control digital outputs DO1-8
modules             # Show compiled modules and features
alarms              # Active alarms
help                # All commands

# Reset ESP32 via serial
python -c "import serial; s=serial.Serial('/dev/ttyACM0', 115200); s.setDTR(False); s.setRTS(True); import time; time.sleep(0.1); s.setRTS(False); s.setDTR(True); s.close()"
```

---

## Connection Flow

1. Boot → Load stored credentials from NVS
2. Try stored credentials → `connect_sta()` → `esp_wifi_connect()`
3. If fails → Start provisioning (AP + SmartConfig + DNS)
4. User connects to AP → DNS redirects to captive portal
5. User submits credentials → URL decode → Store in `pending_creds_`
6. Timer fires (500ms) → `provision()` → Save to NVS → `connect_sta()`
7. If connected → Set `state_ = CONNECTED` → LED turns green
