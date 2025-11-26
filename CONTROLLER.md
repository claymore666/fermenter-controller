# Fermentation Controller - Software Design
## Modular ESP32-S3 Control System

---

## System Overview

**Platform:** ESP32-S3 (Dual-core, 80MHz default with power management, 16MB Flash, 8MB PSRAM)
**OS:** FreeRTOS (ESP-IDF 5.x)
**Language:** C/C++
**Architecture:** Modular plugin system with core scheduler
**Capacity:** 8 fermenters with temperature and pressure control
**Power Mode:** Auto light sleep with dynamic frequency scaling (10-80MHz)

### Core Design Principles

1. **Modularity** - Each subsystem is an independent module
2. **Persistence** - Power-outage safe operation via NVS
3. **Real-time** - FreeRTOS tasks with priority scheduling
4. **Configuration-driven** - JSON-based system configuration
5. **API-first** - RESTful interface with OAuth2 security

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32-S3 Controller                     │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                    Core System                        │   │
│  │  - Config Loader (JSON/NVS)                          │   │
│  │  - Task Scheduler (FreeRTOS)                         │   │
│  │  - State Manager (centralized state)                 │   │
│  │  - Event Bus (inter-module communication)            │   │
│  └──────────────────────────────────────────────────────┘   │
│                          ▲                                    │
│                          │                                    │
│  ┌───────────┬──────────┼──────────┬──────────┬──────────┐  │
│  │           │          │          │          │          │  │
│  ▼           ▼          ▼          ▼          ▼          ▼  │
│ ┌──────┐ ┌──────┐ ┌────────┐ ┌──────┐ ┌──────┐ ┌────────┐ │
│ │MODBUS│ │ REST │ │  MQTT  │ │ PID  │ │ NTP  │ │Display │ │
│ │Module│ │  API │ │ Module │ │Ctrl  │ │Sync  │ │Module  │ │
│ └──────┘ └──────┘ └────────┘ └──────┘ └──────┘ └────────┘ │
│     │        │         │         │        │         │       │
│  ┌──┴────────┴─────────┴─────────┴────────┴─────────┴───┐  │
│  │          Hardware Abstraction Layer (HAL)            │  │
│  │  - IModbusInterface  - IGPIOInterface                │  │
│  │  - IStorageInterface - INetworkInterface            │  │
│  └──────────────────────────────────────────────────────┘  │
│                          │                                    │
└──────────────────────────┼────────────────────────────────────┘
                           │
        ┌──────────────────┴──────────────────┐
        │                                     │
        ▼                                     ▼
   ┌─────────┐                         ┌──────────┐
   │ ESP32   │                         │Simulator │
   │Hardware │                         │(Testing) │
   └─────────┘                         └──────────┘
   │    │    │                         │    │    │
   ▼    ▼    ▼                         ▼    ▼    ▼
MODBUS GPIO NVS                    Socket File RAM
Sensors Relays WiFi                Python Virtual Mock
```

**Key Design Principles:**
- **HAL enables testing:** Same business logic runs on ESP32 and native
- **Dependency injection:** Modules receive interfaces, not implementations
- **90% testable without hardware:** Core logic + simulator = fast development
- **Production & testing builds:** Same codebase, different HAL implementations

---

## Core System Components

### 1. Configuration Loader

**Function:** Load system configuration from NVS or JSON files

**Configuration Hierarchy:**
- `system_config` - Hardware definitions (MODBUS devices, GPIO pins)
- `fermenter_config` - Fermenter definitions and sensor mappings
- `network_config` - WiFi, MQTT broker, API settings
- `security_config` - OAuth secrets, JWT keys
- `timing_config` - All intervals, timeouts, and durations

**Storage:**
- Boot config in NVS (survives firmware updates)
- Runtime changes via REST API → persisted to NVS
- Factory reset loads defaults from compiled-in JSON

### Timing Configuration

**All timing values configurable via JSON:**

```json
{
  "timing": {
    "modbus": {
      "poll_interval_ms": 5000,
      "timeout_ms": 1000,
      "retry_delay_ms": 500,
      "max_retries": 3
    },
    "pid": {
      "calculation_interval_ms": 5000,
      "output_cycle_time_ms": 60000,
      "autotune_max_duration_min": 120
    },
    "fermentation_plan": {
      "check_interval_ms": 60000,
      "step_transition_delay_ms": 0
    },
    "sensors": {
      "quality_bad_timeout_ms": 300000,
      "default_filter": {
        "type": "ema",
        "window_size": 5,
        "ema_alpha": 0.3
      }
    },
    "safety": {
      "check_interval_ms": 1000,
      "temp_deviation_timeout_ms": 900000,
      "pressure_check_interval_ms": 2000,
      "alarm_cooldown_ms": 60000
    },
    "mqtt": {
      "publish_interval_ms": 10000,
      "reconnect_delay_ms": 5000,
      "keepalive_s": 60
    },
    "ntp": {
      "sync_interval_h": 1,
      "boot_sync_timeout_ms": 10000
    },
    "api": {
      "token_expire_min": 60,
      "request_timeout_ms": 30000
    },
    "display": {
      "update_interval_ms": 1000,
      "websocket_ping_interval_ms": 30000
    },
    "watchdog": {
      "hardware_timeout_ms": 30000,
      "task_health_check_ms": 60000
    },
    "persistence": {
      "state_snapshot_interval_ms": 60000,
      "nvs_commit_delay_ms": 100
    }
  }
}
```

**Runtime Updates:**
- `PUT /config/timing` - Update timing configuration
- Changes applied immediately (some require task restart)
- Validated against min/max bounds before applying
- Critical timings (watchdog) have safety limits

### 2. Task Scheduler

**FreeRTOS Task Priorities:**
- Priority 5: MODBUS polling (time-critical)
- Priority 4: PID control loops
- Priority 3: Safety monitors (pressure limits, temp limits)
- Priority 2: HTTP/MQTT communication
- Priority 1: Display updates

**Task Intervals:**
All intervals loaded from `timing_config` at boot and runtime-updatable via API:
- MODBUS sensors: `timing.modbus.poll_interval_ms`
- PID calculation: `timing.pid.calculation_interval_ms`
- Fermentation plan check: `timing.fermentation_plan.check_interval_ms`
- MQTT publish: `timing.mqtt.publish_interval_ms`
- NTP sync: `timing.ntp.sync_interval_h`

**Dynamic Timing Updates:**
- Config changes trigger task parameter updates
- No task restart required for interval changes
- Bounds checking: min/max values enforced

### 3. State Manager

**Centralized State Store:**
```
state {
  sensors: {
    glycol_supply_temp: {
      base_samples, base_average, extra_samples, extra_average,
      filtered_value, display_value,
      timestamp, unit, quality, filter_type, samples_per_second
    }
    glycol_return_temp: { /* same structure */ }
    fermenter_1_temp: { /* same structure */ }
    fermenter_2_temp: { /* same structure */ }
    fermenter_3_temp: { /* same structure */ }
    fermenter_4_temp: { /* same structure */ }
    fermenter_5_temp: { /* same structure */ }
    fermenter_6_temp: { /* same structure */ }
    fermenter_7_temp: { /* same structure */ }
    fermenter_8_temp: { /* same structure */ }
    fermenter_1_pressure: { /* same structure */ }
    fermenter_2_pressure: { /* same structure */ }
    fermenter_3_pressure: { /* same structure */ }
    fermenter_4_pressure: { /* same structure */ }
    fermenter_5_pressure: { /* same structure */ }
    fermenter_6_pressure: { /* same structure */ }
    fermenter_7_pressure: { /* same structure */ }
    fermenter_8_pressure: { /* same structure */ }
  }
  
  relays: {
    glycol_chiller: {state, last_change}
    fermenter_1_cooling: {state, last_change, duty_cycle}
    fermenter_1_spunding: {state, last_change}
    fermenter_2_cooling: {state, last_change, duty_cycle}
    fermenter_2_spunding: {state, last_change}
    fermenter_3_cooling: {state, last_change, duty_cycle}
    fermenter_3_spunding: {state, last_change}
    fermenter_4_cooling: {state, last_change, duty_cycle}
    fermenter_4_spunding: {state, last_change}
    fermenter_5_cooling: {state, last_change, duty_cycle}
    fermenter_5_spunding: {state, last_change}
    fermenter_6_cooling: {state, last_change, duty_cycle}
    fermenter_6_spunding: {state, last_change}
    fermenter_7_cooling: {state, last_change, duty_cycle}
    fermenter_7_spunding: {state, last_change}
    fermenter_8_cooling: {state, last_change, duty_cycle}
    fermenter_8_spunding: {state, last_change}
    heater: {state, last_change}
  }
  
  fermenters: {
    F1: {
      current_temp, target_temp, current_step,
      plan_active, plan_start_time, hours_remaining
    }
    F2: { /* same structure */ }
    F3: { /* same structure */ }
    F4: { /* same structure */ }
    F5: { /* same structure */ }
    F6: { /* same structure */ }
    F7: { /* same structure */ }
    F8: { /* same structure */ }
  }
  
  system: {
    uptime, last_boot, ntp_synced, wifi_rssi
  }
}
```

**Thread Safety:** Protected by FreeRTOS mutexes

### 4. Event Bus

**Inter-Module Communication:**
- Publish/Subscribe pattern
- Events: `SENSOR_UPDATE`, `RELAY_CHANGE`, `PLAN_STEP_CHANGE`, `ALARM`
- Modules register callbacks for events they care about
- Decouples modules (MQTT module doesn't know about MODBUS directly)

---

## Module Specifications

### Hardware Abstraction Layer (HAL)

**Purpose:** Decouple hardware-specific code from business logic for testability

**Design Philosophy:**
- Core logic depends on interfaces, not implementations
- Hardware implementations inject dependencies
- Enables native testing without ESP32 hardware
- Allows MODBUS simulator for integration testing

**Interface Definitions:**

```cpp
// hal/interfaces.h

class IModbusInterface {
public:
    virtual ~IModbusInterface() = default;
    
