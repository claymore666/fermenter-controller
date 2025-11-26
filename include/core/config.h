#pragma once

#include "types.h"

namespace core {

/**
 * MODBUS timing configuration
 */
struct ModbusTimingConfig {
    uint32_t poll_interval_ms = 5000;
    uint32_t timeout_ms = 1000;
    uint32_t retry_delay_ms = 500;
    uint8_t max_retries = 3;
};

/**
 * Poll scheduler configuration
 */
struct SchedulerConfig {
    uint32_t base_cycle_ms = 1000;
    uint8_t base_samples_per_cycle = 3;
    uint32_t min_sample_spacing_ms = 100;
    uint32_t transaction_time_ms = 5;
    bool bulk_read_enabled = true;
    bool fill_idle_windows = true;
};

/**
 * PID timing configuration
 */
struct PIDTimingConfig {
    uint32_t calculation_interval_ms = 5000;
    uint32_t output_cycle_time_ms = 60000;
    uint32_t autotune_max_duration_min = 120;
};

/**
 * Sensor timing configuration
 */
struct SensorTimingConfig {
    uint32_t quality_bad_timeout_ms = 300000;
    FilterType default_filter = FilterType::EMA;
    uint8_t window_size = 5;
    float ema_alpha = 0.3f;
};

/**
 * Safety timing configuration
 */
struct SafetyTimingConfig {
    uint32_t check_interval_ms = 1000;
    uint32_t temp_deviation_timeout_ms = 900000;
    uint32_t pressure_check_interval_ms = 2000;
    uint32_t alarm_cooldown_ms = 60000;
    float max_temp_deviation = 3.0f;
    float max_pressure_bar = 2.5f;
};

/**
 * MQTT configuration
 */
struct MQTTConfig {
    char broker[64] = "";
    uint16_t port = 1883;
    char username[32] = "";
    char password[64] = "";
    char topic_prefix[32] = "brewery/fermentation";
    uint32_t publish_interval_ms = 10000;
    uint32_t reconnect_delay_ms = 5000;
    uint16_t keepalive_s = 60;
    bool enabled = false;
};

/**
 * NTP configuration
 */
struct NTPConfig {
    char server[64] = "pool.ntp.org";
    char timezone[48] = "UTC0";
    uint32_t sync_interval_h = 1;
    uint32_t boot_sync_timeout_ms = 10000;
    bool enabled = true;
};

/**
 * API configuration
 */
struct APIConfig {
    uint16_t port = 80;
    uint32_t token_expire_min = 60;
    uint32_t request_timeout_ms = 30000;
    char oauth_secret[64] = "";
    bool enabled = true;
};

/**
 * Display configuration
 */
struct DisplayConfig {
    uint32_t update_interval_ms = 1000;
    uint32_t websocket_ping_interval_ms = 30000;
    uint16_t websocket_port = 8081;
    bool enabled = true;
};

/**
 * Watchdog configuration
 */
struct WatchdogConfig {
    uint32_t hardware_timeout_ms = 30000;
    uint32_t task_health_check_ms = 60000;
};

/**
 * Persistence configuration
 */
struct PersistenceConfig {
    uint32_t state_snapshot_interval_ms = 60000;
    uint32_t nvs_commit_delay_ms = 100;
};

/**
 * MODBUS device register definition
 */
struct RegisterDef {
    char name[MAX_NAME_LENGTH];
    uint16_t reg;
    float scale = 1.0f;
    SensorPriority priority = SensorPriority::NORMAL;
    uint8_t extra_samples_per_second = 0;
    FilterType filter = FilterType::EMA;
    float filter_alpha = 0.3f;
    uint16_t min_raw = 0;           // Minimum valid raw value (4mA threshold)
    uint16_t max_raw = 65535;       // Maximum valid raw value (20mA threshold)
};

/**
 * MODBUS device definition
 */
struct ModbusDeviceDef {
    uint8_t address;
    char type[16];              // "pt1000_8ch", "analog_8ch", "relay_16ch"
    char name[MAX_NAME_LENGTH];
    RegisterDef registers[16];
    uint8_t register_count = 0;
};

/**
 * GPIO relay definition
 */
struct GPIORelayDef {
    char name[MAX_NAME_LENGTH];
    uint8_t pin;
    RelayType type;
};

/**
 * Hardware configuration
 */
struct HardwareConfig {
    // MODBUS
    char uart_port[16] = "/dev/ttyS1";
    uint32_t baudrate = 115200;
    ModbusDeviceDef modbus_devices[8];
    uint8_t modbus_device_count = 0;

    // GPIO relays
    GPIORelayDef gpio_relays[8];
    uint8_t gpio_relay_count = 0;
};

/**
 * WiFi configuration
 */
struct WiFiConfig {
    char ssid[32] = "";
    char password[64] = "";
    char hostname[32] = "fermenter";
};

/**
 * Fermenter definition
 */
struct FermenterDef {
    uint8_t id;
    char name[MAX_NAME_LENGTH];
    char temp_sensor[MAX_NAME_LENGTH];
    char pressure_sensor[MAX_NAME_LENGTH];
    char cooling_relay[MAX_NAME_LENGTH];
    char spunding_relay[MAX_NAME_LENGTH];
};

/**
 * Complete system configuration
 */
struct SystemConfig {
    // Timing
    ModbusTimingConfig modbus_timing;
    SchedulerConfig scheduler;
    PIDTimingConfig pid_timing;
    SensorTimingConfig sensor_timing;
    SafetyTimingConfig safety_timing;
    WatchdogConfig watchdog;
    PersistenceConfig persistence;

    // Communication
    WiFiConfig wifi;
    MQTTConfig mqtt;
    NTPConfig ntp;
    APIConfig api;
    DisplayConfig display;

    // Hardware
    HardwareConfig hardware;

    // Fermenters
    FermenterDef fermenters[MAX_FERMENTERS];
    uint8_t fermenter_count = 0;
};

} // namespace core