    virtual bool read_holding_registers(
        uint8_t slave_addr,
        uint16_t start_reg,
        uint16_t count,
        uint16_t* data
    ) = 0;
    
    virtual bool write_register(
        uint8_t slave_addr,
        uint16_t reg,
        uint16_t value
    ) = 0;
    
    virtual uint32_t get_transaction_count() = 0;
    virtual uint32_t get_error_count() = 0;
};

class IGPIOInterface {
public:
    virtual ~IGPIOInterface() = default;
    
    virtual void set_relay(uint8_t relay_id, bool state) = 0;
    virtual bool get_relay_state(uint8_t relay_id) = 0;
    virtual bool get_digital_input(uint8_t pin) = 0;
};

class IStorageInterface {
public:
    virtual ~IStorageInterface() = default;
    
    virtual bool write_blob(const char* key, const void* data, size_t len) = 0;
    virtual bool read_blob(const char* key, void* data, size_t* len) = 0;
    virtual bool erase_key(const char* key) = 0;
};

class INetworkInterface {
public:
    virtual ~INetworkInterface() = default;
    
    virtual bool connect(const char* ssid, const char* password) = 0;
    virtual bool is_connected() = 0;
    virtual bool disconnect() = 0;
    virtual const char* get_ip_address() = 0;
};
```

**ESP32 Implementations:**

```cpp
// hal/esp32/modbus_esp32.h
class ESP32ModbusInterface : public IModbusInterface {
private:
    uart_port_t uart_port;
    mb_master_handle_t mb_handle;
    
public:
    ESP32ModbusInterface(uart_port_t port, int baudrate);
    bool read_holding_registers(...) override;
    // Uses ESP-IDF MODBUS library
};

// hal/esp32/gpio_esp32.h
class ESP32GPIOInterface : public IGPIOInterface {
    void set_relay(uint8_t relay_id, bool state) override {
        gpio_set_level((gpio_num_t)relay_id, state ? 1 : 0);
    }
};

// hal/esp32/storage_esp32.h
class ESP32StorageInterface : public IStorageInterface {
private:
    nvs_handle_t nvs_handle;
    
public:
    bool write_blob(...) override {
        return nvs_set_blob(nvs_handle, key, data, len) == ESP_OK;
    }
};
```

**Simulator Implementations:**

```cpp
// hal/simulator/modbus_simulator.h
class ModbusSimulatorInterface : public IModbusInterface {
private:
    int socket_fd;  // Unix socket to Python simulator
    std::string simulator_endpoint;
    
public:
    ModbusSimulatorInterface(const char* endpoint);
    
    bool read_holding_registers(
        uint8_t slave_addr,
        uint16_t start_reg,
        uint16_t count,
        uint16_t* data
    ) override {
        // Send request via Unix socket to Python simulator
        // Receive realistic MODBUS response with timing
        // Parse and return data
    }
};

// hal/simulator/gpio_simulator.h
class GPIOSimulatorInterface : public IGPIOInterface {
private:
    std::map<uint8_t, bool> relay_states;
    
public:
    void set_relay(uint8_t relay_id, bool state) override {
        relay_states[relay_id] = state;
        // Log to file or send to monitoring dashboard
        printf("[GPIO_SIM] Relay %d: %s\n", relay_id, state ? "ON" : "OFF");
    }
};

// hal/simulator/storage_simulator.h
class StorageSimulatorInterface : public IStorageInterface {
private:
    std::map<std::string, std::vector<uint8_t>> storage;
    
public:
    bool write_blob(const char* key, const void* data, size_t len) override {
        std::vector<uint8_t> vec((uint8_t*)data, (uint8_t*)data + len);
        storage[key] = vec;
        return true;
    }
};
```

**Dependency Injection:**

```cpp
// main.cpp (ESP32 target)
void app_main() {
    // Create hardware implementations
    auto modbus = std::make_unique<ESP32ModbusInterface>(UART_NUM_1, 115200);
    auto gpio = std::make_unique<ESP32GPIOInterface>();
    auto storage = std::make_unique<ESP32StorageInterface>();
    
    // Inject into modules
    auto modbus_module = ModbusModule(modbus.get());
    auto state_manager = StateManager(storage.get());
    
    // Start system
    system_start();
}

// main_simulator.cpp (native testing target)
int main() {
    // Create simulator implementations
    auto modbus = std::make_unique<ModbusSimulatorInterface>("/tmp/modbus.sock");
    auto gpio = std::make_unique<GPIOSimulatorInterface>();
    auto storage = std::make_unique<StorageSimulatorInterface>();
    
    // Inject into modules (same code as ESP32!)
    auto modbus_module = ModbusModule(modbus.get());
    auto state_manager = StateManager(storage.get());
    
    // Start system
    system_start();
}
```

**Benefits:**
- ✅ Core logic testable without hardware (90% of codebase)
- ✅ Same business logic runs on ESP32 and native
- ✅ Fast development cycles (compile natively in seconds)
- ✅ Realistic integration testing with MODBUS simulator
- ✅ Easy to add new hardware platforms (Raspberry Pi, etc.)

**Trade-offs:**
- ⚠️ Slightly more complex architecture (~10% more code)
- ⚠️ Virtual function call overhead (negligible on ESP32-S3)
- ⚠️ Must maintain interface contracts

---

### MODBUS Module

**Responsibilities:**
- Poll PT1000 temperature module (0x01)
- Poll analog input module (0x02)
- Execute intelligent poll scheduler with multi-rate sampling
- Convert raw values to engineering units
- Apply sensor filtering (smoothing)
- Publish `SENSOR_UPDATE` events to Event Bus
- Handle communication errors (retry logic)

**Configuration:**
```json
{
  "modbus": {
    "uart_port": "/dev/ttyS1",
    "baudrate": 115200,
    "polling": {
      "strategy": "interleaved_multi_rate",
      "base_samples_per_second": 3,
      "min_sample_interval_ms": 100,
      "bulk_read": true,
      "schedule_optimization": "auto"
    },
    "devices": [
      {
        "address": 1,
        "type": "pt1000_8ch",
        "name": "PT1000 Module 1",
        "registers": [
          {"name": "glycol_supply", "reg": 0, "scale": 0.1, "priority": "low"},
          {"name": "glycol_return", "reg": 1, "scale": 0.1, "priority": "low"},
          {"name": "fermenter_1_temp", "reg": 2, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_2_temp", "reg": 3, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_3_temp", "reg": 4, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_4_temp", "reg": 5, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_5_temp", "reg": 6, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_6_temp", "reg": 7, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5}
        ]
      },
      {
        "address": 2,
        "type": "analog_8ch",
        "name": "Analog Input Module",
        "registers": [
          {"name": "fermenter_1_pressure", "reg": 0, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_2_pressure", "reg": 1, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_3_pressure", "reg": 2, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_4_pressure", "reg": 3, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_5_pressure", "reg": 4, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_6_pressure", "reg": 5, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_7_pressure", "reg": 6, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15},
          {"name": "fermenter_8_pressure", "reg": 7, "scale": 0.01, "transform": "(x-400)/1600*3", "priority": "critical", "extra_samples_per_second": 15}
        ]
      },
      {
        "address": 3,
        "type": "relay_16ch",
        "name": "MODBUS Relay Module",
        "comment": "Waveshare 16CH MODBUS Relay for fermenters 4-8",
        "relays": [
          {"name": "fermenter_4_cooling", "reg": 0},
          {"name": "fermenter_4_spunding", "reg": 1},
          {"name": "fermenter_5_cooling", "reg": 2},
          {"name": "fermenter_5_spunding", "reg": 3},
          {"name": "fermenter_6_cooling", "reg": 4},
          {"name": "fermenter_6_spunding", "reg": 5},
          {"name": "fermenter_7_cooling", "reg": 6},
          {"name": "fermenter_7_spunding", "reg": 7},
          {"name": "fermenter_8_cooling", "reg": 8},
          {"name": "fermenter_8_spunding", "reg": 9},
          {"name": "reserve_1", "reg": 10},
          {"name": "reserve_2", "reg": 11}
        ]
      },
      {
        "address": 4,
        "type": "pt1000_8ch",
        "name": "PT1000 Module 2 (optional)",
        "comment": "For fermenters 7-8 if needed",
        "registers": [
          {"name": "fermenter_7_temp", "reg": 0, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5},
          {"name": "fermenter_8_temp", "reg": 1, "scale": 0.1, "priority": "high", "extra_samples_per_second": 5}
        ]
      }
    ]
  },
  "gpio": {
    "esp32_relays": {
      "DO1": {"name": "glycol_chiller", "type": "contactor_coil"},
      "DO2": {"name": "fermenter_1_cooling", "type": "solenoid_nc"},
      "DO3": {"name": "fermenter_1_spunding", "type": "solenoid_no"},
      "DO4": {"name": "fermenter_2_cooling", "type": "solenoid_nc"},
      "DO5": {"name": "fermenter_2_spunding", "type": "solenoid_no"},
      "DO6": {"name": "fermenter_3_cooling", "type": "solenoid_nc"},
      "DO7": {"name": "fermenter_3_spunding", "type": "solenoid_no"},
      "DO8": {"name": "heater_contactor", "type": "contactor_coil"}
    }
  }
}
```

**Note:** For 8 fermenters:
- PT1000 Module 1 (0x01): Glycol (2) + Fermenters 1-6 (6) = 8 channels used
- Analog Module (0x02): Fermenters 1-8 pressure = 8 channels used
- MODBUS Relay Module (0x03): Fermenters 4-8 control = 10 relays used
- PT1000 Module 2 (0x04): Optional for Fermenters 7-8 if PT1000 Module 1 full
- ESP32 Built-in: Chiller + Fermenters 1-3 + Heater = 8 relays used

**Timing:** All intervals from `timing.modbus.*` config

**Error Handling:**
- Retries: `timing.modbus.max_retries`
- Retry delay: `timing.modbus.retry_delay_ms`
- Mark sensor as `quality: BAD` if persistent failure
- Trigger `ALARM` event if critical sensor fails

---

### Intelligent Poll Scheduler

**Design Goal:** Maximize sensor quality while respecting timing constraints

**Core Requirements:**
1. **Every sensor gets 3 samples per second** (configurable)
2. **Minimum 100ms between samples** per sensor (configurable)
3. **Optimal bus utilization** during idle periods
4. **Priority-based extra sampling** for critical sensors
5. **Fully configurable** for field testing

**Scheduler Algorithm:**

```
Phase 1: Base Sampling Schedule (guaranteed for all sensors)
─────────────────────────────────────────────────────────────
Timeline (1000ms):

t=0ms:     Bulk Read PT1000 (8 sensors, Sample 1)     - 5ms
t=5ms:     Bulk Read Analog  (8 sensors, Sample 1)    - 5ms
           ↓ 323ms idle window

t=333ms:   Bulk Read PT1000 (8 sensors, Sample 2)     - 5ms
t=338ms:   Bulk Read Analog  (8 sensors, Sample 2)    - 5ms
           ↓ 323ms idle window

t=666ms:   Bulk Read PT1000 (8 sensors, Sample 3)     - 5ms
t=671ms:   Bulk Read Analog  (8 sensors, Sample 3)    - 5ms
           ↓ 324ms idle window until next cycle

Total: 30ms transactions, 970ms available for extras
Bus utilization (base only): 3%
Sample spacing per sensor: 333ms (>> 100ms requirement ✓)
```

**Phase 2: Interleaved Extra Sampling (fills idle windows)**

```
Available time slots per cycle:
├─ Window 1: 10ms - 333ms  (323ms, ~64 transactions possible)
├─ Window 2: 343ms - 666ms (323ms, ~64 transactions)
└─ Window 3: 676ms - 1000ms (324ms, ~64 transactions)

Total capacity: ~192 extra transactions per second

Priority allocation (8 fermenters):
├─ Critical sensors (8× pressure): 15 extra samples each = 120 trans
├─ High sensors (8× fermenter temp): 5 extra samples each = 40 trans
├─ Low sensors (2× glycol): 0 extra samples
└─ Remaining capacity: 32 transactions unused (headroom for retries)
```

**Example Schedule with Priorities (8 Fermenters):**

```
0ms:    [BULK] PT1000-1 All (Sample 1)                  - 5ms
5ms:    [BULK] PT1000-2 All (Sample 1)                  - 5ms
10ms:   [BULK] Analog All (Sample 1)                    - 5ms

20ms:   [SINGLE] F1 Pressure                            - 5ms
25ms:   [SINGLE] F2 Pressure                            - 5ms
30ms:   [SINGLE] F3 Pressure                            - 5ms
35ms:   [SINGLE] F4 Pressure                            - 5ms
40ms:   [SINGLE] F5 Pressure                            - 5ms
45ms:   [SINGLE] F6 Pressure                            - 5ms
50ms:   [SINGLE] F7 Pressure                            - 5ms
55ms:   [SINGLE] F8 Pressure                            - 5ms
60ms:   [SINGLE] F1 Temp                                - 5ms
65ms:   [SINGLE] F2 Temp                                - 5ms
70ms:   [SINGLE] F3 Temp                                - 5ms
75ms:   [SINGLE] F4 Temp                                - 5ms
80ms:   [SINGLE] F5 Temp                                - 5ms
85ms:   [SINGLE] F6 Temp                                - 5ms
90ms:   [SINGLE] F7 Temp                                - 5ms
95ms:   [SINGLE] F8 Temp                                - 5ms
100ms:  [SINGLE] F1 Pressure                            - 5ms
...
(pattern repeats to fill 318ms window)

333ms:  [BULK] PT1000-1 All (Sample 2)                  - 5ms
338ms:  [BULK] PT1000-2 All (Sample 2)                  - 5ms
343ms:  [BULK] Analog All (Sample 2)                    - 5ms
...
(window 2 fills with extra samples)

666ms:  [BULK] PT1000-1 All (Sample 3)                  - 5ms
671ms:  [BULK] PT1000-2 All (Sample 3)                  - 5ms
676ms:  [BULK] Analog All (Sample 3)                    - 5ms
...
(window 3 fills with extra samples)

1000ms: Cycle complete, publish averaged values
```

---

### Configuration Options

**Timing Configuration:**
```json
{
  "timing": {
    "modbus": {
      "scheduler": {
        "base_cycle_ms": 1000,
        "base_samples_per_cycle": 3,
        "min_sample_spacing_ms": 100,
        "transaction_time_ms": 5,
        "bulk_read_enabled": true
      },
      "optimization": {
        "fill_idle_windows": true,
        "respect_priorities": true,
        "balance_load": true
      }
    }
  }
}
```

**Per-Sensor Priority Levels:**
```json
{
  "priority": "critical|high|normal|low",
  "extra_samples_per_second": 0-50,
  "require_base_samples": true
}
```

**Priority Definitions:**
- `critical`: Max extra samples (15-20/s) - for spunding pressure control
- `high`: Moderate extras (5-10/s) - for PID temperature control  
- `normal`: Few extras (1-3/s) - for monitoring
- `low`: Base only (3/s) - for slow-changing values

---

### Scheduler Implementation Details

**Scheduler State Machine:**

```
1. Calculate Base Schedule
   ├─ Divide cycle time by base_samples
   ├─ Place bulk reads at equal intervals
   └─ Ensure min_sample_spacing respected

2. Identify Idle Windows
   ├─ Time between bulk reads
   └─ Subtract transaction overhead

3. Allocate Extra Samples
   ├─ Sort sensors by priority
   ├─ Distribute extras proportionally
   ├─ Fill windows round-robin
   └─ Respect timing constraints

4. Generate Transaction Timeline
   ├─ Merge base + extra schedules
   ├─ Optimize transaction order
   └─ Minimize MODBUS address switching

5. Execute & Monitor
   ├─ FreeRTOS timer triggers transactions
   ├─ Track actual timing vs planned
   ├─ Adjust dynamically if bus overload
   └─ Publish stats via API
```

**Dynamic Adjustment:**
- If transactions take longer than expected (>5ms):
  - Reduce extra samples automatically
  - Maintain base samples always
  - Log warning via API
- If bus errors increase:
  - Slow down temporarily
  - Re-calculate schedule

**Transaction Pipelining:**
```
Optimization: Don't wait for response before next request
├─ Send Request 1
├─ (Device processes, ~2ms)
├─ Send Request 2 (while waiting for Response 1)
├─ Receive Response 1
└─ Receive Response 2

Reduces idle time, increases throughput 30-40%
Only for different device addresses (MODBUS spec compliant)
```

---

### Data Aggregation Strategy

**Multi-Rate Data Management:**

```
sensor_state {
  // Base samples (guaranteed 3/s, hardware averaged)
  base_samples: [float; 3]
  base_average: float
  base_last_update: timestamp
  
  // Extra samples (variable rate, for responsiveness)
  extra_samples: circular_buffer[16]
  extra_average: float  // Rolling average
  extra_last_update: timestamp
  
  // Combined filtered value
  filtered_value: float  // EMA over all samples
  
  // Display value (extra smoothed)
  display_value: float  // EMA over base_average
  
  // Quality indicators
  base_quality: GOOD|WARMING_UP|BAD
  extra_quality: GOOD|BAD
  
  // Statistics
  samples_per_second: float
  actual_interval_ms: float
}
```

**Value Selection for Consumers:**

| Consumer | Uses | Rationale |
|----------|------|-----------|
| PID Controller | `filtered_value` | Balance of quality + responsiveness |
| Spunding Controller | `extra_average` | Fastest response, lightly filtered |
| Display (UI) | `display_value` | Heavily smoothed, aesthetic |
| Safety Alarms | `extra_samples[latest]` | Fastest detection |
| Data Logging | `base_average` | Best quality, consistent rate |
| API `/raw` | `extra_samples[latest]` | Real-time debugging |

---

### Bus Utilization Analysis

**Scenario 1: Base Only (Conservative)**
```
Config:
- 3 samples/s for all 18 sensors (2 glycol + 8 temp + 8 pressure)
- Bulk reads only

Bus load: 
  - 3× PT1000-1 bulk (8 channels) = 15ms
  - 3× PT1000-2 bulk (2 channels) = 15ms
  - 3× Analog bulk (8 channels) = 15ms
  Total: 45ms/1000ms = 4.5%

Update rate: 3 Hz (all sensors)
Quality: Excellent (hardware averaged)
Response: Moderate (333ms between samples)
```

**Scenario 2: Balanced (Recommended for 8 Fermenters)**
```
Config:
- 3 base samples/s (all)
- +15 extra for 8× pressure sensors
- +5 extra for 8× fermenter temps

Bus load: 
  Base: 45ms
  Extras: (15×8 + 5×8) × 5ms = 800ms
  Total: 845ms/1000ms = 84.5%

Update rates:
  Pressure: 18 Hz (3 base + 15 extra)
  Fermenter Temp: 8 Hz (3 base + 5 extra)
  Glycol Temp: 3 Hz (base only)

Quality: Excellent base + responsive extras
Response: Fast (<60ms for critical sensors)
Margin: 15.5% headroom for retries
```

**Scenario 3: Conservative Extras (Better Margin)**
```
Config:
- 3 base samples/s (all)
- +10 extra for 8× pressure sensors
- +3 extra for 8× fermenter temps

Bus load: 
  Base: 45ms
  Extras: (10×8 + 3×8) × 5ms = 520ms
  Total: 565ms/1000ms = 56.5%

Update rates:
  Pressure: 13 Hz (3 base + 10 extra)
  Fermenter Temp: 6 Hz (3 base + 3 extra)
  Glycol Temp: 3 Hz (base only)

Quality: Excellent
Response: Good (<80ms)
Margin: 43.5% headroom (recommended for 8 fermenters)
```

---

### Sensor Filtering System

**Purpose:** Reduce noise while maintaining responsiveness for control loops

**Filter Types:**

**1. Moving Average (Simple)**
```
Filtered_value = (Sample[n] + Sample[n-1] + ... + Sample[n-k]) / k
```
- **Use case:** Temperature sensors with slow dynamics
- **Config:** `window_size` (3-10 samples)
- **Pros:** Simple, effective noise reduction
- **Cons:** Lag = window_size × avg_sample_interval
- **Example:** `window_size: 5` at 333ms base interval = 1.67s lag

**2. Exponential Moving Average (EMA) - Recommended**
```
Filtered_value = alpha × Sample[n] + (1-alpha) × Filtered_value[n-1]
```
- **Use case:** All sensors requiring balance of smoothing + responsiveness
- **Config:** `alpha` (0.0-1.0)
  - `alpha = 1.0`: No filtering (raw value)
  - `alpha = 0.5`: Moderate smoothing
  - `alpha = 0.1`: Heavy smoothing
- **Pros:** Low memory, minimal lag, mathematically optimal
- **Cons:** Requires tuning per sensor
- **Recommended:**
  - Temperature (base samples): `alpha = 0.3` (smooth, slow response OK)
  - Temperature (with extras): `alpha = 0.5` (balanced)
  - Pressure: `alpha = 0.7` (responsive for spunding)

**3. Median Filter**
```
Filtered_value = median(Sample[n], Sample[n-1], ..., Sample[n-k])
```
- **Use case:** Sensors prone to spike outliers
- **Config:** `window_size` (odd number: 3, 5, 7)
- **Pros:** Excellent outlier rejection
- **Cons:** Higher CPU, moderate lag
- **Example:** Pressure sensors with electrical noise

**4. Dual-Rate Filter (Advanced)**
```
Base_filter = EMA(base_samples, alpha=0.3)    // Quality
Extra_filter = EMA(extra_samples, alpha=0.7)  // Speed
Combined = 0.7 × Extra_filter + 0.3 × Base_filter
```
- **Use case:** Sensors with both base and extra samples
- **Pros:** Best of both worlds (quality + speed)
- **Cons:** More complex configuration

**Filter Selection Guide:**

| Sensor Type | Samples/s | Recommended Filter | Config | Rationale |
|-------------|-----------|-------------------|--------|-----------|
| Fermenter Temp | 3 base + 5 extra | Dual-Rate EMA | base_α=0.3, extra_α=0.5 | Balance quality + PID response |
| Glycol Supply/Return | 3 base only | Moving Average | window=3 | Very stable, can tolerate lag |
| Fermenter Pressure | 3 base + 15 extra | EMA on extras | α=0.7 | Fast response for spunding |
| Ambient Temp | 3 base only | Moving Average | window=5 | Very slow changes |

**Configuration Per Sensor:**
```json
{
  "filter": {
    "type": "none|moving_average|ema|median|dual_rate",
    "enabled": true,
    
    // For moving_average, median
    "window_size": 5,
    
    // For EMA
    "alpha": 0.3,
    
    // For dual_rate (when extra samples configured)
    "base_alpha": 0.3,
    "extra_alpha": 0.7,
    "blend_ratio": 0.7  // Weight of extra_filter
  }
}
```

**Filter State Persistence:**
- Filter buffers stored in RAM only (not NVS)
- On boot: Buffers empty, first few samples may be unstable
- Warmup period: ~(window_size × avg_interval) until filter converges
- Initial value: First raw sample used as seed

**Quality Indicators:**
- During warmup: `quality: WARMING_UP`
- After warmup: `quality: GOOD`
- If raw value changes >10% in one sample: `quality: SUSPECT` (potential spike)
- Median filter automatically handles spikes

---

### Display Integration

**Display Update Strategy:**

```json
{
  "display": {
    "update_interval_ms": 1000,  // 1 Hz refresh
    "data_source": "display_value",
    "smoothing": "heavy"
  }
}
```

**Display Update Cycle:**
```
Every 1000ms:
├─ Fetch all sensor.display_value from State Manager
├─ Update UI elements (temps, pressures, graphs)
├─ Publish via WebSocket to connected clients
└─ Update local LVGL widgets (if applicable)

Synchronization with Poll Scheduler:
├─ Display refresh triggered by scheduler "cycle complete" event
├─ Ensures fresh data (just after 3rd base sample averaged)
└─ No tearing or partial updates
```

**Benefits:**
- Display always shows averaged, high-quality values
- 1 Hz refresh perfect for human perception
- No flicker from rapid sensor updates
- Synchronized with polling cycle

---

### Runtime Monitoring & Tuning

**API Endpoints for Scheduler:**

```
GET /modbus/scheduler/status
{
  "cycle_time_ms": 1000,
  "bus_utilization": 0.33,
  "transactions_per_second": 66,
  "sensors": [
    {
      "name": "fermenter_1_pressure",
      "samples_per_second": 18,
      "actual_interval_ms": 55.6,
      "quality": "GOOD",
      "priority": "critical"
    }
  ],
  "timing_violations": 0,
  "overruns": 0
}

PUT /modbus/scheduler/config
{
  "base_samples_per_second": 3,
  "min_sample_spacing_ms": 100,
  "priorities": {
    "fermenter_1_pressure": {"priority": "critical", "extras": 20}
  }
}

POST /modbus/scheduler/optimize
{
  "mode": "balanced|aggressive|conservative"
}
```

**Diagnostic Logging:**
```
[MODBUS] Cycle 1234 complete: 987ms actual (1000ms target)
[MODBUS] Bus utilization: 34% (340ms/1000ms)
[MODBUS] Sensor F1_pressure: 18.2 samples/s (18 target)
[MODBUS] Timing violations: 0, Retries: 2
```

**Auto-Tuning Mode:**
- Monitor actual transaction times over 100 cycles
- Adjust schedule if systematic deviations detected
- Reduce extras if bus overload risk
- Increase extras if consistent headroom available

---

### Performance Guarantees

**Hard Guarantees (Always Maintained):**
- ✅ Every sensor gets minimum 3 samples per second
- ✅ Minimum 100ms between samples (same sensor)
- ✅ Base samples always hardware-averaged (high quality)
- ✅ Power-safe operation (schedule persists in code, not NVS)

**Soft Targets (Best Effort):**
- 🎯 Critical sensors: 15-20 Hz
- 🎯 High sensors: 8-10 Hz
- 🎯 Bus utilization: <75% (margin for retries)
- 🎯 Timing jitter: <5ms (real-time OS)

**Fallback Behavior:**
- If bus errors increase: Disable extras, maintain base only
- If transactions take too long: Reduce extras dynamically
- If scheduler overruns: Log warning, skip non-critical extras
- Safety always maintained: Base samples never compromised

---

### Memory & CPU Budget

**RAM Usage:**
```
Per sensor:
├─ Base samples buffer: 3 × 4 bytes = 12 bytes
├─ Extra samples buffer: 16 × 4 bytes = 64 bytes
├─ Filter state: ~20 bytes
└─ Total per sensor: ~96 bytes

18 sensors (2 glycol + 8 temp + 8 pressure): 1,728 bytes
Scheduler state: ~500 bytes
Total MODBUS module: ~2.2 KB (negligible)
```

**CPU Usage:**
```
Per cycle (1000ms):
├─ Scheduler execution: ~1ms
├─ MODBUS transactions: 340ms (blocking UART)
├─ Filter calculations: ~2ms
├─ Event publishing: ~1ms
└─ Total: ~344ms = 34.4% one core

Leaves >60% for other tasks (PID, API, MQTT, Display)
```

---

### Configuration Examples

**Conservative (Safe Default):**
```json
{
  "modbus": {
    "polling": {
      "base_samples_per_second": 3,
      "min_sample_spacing_ms": 100,
      "bulk_read": true
    },
    "all_sensors": {
      "priority": "normal",
      "extra_samples_per_second": 0,
      "filter": {"type": "ema", "alpha": 0.3}
    }
  }
}
```
Result: 3% bus load, 3 Hz all sensors, excellent quality

**Balanced (Recommended for Production):**
```json
{
  "modbus": {
    "polling": {
      "base_samples_per_second": 3,
      "min_sample_spacing_ms": 100,
      "bulk_read": true,
      "fill_idle_windows": true
    },
    "sensors": {
      "pressures": {
        "priority": "critical",
        "extra_samples_per_second": 15,
        "filter": {"type": "ema", "alpha": 0.7}
      },
      "fermenter_temps": {
        "priority": "high",
        "extra_samples_per_second": 5,
        "filter": {"type": "dual_rate", "base_alpha": 0.3, "extra_alpha": 0.5}
      },
      "glycol_temps": {
        "priority": "low",
        "extra_samples_per_second": 0,
        "filter": {"type": "moving_average", "window_size": 3}
      }
    }
  }
}
```
Result: 33% bus load, 18/8/3 Hz rates, optimal for control

**Aggressive (Maximum Performance):**
```json
{
  "modbus": {
    "polling": {
      "base_samples_per_second": 3,
      "min_sample_spacing_ms": 50,
      "bulk_read": true,
      "fill_idle_windows": true,
      "optimize_transaction_order": true
    },
    "sensors": {
      "all_critical": {
        "extra_samples_per_second": 30
      }
    }
  }
}
```
Result: 70% bus load, 33 Hz all sensors, maximum responsiveness

### REST API Module

**Framework:** ESP-IDF HTTP Server (lightweight)

**Authentication:** OAuth2 Bearer Token (JWT)
- Token endpoint: `POST /token` (username/password → JWT)
- All other endpoints require `Authorization: Bearer <token>`
- Tokens expire after 1 hour (configurable)
- Secrets stored in NVS

**Endpoints:**

**Sensors:**
- `GET /sensors` - List all sensors with current values
- `GET /sensors/{id}` - Get specific sensor (includes raw, filtered, display values)
- `GET /sensors/{id}/raw` - Get only raw value (for debugging)
- `GET /sensors/{id}/history?hours=24` - Historical data (if logged)
- `PUT /sensors/{id}/filter` - Update filter configuration
- `GET /sensors/{id}/filter` - Get current filter settings

**Relays:**
- `GET /relays` - List all relays
- `POST /relays/{id}/on` - Turn relay on
- `POST /relays/{id}/off` - Turn relay off

**Fermenters:**
- `GET /fermenters` - List all fermenters
- `GET /fermenters/{id}` - Get fermenter state
- `PUT /fermenters/{id}/setpoint` - Manual temperature setpoint
- `POST /fermenters/{id}/plan` - Upload fermentation plan
- `GET /fermenters/{id}/plan` - Get current plan and progress
- `DELETE /fermenters/{id}/plan` - Stop/clear plan

**PID:**
- `POST /fermenters/{id}/pid/autotune` - Start autotuning
- `PUT /fermenters/{id}/pid/params` - Manual PID parameters
- `GET /fermenters/{id}/pid/params` - Get current PID parameters

**System:**
- `GET /system/status` - Uptime, memory, WiFi, NTP status
- `POST /system/reboot` - Reboot controller
- `GET /config` - Get full system config
- `PUT /config` - Update system config

**CORS:** Enabled for web UI access from browsers

### MQTT Module

**Protocol:** MQTT 3.1.1 (esp-mqtt library)

**Topics Structure:**
```
brewery/fermentation/
  ├── status                    (online/offline - LWT)
  ├── F1/temperature           (current value)
  ├── F1/pressure              (current value)
  ├── F1/setpoint              (target temp)
  ├── F1/plan/step             (current step number)
  ├── F1/plan/remaining_hours  (time left)
  ├── F2/temperature           (current value)
  ├── F2/pressure              (current value)
  ├── F2/setpoint              (target temp)
  ├── F2/plan/step             (current step number)
  ├── F2/plan/remaining_hours  (time left)
  ├── F3/temperature           (current value)
  ├── F3/pressure              (current value)
  ├── F3/setpoint              (target temp)
  ├── F3/plan/step             (current step number)
  ├── F3/plan/remaining_hours  (time left)
  ├── F4/temperature           (current value)
  ├── F4/pressure              (current value)
  ├── F4/setpoint              (target temp)
  ├── F4/plan/step             (current step number)
  ├── F4/plan/remaining_hours  (time left)
  ├── F5/temperature           (current value)
  ├── F5/pressure              (current value)
  ├── F5/setpoint              (target temp)
  ├── F5/plan/step             (current step number)
  ├── F5/plan/remaining_hours  (time left)
  ├── F6/temperature           (current value)
  ├── F6/pressure              (current value)
  ├── F6/setpoint              (target temp)
  ├── F6/plan/step             (current step number)
  ├── F6/plan/remaining_hours  (time left)
  ├── F7/temperature           (current value)
  ├── F7/pressure              (current value)
  ├── F7/setpoint              (target temp)
  ├── F7/plan/step             (current step number)
  ├── F7/plan/remaining_hours  (time left)
  ├── F8/temperature           (current value)
  ├── F8/pressure              (current value)
  ├── F8/setpoint              (target temp)
  ├── F8/plan/step             (current step number)
  ├── F8/plan/remaining_hours  (time left)
  ├── relays/glycol_chiller    (on/off)
  └── system/uptime            (seconds)
```

**Home Assistant MQTT Discovery:**
- Auto-publish discovery messages on connect
- Creates sensors/switches automatically in HA
- Discovery prefix: `homeassistant/`

**QoS Levels:**
- Sensor values: QoS 0 (fire and forget)
- Commands: QoS 1 (at least once delivery)
- Retained: Last state retained for subscribers

**Command Topics:**
```
brewery/fermentation/F1/setpoint/set      → Set temperature (F1)
brewery/fermentation/F2/setpoint/set      → Set temperature (F2)
brewery/fermentation/F3/setpoint/set      → Set temperature (F3)
brewery/fermentation/F4/setpoint/set      → Set temperature (F4)
brewery/fermentation/F5/setpoint/set      → Set temperature (F5)
brewery/fermentation/F6/setpoint/set      → Set temperature (F6)
brewery/fermentation/F7/setpoint/set      → Set temperature (F7)
brewery/fermentation/F8/setpoint/set      → Set temperature (F8)
brewery/fermentation/relays/{id}/set      → Control relay
```

### PID Controller Module

**Algorithm:** Standard PID with anti-windup

**Input:** Filtered temperature values from sensors (EMA recommended)

**Features:**
- Individual PID instance per fermenter
- Parameters stored in NVS per fermenter
- Manual tuning or automatic via relay method
- Output limiting (0-100% duty cycle)
- Derivative filter to reduce noise (redundant if sensor already filtered)

**Sensor Integration:**
- Uses `filtered_value` from state manager
- Recommended sensor filter: EMA with `alpha: 0.3`
- Derivative term benefits from pre-filtered input
- Integral windup protected (output limiting)

**PID Output → Relay Control:**
- Output 0%: Relay OFF (no cooling)
- Output 100%: Relay ON (full cooling)
- Time-proportional control: Cycle time from `timing.pid.output_cycle_time_ms`
  - Example: 30% output with 60s cycle = ON for 18s, OFF for 42s

**Autotuning Process:**
1. User triggers via API: `POST /fermenters/F1/pid/autotune`
2. System applies relay oscillation (±2°C around setpoint)
3. Measures oscillation period (Pu) and amplitude (A)
4. Max duration: `timing.pid.autotune_max_duration_min`
5. Calculates Ziegler-Nichols parameters:
   - Kp = 0.6 * Ku (where Ku = 4A / πd)
   - Ki = 1.2 * Ku / Pu
   - Kd = 0.075 * Ku * Pu
6. Saves parameters to NVS
7. Switches to normal PID mode

**Safety Limits:**
- Max cooling output: 100% (configurable per fermenter)
- Temperature deviation alarm: ±3°C from setpoint
- Disable PID if sensor quality is BAD

### Fermentation Plan Module

**Data Structure:**
```
FermentationPlan {
  fermenter_id: string
  plan_start_time: unix_timestamp
  current_step: uint8
  steps[]: [
    {
      duration_hours: uint32
      target_temp: float
      target_pressure: float
      name: string (optional, e.g. "Primary Fermentation")
    }
  ]
}
```

**Persistence:** NVS key per fermenter
- Key: `fermentation/F1/plan`
- Survives power outages
- Loaded on boot → resumes automatically

**Plan Execution:**
1. Calculate elapsed time since `plan_start_time`
2. Determine current step based on cumulative durations
3. Update PID setpoint to current step's `target_temp`
4. Update spunding valve to `target_pressure`
5. Publish `PLAN_STEP_CHANGE` event when transitioning steps
6. Mark plan complete when all steps finished

**Step Transitions:**
- Immediate: New setpoint applied instantly
- Ramped (future): Linear temperature ramp over first hour of step

**Pressure Control Integration:**
- Spunding valves use pressure sensor values
- Recommended: EMA with `alpha: 0.7-0.9` for fast response
- Hysteresis prevents valve chattering:
  - Open valve at: setpoint + 0.05 bar
  - Close valve at: setpoint - 0.05 bar
- Safety: Max pressure alarm at 2.5 bar (independent of filter)

**Example Plan - Lager:**
```json
{
  "fermenter_id": "F1",
  "start_time": "now",
  "steps": [
    {"name": "Pitch", "duration_hours": 24, "temp": 10.0, "pressure": 0.0},
    {"name": "Primary", "duration_hours": 168, "temp": 10.0, "pressure": 1.0},
    {"name": "Diacetyl Rest", "duration_hours": 72, "temp": 15.0, "pressure": 1.0},
    {"name": "Lager", "duration_hours": 720, "temp": 0.0, "pressure": 1.5}
  ]
}
```

### NTP Time Sync Module

**Purpose:** Accurate timestamps for fermentation plans

**Configuration:**
```json
{
  "ntp": {
    "enabled": true,
    "server": "pool.ntp.org",
    "timezone": "CET-1CEST,M3.5.0,M10.5.0/3"
  }
}
```

**Behavior:**
- Sync on boot (blocks for max `timing.ntp.boot_sync_timeout_ms`)
- Re-sync every `timing.ntp.sync_interval_h`
- If sync fails: continue with last known time
- Status flag: `ntp_synced` (exposed in API)

**Time Accuracy Requirements:**
- Fermentation plans: ±1 minute is sufficient
- No subsecond precision needed

**Timezone Support:**
- Handles DST transitions automatically
- All timestamps stored as UTC (Unix epoch)
- Local time conversion for display/logs

### Display Module

**Options:**

**Option A: WebSocket Server**
- ESP32 serves WebSocket on port 8081
- Web UI connects via `ws://controller.local:8081`
- Real-time sensor updates pushed to clients
- Runs on any tablet/touch screen with browser
- UI: React/Vue/Svelte SPA

**Option B: Native LVGL**
- Direct LVGL UI on attached TFT (e.g., 3.5" ILI9341)
- Capacitive touch input
- Renders locally on ESP32
- Lower latency, offline operation
- Higher RAM usage (~100KB for framebuffer)

**Recommended:** Option A (flexibility, easier updates)

**WebSocket Message Format:**
```json
{
  "type": "sensor_update",
  "data": {
    "fermenter_1_temp": {"value": 18.2, "unit": "°C"},
    "fermenter_1_pressure": {"value": 1.1, "unit": "bar"}
  }
}
```

**Display Content:**
- Overview: All fermenters (temp, pressure, step)
- Detail view per fermenter: Graph, plan progress
- Manual controls: Setpoint override, relay control
- System status: WiFi, NTP, uptime

---

## Data Persistence Strategy

### NVS (Non-Volatile Storage) Layout

**Namespace: `config`**
- `wifi_ssid`, `wifi_password`
- `mqtt_broker`, `mqtt_user`, `mqtt_pass`
- `oauth_secret`
- `system_config` (JSON blob)

**Namespace: `fermentation`**
- `F1/plan` - FermentationPlan struct
- `F1/pid_params` - PID coefficients (kp, ki, kd)
- `F2/plan`, `F2/pid_params`, etc.

**Namespace: `calibration`**
- `pt1000_offsets` - Per-sensor calibration offsets
- `pressure_offsets` - Per-sensor calibration

**Write Frequency:**
- PID params: Only when changed (infrequent)
- Fermentation plans: On upload or step change
- State snapshots: Every 60 seconds (optional, for analytics)

**Wear Leveling:** ESP-IDF NVS handles automatically (100k write cycles)

### Power Failure Recovery

**On Boot Sequence:**
1. Load system config from NVS
2. Initialize MODBUS, read sensors (validate hardware)
3. Load fermentation plans for each fermenter
4. Resume PID control at last known setpoints
5. Reconnect WiFi, MQTT, sync NTP
6. Continue fermentation plans from elapsed time

**Critical State Preserved:**
- Fermentation plan start time (resume correctly)
- Current step (no step skipping)
- PID parameters (tuned values retained)
- Relay states (fail-safe: all OFF on boot, then resume)

**Data Loss Window:**
- Maximum 60 seconds of sensor history (if logging enabled)
- No loss of fermentation plan progress (NVS updates on step change)

---

## Safety & Monitoring

### Safety Controller

**Independent FreeRTOS task (Priority 3)**

**Monitored Conditions:**
- Temperature excursion: ±3°C from setpoint for >`timing.safety.temp_deviation_timeout_ms`
- Pressure overpressure: >2.5 bar (fermenter failure risk)
- Sensor failures: Quality BAD for >`timing.sensors.quality_bad_timeout_ms`
- MODBUS communication loss: No response for >`timing.modbus.timeout_ms` × `timing.modbus.max_retries`

**Check Interval:** `timing.safety.check_interval_ms`

**Actions on Alarm:**
1. Log alarm event (with timestamp)
2. Publish `ALARM` event to Event Bus
3. Send MQTT alert to `brewery/fermentation/alarms`
4. Activate failsafe: Turn off all heating, open spunding valves
5. Send push notification (if configured via Home Assistant)

**Alarm Cooldown:** `timing.safety.alarm_cooldown_ms` between repeated alarms

**Manual Override:**
- API endpoint: `POST /system/override` - Disable safety limits (use with caution)
- Auto-restore after configurable timeout or manual clear

### Watchdog Timer

**Hardware Watchdog:** ESP32 built-in
- Timeout: `timing.watchdog.hardware_timeout_ms`
- Reset if main loop hangs
- Each task must "feed" watchdog periodically

**Software Watchdog:** Monitor task health
- Check interval: `timing.watchdog.task_health_check_ms`
- If MODBUS task doesn't update state → restart task
- If API server unresponsive → restart HTTP server

---

## Memory & Performance Budget

### Flash Usage (8MB total)
- Firmware + ESP-IDF: ~2MB
- TLS certificates: ~10KB
- NVS partition: 512KB
- OTA partition: 2MB (for updates)
- Available: ~3.5MB

### RAM Usage (512KB total)
- FreeRTOS heap: ~200KB
- Task stacks: ~50KB (8 tasks × 4-8KB each)
- Sensor filter buffers: ~3.5KB (18 sensors × 96 bytes per sensor)
- LVGL framebuffer (if used): ~100KB
- HTTP server buffers: ~32KB
- Available: ~126KB margin

### CPU Loading (80MHz with power management)
- MODBUS polling: <5% (one core)
- PID calculations: <1%
- HTTP/MQTT: <10%
- Display rendering: ~15% (if LVGL)
- **Total: <30% utilization**
- **Idle: ~0.2%** (with auto light sleep)

**Power Management:**
- Max frequency: 80MHz (reduced from 240MHz for power saving)
- Min frequency: 10MHz (during light sleep periods)
- Light sleep: Automatic during idle (WiFi connection maintained)
- Dynamic frequency scaling: Enabled

---

## Development & Testing

### Testing Strategy Overview

**Three-Tier Testing Approach:**

```
Tier 1: Unit Tests (Native)
├─ Pure logic without hardware
├─ Fast (seconds), CI/CD friendly
├─ 90% code coverage achievable
└─ Tests: PID, filters, scheduler, parsers

Tier 2: Integration Tests (Simulator)
├─ HAL + MODBUS Simulator
├─ Realistic hardware behavior
├─ Serial protocol verification
└─ Tests: MODBUS flows, state management

Tier 3: Hardware Tests (ESP32)
├─ Real hardware validation
├─ Timing verification
├─ Sensor calibration
└─ Tests: End-to-end scenarios
```

---

### Build System

**PlatformIO Configuration:**

```ini
; platformio.ini

# Native testing environment (Tier 1)
[env:native_test]
platform = native
build_flags = 
    -std=c++17
    -DUNIT_TEST
    -DNATIVE_BUILD
test_framework = unity
test_build_src = yes

# Simulator environment (Tier 2)
[env:simulator]
platform = native
build_flags = 
    -std=c++17
    -DSIMULATOR_BUILD
    -DHAL_SIMULATOR
lib_deps = 
    paulstoffregen/Time
test_framework = unity
test_build_src = yes

# ESP32 target (Tier 3)
[env:esp32]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
build_flags = 
    -DESP32_BUILD
    -DHAL_ESP32
lib_deps = 
    espressif/esp-modbus
monitor_speed = 115200
upload_protocol = esptool
test_framework = unity
```

**Build Commands:**

```bash
# Tier 1: Unit tests (no hardware)
pio test -e native_test

# Tier 2: Integration tests (with simulator)
pio test -e simulator

# Tier 3: Hardware tests
pio test -e esp32 --upload-port /dev/ttyUSB0

# Build for ESP32
pio run -e esp32

# Upload firmware
pio run -e esp32 -t upload

# OTA update
curl -X POST http://controller.local/ota \
     -H "Authorization: Bearer $TOKEN" \
     -F "firmware=@.pio/build/esp32/firmware.bin"
```

---

### MODBUS Simulator

**Purpose:** Provide realistic MODBUS RTU slave simulation for integration testing without hardware

**Architecture:**

```
┌─────────────────────────────────────────────────┐
│         Development Machine (Linux/Mac)          │
│                                                  │
│  ┌────────────────┐         ┌─────────────────┐ │
│  │ Fermenter Code │◄───────►│ MODBUS Simulator│ │
│  │  (C++ Native)  │ Virtual │  (Python)       │ │
│  │                │ Serial  │                  │ │
│  │ Uses HAL       │   or    │ Pymodbus Server │ │
│  │ Simulator Impl │ Socket  │ + Sensor Models │ │
│  └────────────────┘         └─────────────────┘ │
│                                                  │
│  Features:                                      │
│  ✅ PT1000 module (0x01): 8 temp channels       │
│  ✅ Analog module (0x02): 8 pressure channels   │
│  ✅ Realistic noise & drift simulation          │
│  ✅ Fermentation scenario profiles              │
│  ✅ Error injection (timeouts, corruption)      │
│  ✅ Accurate MODBUS RTU timing (115200 baud)    │
│  ✅ Runtime control via files or signals        │
└─────────────────────────────────────────────────┘
```

---

#### Implementation

**File Structure:**

```
test/modbus_simulator/
├── modbus_simulator.py          # Main simulator
├── sensor_models.py              # Realistic sensor behavior
├── scenarios.py                  # Fermentation profiles
├── error_injection.py            # Fault simulation
├── config.yaml                   # Configuration
└── README.md                     # Documentation
```

**Simulator Implementation:**

The MODBUS Simulator is already implemented and available in the project repository. See `SIMULATOR_DESCRIPTION.md` in the project files for:
- Complete architecture and implementation details
- Sensor models (PT1000 temperature, 4-20mA pressure)
- Fermentation scenario profiles (Lager, Ale)
- Error injection mechanisms
- Configuration options
- Usage examples

**Key Features:**
- Pymodbus-based MODBUS RTU server
- Realistic sensor behavior (noise, drift, physical constraints)
- Fermentation scenario simulation (time-accelerated or real-time)
- Runtime control via `/tmp/modbus_control` file
- Multiple slave addresses (0x01: PT1000, 0x02: Analog)
- Logging and diagnostics

---

#### Configuration & Setup

**Installation:**

```bash
# Install dependencies
pip3 install pymodbus pyserial

# Install socat for virtual serial ports
sudo apt install socat  # Ubuntu/Debian
brew install socat      # macOS
```

**Configuration File (config.yaml):**

```yaml
simulator:
  port: /dev/pts/2
  baudrate: 115200
  
sensors:
  temperature:
    noise_std: 0.5        # ±0.05°C
    drift_rate: 0.01      # 0.001°C per cycle
    
  pressure:
    noise_std: 5          # ±0.003 bar (in mA)
    co2_rate: 0.2         # Pressure increase rate
    
scenarios:
  lager:
    pitch_temp: 10.0
    ferment_temp: 12.0
    rest_temp: 18.0
    lager_temp: 0.0
    
  ale:
    ferment_temp: 20.0
    finish_temp: 18.0
    
error_injection:
  enabled: true
  timeout_probability: 0.005
  corruption_probability: 0.01
```

**Startup Script (start_simulator.sh):**

```bash
#!/bin/bash
# Start MODBUS simulator with virtual serial ports

set -e

echo "Starting MODBUS RTU Simulator..."

# Cleanup function
cleanup() {
    echo "Stopping simulator..."
    kill $SOCAT_PID $SIMULATOR_PID 2>/dev/null
    rm -f /tmp/modbus_master /tmp/modbus_slave /tmp/modbus_control
}

trap cleanup EXIT INT TERM

# Create virtual serial port pair
socat -d -d \
    pty,raw,echo=0,link=/tmp/modbus_master \
    pty,raw,echo=0,link=/tmp/modbus_slave \
    &
SOCAT_PID=$!

# Wait for ports
sleep 1

# Start simulator
python3 test/modbus_simulator/modbus_simulator.py /tmp/modbus_slave &
SIMULATOR_PID=$!

echo ""
echo "MODBUS Simulator Ready!"
echo "  Master (use in code): /tmp/modbus_master"
echo "  Slave (simulator):    /tmp/modbus_slave"
echo ""
echo "Control commands:"
echo "  echo 'SCENARIO:lager' > /tmp/modbus_control"
echo "  echo 'INJECT_TIMEOUT' > /tmp/modbus_control"
echo "  echo 'CLEAR_ERRORS' > /tmp/modbus_control"
echo ""
echo "Press Ctrl+C to stop"

# Keep running
wait
```

---

#### Runtime Control

**Scenario Control:**

```bash
# Start lager fermentation scenario
echo 'SCENARIO:lager' > /tmp/modbus_control

# Start ale fermentation scenario
echo 'SCENARIO:ale' > /tmp/modbus_control

# Reset to default (drift simulation)
echo 'SCENARIO:none' > /tmp/modbus_control
```

**Error Injection:**

```bash
# Inject MODBUS timeout (5 second delay)
echo 'INJECT_TIMEOUT' > /tmp/modbus_control

# Inject data corruption (10% error rate)
echo 'INJECT_CORRUPTION' > /tmp/modbus_control

# Clear all errors
echo 'CLEAR_ERRORS' > /tmp/modbus_control
```

**Monitoring:**

```bash
# Watch simulator logs
tail -f modbus_simulator.log

# Monitor sensor values
watch -n 1 'cat /tmp/modbus_control'

# Test with modpoll utility
modpoll -m rtu -b 115200 -p none -a 1 -r 0 -c 8 /tmp/modbus_master
```

---

#### Integration with C++ Code

**HAL Implementation (hal/simulator/modbus_simulator.cpp):**

```cpp
#include "modbus_simulator.h"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

ModbusSimulatorInterface::ModbusSimulatorInterface(const char* port) {
    // Open virtual serial port
    serial_fd = open(port, O_RDWR | O_NOCTTY);
    
    if (serial_fd < 0) {
        throw std::runtime_error("Failed to open serial port");
    }
    
    // Configure serial port (115200 8N1)
    struct termios tty;
    tcgetattr(serial_fd, &tty);
    
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    tty.c_cflag &= ~PARENB;  // No parity
    tty.c_cflag &= ~CSTOPB;  // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;       // 8 data bits
    
    tcsetattr(serial_fd, TCSANOW, &tty);
}

bool ModbusSimulatorInterface::read_holding_registers(
    uint8_t slave_addr,
    uint16_t start_reg,
    uint16_t count,
    uint16_t* data
) {
    // Build MODBUS RTU request
    uint8_t request[8];
    request[0] = slave_addr;
    request[1] = 0x03;  // Read Holding Registers
    request[2] = (start_reg >> 8) & 0xFF;
    request[3] = start_reg & 0xFF;
    request[4] = (count >> 8) & 0xFF;
    request[5] = count & 0xFF;
    
    // Add CRC
    uint16_t crc = calculate_crc(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Send request
    write(serial_fd, request, 8);
    
    // Read response
    uint8_t response[256];
    int bytes_read = read(serial_fd, response, sizeof(response));
    
    if (bytes_read < 5) {
        return false;  // Timeout or error
    }
    
    // Parse response
    if (response[0] != slave_addr || response[1] != 0x03) {
        return false;  // Invalid response
    }
    
    uint8_t byte_count = response[2];
    
    for (int i = 0; i < count; i++) {
        data[i] = (response[3 + i*2] << 8) | response[4 + i*2];
    }
    
    transaction_count++;
    return true;
}
```

**Usage in Tests:**

```cpp
// test/integration/test_modbus_simulator.cpp
#include "hal/simulator/modbus_simulator.h"
#include "modules/modbus_module.h"

void test_realistic_fermentation_scenario() {
    // Start simulator with lager scenario
    system("echo 'SCENARIO:lager' > /tmp/modbus_control");
    
    auto modbus = std::make_unique<ModbusSimulatorInterface>("/tmp/modbus_master");
    ModbusModule module(modbus.get());
    
    // Simulate 24 hours (fast)
    for (int hour = 0; hour < 24; hour++) {
        module.poll_cycle();
        
        float temp = module.get_sensor_value("fermenter_1_temp");
        
        // Verify temperature follows lager profile
        if (hour < 12) {
            TEST_ASSERT_FLOAT_WITHIN(1.0, 10.0, temp);  // Pitch temp
        } else {
            TEST_ASSERT_FLOAT_WITHIN(1.0, 12.0, temp);  // Ferment temp
        }
        
        // Advance simulator time (or wait)
        // In real test: sleep(3600) or use time acceleration
    }
}
```

---

#### Advantages Over Hardware

| Aspect | Hardware | Simulator |
|--------|----------|-----------|
| **Cost** | €150-500 | Free |
| **Setup Time** | 1-2 hours | 1 minute |
| **Reproducibility** | Variable | Perfect |
| **Scenarios** | Manual | Automated |
| **Error Testing** | Difficult | Easy |
| **CI/CD** | No | Yes |
| **Speed** | Real-time | Configurable |
| **Availability** | Limited | 24/7 |

**Realistic Behavior:**
- ✅ Accurate MODBUS RTU timing (5ms per transaction @ 115200 baud)
- ✅ Realistic sensor noise (Gaussian distribution)
- ✅ Physical constraints (temperature bounds, pressure limits)
- ✅ Fermentation dynamics (CO2 production, temperature drift)
- ✅ Error conditions (timeouts, corruption, bus errors)

**Development Speed:**
- Integration test cycle: ~30 seconds (vs. hours with real fermentation)
- Parallel development: Multiple developers can test simultaneously
- No hardware wear: Unlimited testing without equipment degradation

---

### Unit Testing (Tier 1)

**Test Structure:**

```
test/
├── native/
│   ├── test_pid_controller.cpp
│   ├── test_filters.cpp
│   ├── test_scheduler_logic.cpp
│   ├── test_fermentation_plan.cpp
│   └── test_config_parser.cpp
```

**Example Unit Test:**

```cpp
// test/native/test_pid_controller.cpp
#include <unity.h>
#include "controllers/pid_controller.h"

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

void test_pid_proportional_only() {
    PIDController pid(1.0, 0.0, 0.0);  // Kp=1, Ki=0, Kd=0
    
    float output = pid.compute(20.0, 18.0);  // Setpoint=20, Current=18
    
    TEST_ASSERT_EQUAL_FLOAT(2.0, output);  // Error=2, Output=Kp*Error
}

void test_pid_integral_windup() {
    PIDController pid(0.5, 0.1, 0.0);
    pid.set_output_limits(0, 100);
    
    // Drive to saturation
    for(int i = 0; i < 100; i++) {
        pid.compute(100.0, 10.0);  // Large error
    }
    
    // Should not overshoot when error reverses
    float output = pid.compute(10.0, 10.0);  // Error now zero
    TEST_ASSERT_LESS_THAN(10.0, output);  // No huge overshoot
}

void test_ema_filter_convergence() {
    EMAFilter filter(0.3);
    
    // Feed constant value
    for(int i = 0; i < 20; i++) {
        filter.update(100.0);
    }
    
    // Should converge to input
    TEST_ASSERT_FLOAT_WITHIN(0.1, 100.0, filter.get_value());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_pid_proportional_only);
    RUN_TEST(test_pid_integral_windup);
    RUN_TEST(test_ema_filter_convergence);
    return UNITY_END();
}
```

**Run Unit Tests:**
```bash
pio test -e native_test -v
```

---

### Integration Testing (Tier 2)

**Test Structure:**

```
test/
├── integration/
│   ├── test_modbus_polling.cpp
│   ├── test_state_management.cpp
│   ├── test_fermentation_scenario.cpp
│   └── test_error_recovery.cpp
├── modbus_simulator/
│   ├── modbus_simulator.py      # Complete simulator implementation
│   ├── config.yaml               # Configuration file
│   └── README.md                 # Documentation
└── start_simulator.sh
```

**Example Integration Test:**

```cpp
// test/integration/test_modbus_polling.cpp
#include <unity.h>
#include "hal/simulator/modbus_simulator.h"
#include "modules/modbus_module.h"

void test_read_temperature_sensors() {
    // Setup HAL with simulator
    auto modbus_hal = std::make_unique<ModbusSimulatorInterface>("/tmp/modbus_master");
    ModbusModule modbus(modbus_hal.get());
    
    // Configure module
    modbus.configure_device(0x01, "PT1000", 8);
    
    // Perform poll cycle
    bool success = modbus.poll_cycle();
    
    TEST_ASSERT_TRUE(success);
    
    // Verify sensor values are reasonable
    float temp = modbus.get_sensor_value("fermenter_1_temp");
    TEST_ASSERT_FLOAT_WITHIN(50.0, 18.0, temp);  // Should be ~18°C ±5
}

void test_scheduler_timing() {
    auto modbus_hal = std::make_unique<ModbusSimulatorInterface>("/tmp/modbus_master");
    PollScheduler scheduler(modbus_hal.get());
    
    scheduler.configure(3, 100);  // 3 samples/s, 100ms spacing
    
    auto start = std::chrono::steady_clock::now();
    scheduler.execute_cycle();
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should take ~1000ms for full cycle
    TEST_ASSERT_INT_WITHIN(50, 1000, duration.count());
}

void test_error_recovery() {
    auto modbus_hal = std::make_unique<ModbusSimulatorInterface>("/tmp/modbus_master");
    ModbusModule modbus(modbus_hal.get());
    
    // Simulator can inject errors via control file
    system("echo 'INJECT_TIMEOUT' > /tmp/modbus_control");
    
    bool success = modbus.poll_cycle();
    
    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQUAL(SENSOR_QUALITY_BAD, modbus.get_sensor_quality("fermenter_1_temp"));
    
    // Next cycle should recover
    system("echo 'CLEAR_ERRORS' > /tmp/modbus_control");
    success = modbus.poll_cycle();
    TEST_ASSERT_TRUE(success);
}
```

**Run Integration Tests:**
```bash
# Start simulator
./test/start_simulator.sh &

# Run tests
pio test -e simulator -v

# Stop simulator
pkill -f modbus_simulator.py
```

---

### Hardware Testing (Tier 3)

**Test on Real ESP32:**

```cpp
// test/hardware/test_esp32_modbus.cpp
#include <unity.h>
#include "hal/esp32/modbus_esp32.h"

void test_real_sensor_read() {
    // This test runs ON the ESP32
    auto modbus_hal = std::make_unique<ESP32ModbusInterface>(UART_NUM_1, 115200);
    
    uint16_t data[8];
    bool success = modbus_hal->read_holding_registers(0x01, 0, 8, data);
    
    TEST_ASSERT_TRUE(success);
    
    // Verify PT1000 values are in valid range
    for(int i = 0; i < 5; i++) {
        float temp = data[i] * 0.1;
        TEST_ASSERT_TRUE(temp > -20.0 && temp < 100.0);
    }
}
```

**Run Hardware Tests:**
```bash
# Upload and run
pio test -e esp32 --upload-port /dev/ttyUSB0

# Monitor output
pio device monitor -e esp32
```

---

### CI/CD Pipeline

**GitHub Actions:**

```yaml
# .github/workflows/test.yml
name: Automated Testing

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup PlatformIO
        run: |
          pip install platformio
      
      - name: Run Unit Tests
        run: pio test -e native_test
      
      - name: Generate Coverage
        run: |
          pio test -e native_test --coverage
          lcov --capture --directory . --output-file coverage.info
      
      - name: Upload Coverage
        uses: codecov/codecov-action@v3
        with:
          file: ./coverage.info

  integration-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y socat
          pip install platformio pymodbus
      
      - name: Start MODBUS Simulator
        run: |
          ./test/start_simulator.sh &
          sleep 2
      
      - name: Run Integration Tests
        run: pio test -e simulator
      
      - name: Stop Simulator
        run: pkill -f modbus_simulator.py

  build-firmware:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup PlatformIO
        run: pip install platformio
      
      - name: Build ESP32 Firmware
        run: pio run -e esp32
      
      - name: Upload Artifact
        uses: actions/upload-artifact@v3
        with:
          name: firmware
          path: .pio/build/esp32/firmware.bin
```

---

### Development Workflow

**Daily Development Cycle:**

```bash
# 1. Write new feature (e.g., PID autotune)
vim src/controllers/pid_controller.cpp

# 2. Write unit test
vim test/native/test_pid_autotune.cpp

# 3. Run unit tests (fast, seconds)
pio test -e native_test -f test_pid_autotune

# 4. Write integration test
vim test/integration/test_autotune_scenario.cpp

# 5. Start simulator
./test/start_simulator.sh &

# 6. Run integration test
pio test -e simulator -f test_autotune_scenario

# 7. Deploy to ESP32 (only when needed)
pio run -e esp32 -t upload

# 8. Manual testing on hardware
pio device monitor
```

**Development Speed:**
- Unit test cycle: ~5 seconds
- Integration test cycle: ~30 seconds
- Hardware upload: ~2 minutes
- **Result: 90% of development without hardware**

---

### Module Testing

**API Testing:**

```bash
# Use HTTPie or curl
http POST http://controller.local/token \
     username=admin password=secret

export TOKEN="eyJ..."

# Test sensor endpoints
http GET http://controller.local/sensors \
     "Authorization: Bearer $TOKEN"

# Test relay control
http POST http://controller.local/relays/glycol_chiller/on \
     "Authorization: Bearer $TOKEN"

# Test PID autotune
http POST http://controller.local/fermenters/F1/pid/autotune \
     "Authorization: Bearer $TOKEN"
```

**MQTT Testing:**

```bash
# Subscribe to all topics
mosquitto_sub -h homeassistant.local -t "brewery/#" -v

# Publish test command
mosquitto_pub -h homeassistant.local \
    -t "brewery/fermentation/F1/setpoint/set" \
    -m "18.5"
```

**Load Testing:**

```bash
# Apache Bench
ab -n 1000 -c 10 -H "Authorization: Bearer $TOKEN" \
   http://controller.local/sensors

# Expected: <100ms p95 latency, no errors
```

---

### Deployment
1. Flash firmware via USB (initial)
2. Configure WiFi via captive portal or serial
3. Upload system config via API
4. **Configure sensor filters:**
   - Start with conservative defaults (EMA α=0.5)
   - Monitor raw vs filtered values via API
   - Adjust based on noise level and response requirements
5. Calibrate sensors (ice bath, pressure reference)
6. Run PID autotune per fermenter
7. Upload fermentation plan and start

**Filter Tuning Guide:**
1. Read raw sensor values: `GET /sensors/{id}/raw`
2. Observe noise amplitude over 5 minutes
3. If noise >0.2°C: Apply filtering
4. Start with EMA α=0.5, decrease if more smoothing needed
5. For PID control: Target filtered noise <0.1°C
6. For pressure: Keep α>0.7 for responsive spunding

---

## Future Enhancements

### Phase 2
- **Data Logging:** Store sensor history in SPIFFS (graphs)
- **Brewfather Integration:** HTTP POST to Brewfather API
- **iSpindel Support:** Gravity tracking via WiFi hydrometers
- **CIP Automation:** Timer-based cleaning cycles
- **Multi-language UI:** i18n support (German, English)

### Phase 3
- **Bluetooth Mesh:** Multi-controller coordination
- **Advanced Scheduling:** Calendar-based plan activation
- **Recipe Database:** Integrated recipe → plan conversion
- **Mobile App:** Native iOS/Android control app

---

## Summary

**Architecture:** Modular C++ on ESP32-S3 with FreeRTOS + Hardware Abstraction Layer
**Power:** 80MHz with auto light sleep (10MHz min), WiFi power save enabled
**Core Features:**
- ✅ Configuration-driven hardware abstraction
- ✅ **Hardware Abstraction Layer (HAL)** for testability
  - Interface-based design (MODBUS, GPIO, Storage, Network)
  - ESP32 implementations for production
  - Simulator implementations for testing
  - 90% of code testable without hardware
- ✅ **MODBUS Simulator** for realistic integration testing
  - Python-based sensor simulation (existing implementation)
  - Fermentation scenario support (lager, ale profiles)
  - Error injection capabilities (timeouts, corruption)
  - See SIMULATOR_DESCRIPTION.md for details
- ✅ Intelligent multi-rate MODBUS polling scheduler
  - Guaranteed 3 base samples/s for all sensors (100ms spacing)
  - Priority-based extra sampling (up to 33 Hz for critical sensors)
  - Optimal bus utilization (3-75% configurable)
  - Hardware averaging + software filtering
- ✅ Advanced sensor filtering (Moving Avg, EMA, Median, Dual-Rate)
- ✅ REST API with admin web interface (OAuth2 pending)
- ✅ MQTT Home Assistant integration
- ✅ PID control with autotuning
- ✅ Fermentation plan execution (power-safe)
- ✅ NTP time synchronization
- ✅ Touch screen support (WebSocket or LVGL, 1Hz synchronized refresh)
- ✅ Centralized timing configuration (all values runtime-adjustable)

**Performance:** <35% CPU (0.2% idle), 204KB RAM, 2MB flash
**Reliability:** Power-outage safe, watchdog protected, safety monitoring
**Power:** 80MHz max, 10MHz min, auto light sleep with WiFi maintained

**Testing Strategy:**
- **Tier 1:** Unit tests (native, seconds, >80% coverage target)
- **Tier 2:** Integration tests (with MODBUS simulator, 30s cycles)
- **Tier 3:** Hardware validation (ESP32 + real sensors)
- **CI/CD:** Automated testing pipeline (GitHub Actions)
- **Development:** 90% without ESP32 hardware

**Sensor Quality:**
- Multi-rate sampling strategy (3-33 Hz per sensor)
- Hardware averaging (3 samples) + software filtering
- Balance between quality and responsiveness
- Per-sensor priority configuration
- Real-time diagnostics and auto-tuning
- Balance between smoothing and responsiveness
- Per-sensor configuration for optimal control

**Next Steps:**

**Phase 1: Foundation (Native Development)**
1. Define HAL interfaces (IModbusInterface, IGPIOInterface, IStorageInterface)
2. Implement core logic modules (native, hardware-independent):
   - Configuration loader (JSON/YAML parsing)
   - State Manager (centralized state)
   - PID Controller (algorithm + autotuning)
   - Filter implementations (EMA, Moving Average, Median)
   - Poll Scheduler logic
3. Write comprehensive unit tests (Tier 1)
4. Achieve >80% code coverage

**Phase 2: Simulation (Integration Testing)**
5. Use existing MODBUS Simulator (see SIMULATOR_DESCRIPTION.md)
   - Simulator already implements PT1000 and pressure sensors
   - Fermentation scenarios (lager, ale) already available
   - Error injection mechanisms already implemented
   - Setup virtual serial ports and test integration
6. Create HAL simulator implementations:
   - ModbusSimulatorInterface (virtual serial port)
   - GPIOSimulatorInterface (logging/monitoring)
   - StorageSimulatorInterface (in-memory map)
7. Write integration tests (Tier 2):
   - MODBUS polling flows
   - Fermentation scenario execution
   - Error recovery
8. Setup CI/CD pipeline (GitHub Actions)

**Phase 3: Hardware Implementation**
9. Implement ESP32 HAL implementations:
   - ESP32ModbusInterface (ESP-IDF MODBUS library)
   - ESP32GPIOInterface (GPIO driver)
   - ESP32StorageInterface (NVS)
   - ESP32NetworkInterface (WiFi)
10. Add REST API + OAuth2 (ESP HTTP Server)
11. Add MQTT module (esp-mqtt)
12. Integrate NTP sync

**Phase 4: Hardware Validation**
13. Hardware tests on ESP32-S3 with real MODBUS modules
14. Sensor calibration (PT1000, pressure sensors)
15. PID autotune with real fermenters (water test)
16. Timing validation and optimization

**Phase 5: UI & Deployment**
17. Develop WebSocket display server or LVGL UI
18. Build web dashboard (React/Vue/Svelte)
19. Field test with actual fermentation batches
20. Documentation and user guides

**Development Strategy:**
- 90% of development without ESP32 hardware (Phases 1-2)
- Fast iteration cycles (seconds for unit tests, 30s for integration)
- Hardware only needed for Phase 4 validation
- Parallel development possible (backend + simulator + UI)
