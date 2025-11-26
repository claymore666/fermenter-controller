#include "core/config_loader.h"
#include <ArduinoJson.h>
#include <cstring>

namespace core {

void ConfigLoader::load_defaults(SystemConfig& config) {
    // Timing defaults
    config.modbus_timing.poll_interval_ms = 5000;
    config.modbus_timing.timeout_ms = 1000;
    config.modbus_timing.retry_delay_ms = 500;
    config.modbus_timing.max_retries = 3;

    config.scheduler.base_cycle_ms = 1000;
    config.scheduler.base_samples_per_cycle = 3;
    config.scheduler.min_sample_spacing_ms = 100;
    config.scheduler.transaction_time_ms = 5;
    config.scheduler.bulk_read_enabled = true;
    config.scheduler.fill_idle_windows = true;

    config.pid_timing.calculation_interval_ms = 5000;
    config.pid_timing.output_cycle_time_ms = 60000;
    config.pid_timing.autotune_max_duration_min = 120;

    config.sensor_timing.quality_bad_timeout_ms = 300000;
    config.sensor_timing.default_filter = FilterType::EMA;
    config.sensor_timing.window_size = 5;
    config.sensor_timing.ema_alpha = 0.3f;

    config.safety_timing.check_interval_ms = 1000;
    config.safety_timing.temp_deviation_timeout_ms = 900000;
    config.safety_timing.pressure_check_interval_ms = 2000;
    config.safety_timing.alarm_cooldown_ms = 60000;
    config.safety_timing.max_temp_deviation = 3.0f;
    config.safety_timing.max_pressure_bar = 2.5f;

    config.watchdog.hardware_timeout_ms = 30000;
    config.watchdog.task_health_check_ms = 60000;

    config.persistence.state_snapshot_interval_ms = 60000;
    config.persistence.nvs_commit_delay_ms = 100;

    // Communication defaults
    strncpy(config.wifi.hostname, "fermenter", sizeof(config.wifi.hostname));

    config.mqtt.port = 1883;
    strncpy(config.mqtt.topic_prefix, "brewery/fermentation", sizeof(config.mqtt.topic_prefix));
    config.mqtt.publish_interval_ms = 10000;
    config.mqtt.reconnect_delay_ms = 5000;
    config.mqtt.keepalive_s = 60;
    config.mqtt.enabled = false;

    strncpy(config.ntp.server, "pool.ntp.org", sizeof(config.ntp.server));
    strncpy(config.ntp.timezone, "UTC0", sizeof(config.ntp.timezone));
    config.ntp.sync_interval_h = 1;
    config.ntp.boot_sync_timeout_ms = 10000;
    config.ntp.enabled = true;

    config.api.port = 80;
    config.api.token_expire_min = 60;
    config.api.request_timeout_ms = 30000;
    config.api.enabled = true;

    config.display.update_interval_ms = 1000;
    config.display.websocket_ping_interval_ms = 30000;
    config.display.websocket_port = 8081;
    config.display.enabled = true;

    // Hardware defaults
    strncpy(config.hardware.uart_port, "/dev/ttyS1", sizeof(config.hardware.uart_port));
    config.hardware.baudrate = 115200;
    config.hardware.modbus_device_count = 0;
    config.hardware.gpio_relay_count = 0;

    config.fermenter_count = 0;
}

bool ConfigLoader::load_from_json(const char* json, SystemConfig& config) {
    // Start with defaults
    load_defaults(config);

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        return false;
    }

    // Timing configuration
    JsonObject timing = doc["timing"];
    if (timing) {
        JsonObject modbus = timing["modbus"];
        if (modbus) {
            config.modbus_timing.poll_interval_ms = modbus["poll_interval_ms"] | config.modbus_timing.poll_interval_ms;
            config.modbus_timing.timeout_ms = modbus["timeout_ms"] | config.modbus_timing.timeout_ms;
            config.modbus_timing.retry_delay_ms = modbus["retry_delay_ms"] | config.modbus_timing.retry_delay_ms;
            config.modbus_timing.max_retries = modbus["max_retries"] | config.modbus_timing.max_retries;
        }

        JsonObject scheduler = timing["scheduler"];
        if (scheduler) {
            config.scheduler.base_cycle_ms = scheduler["base_cycle_ms"] | config.scheduler.base_cycle_ms;
            config.scheduler.base_samples_per_cycle = scheduler["base_samples_per_cycle"] | config.scheduler.base_samples_per_cycle;
            config.scheduler.min_sample_spacing_ms = scheduler["min_sample_spacing_ms"] | config.scheduler.min_sample_spacing_ms;
            config.scheduler.bulk_read_enabled = scheduler["bulk_read_enabled"] | config.scheduler.bulk_read_enabled;
            config.scheduler.fill_idle_windows = scheduler["fill_idle_windows"] | config.scheduler.fill_idle_windows;
        }

        JsonObject pid = timing["pid"];
        if (pid) {
            config.pid_timing.calculation_interval_ms = pid["calculation_interval_ms"] | config.pid_timing.calculation_interval_ms;
            config.pid_timing.output_cycle_time_ms = pid["output_cycle_time_ms"] | config.pid_timing.output_cycle_time_ms;
            config.pid_timing.autotune_max_duration_min = pid["autotune_max_duration_min"] | config.pid_timing.autotune_max_duration_min;
        }

        JsonObject safety = timing["safety"];
        if (safety) {
            config.safety_timing.check_interval_ms = safety["check_interval_ms"] | config.safety_timing.check_interval_ms;
            config.safety_timing.temp_deviation_timeout_ms = safety["temp_deviation_timeout_ms"] | config.safety_timing.temp_deviation_timeout_ms;
            config.safety_timing.max_temp_deviation = safety["max_temp_deviation"] | config.safety_timing.max_temp_deviation;
            config.safety_timing.max_pressure_bar = safety["max_pressure_bar"] | config.safety_timing.max_pressure_bar;
        }
    }

    // WiFi configuration
    JsonObject wifi = doc["wifi"];
    if (wifi) {
        const char* ssid = wifi["ssid"];
        if (ssid) strncpy(config.wifi.ssid, ssid, sizeof(config.wifi.ssid) - 1);

        const char* password = wifi["password"];
        if (password) strncpy(config.wifi.password, password, sizeof(config.wifi.password) - 1);

        const char* hostname = wifi["hostname"];
        if (hostname) strncpy(config.wifi.hostname, hostname, sizeof(config.wifi.hostname) - 1);
    }

    // MQTT configuration
    JsonObject mqtt = doc["mqtt"];
    if (mqtt) {
        const char* broker = mqtt["broker"];
        if (broker) strncpy(config.mqtt.broker, broker, sizeof(config.mqtt.broker) - 1);

        config.mqtt.port = mqtt["port"] | config.mqtt.port;

        const char* username = mqtt["username"];
        if (username) strncpy(config.mqtt.username, username, sizeof(config.mqtt.username) - 1);

        const char* password = mqtt["password"];
        if (password) strncpy(config.mqtt.password, password, sizeof(config.mqtt.password) - 1);

        const char* topic_prefix = mqtt["topic_prefix"];
        if (topic_prefix) strncpy(config.mqtt.topic_prefix, topic_prefix, sizeof(config.mqtt.topic_prefix) - 1);

        config.mqtt.publish_interval_ms = mqtt["publish_interval_ms"] | config.mqtt.publish_interval_ms;
        config.mqtt.enabled = mqtt["enabled"] | config.mqtt.enabled;
    }

    // NTP configuration
    JsonObject ntp = doc["ntp"];
    if (ntp) {
        const char* server = ntp["server"];
        if (server) strncpy(config.ntp.server, server, sizeof(config.ntp.server) - 1);

        const char* timezone = ntp["timezone"];
        if (timezone) strncpy(config.ntp.timezone, timezone, sizeof(config.ntp.timezone) - 1);

        config.ntp.enabled = ntp["enabled"] | config.ntp.enabled;
    }

    // Hardware configuration - MODBUS devices
    JsonArray devices = doc["modbus"]["devices"];
    if (devices) {
        config.hardware.modbus_device_count = 0;

        for (JsonObject device : devices) {
            if (config.hardware.modbus_device_count >= 8) break;

            ModbusDeviceDef& def = config.hardware.modbus_devices[config.hardware.modbus_device_count];

            def.address = device["address"] | 1;

            const char* type = device["type"];
            if (type) strncpy(def.type, type, sizeof(def.type) - 1);

            const char* name = device["name"];
            if (name) strncpy(def.name, name, sizeof(def.name) - 1);

            // Parse registers
            JsonArray registers = device["registers"];
            if (registers) {
                def.register_count = 0;

                for (JsonObject reg : registers) {
                    if (def.register_count >= 16) break;

                    RegisterDef& r = def.registers[def.register_count];

                    const char* reg_name = reg["name"];
                    if (reg_name) strncpy(r.name, reg_name, sizeof(r.name) - 1);

                    r.reg = reg["reg"] | 0;
                    r.scale = reg["scale"] | 1.0f;

                    const char* priority = reg["priority"];
                    if (priority) {
                        if (strcmp(priority, "critical") == 0) r.priority = SensorPriority::CRITICAL;
                        else if (strcmp(priority, "high") == 0) r.priority = SensorPriority::HIGH;
                        else if (strcmp(priority, "low") == 0) r.priority = SensorPriority::LOW;
                        else r.priority = SensorPriority::NORMAL;
                    }

                    r.extra_samples_per_second = reg["extra_samples_per_second"] | 0;
                    r.filter_alpha = reg["filter_alpha"] | 0.3f;

                    // Parse filter type
                    const char* filter = reg["filter"];
                    if (filter) {
                        if (strcmp(filter, "ema") == 0) r.filter = FilterType::EMA;
                        else if (strcmp(filter, "moving_avg") == 0) r.filter = FilterType::MOVING_AVERAGE;
                        else if (strcmp(filter, "median") == 0) r.filter = FilterType::MEDIAN;
                        else if (strcmp(filter, "dual_rate") == 0) r.filter = FilterType::DUAL_RATE;
                        else if (strcmp(filter, "none") == 0) r.filter = FilterType::NONE;
                    }

                    // Parse 4-20mA range thresholds
                    r.min_raw = reg["min_raw"] | 0;
                    r.max_raw = reg["max_raw"] | 65535;

                    def.register_count++;
                }
            }

            config.hardware.modbus_device_count++;
        }
    }

    // GPIO relays
    JsonObject gpio = doc["gpio"]["esp32_relays"];
    if (gpio) {
        config.hardware.gpio_relay_count = 0;

        for (JsonPair kv : gpio) {
            if (config.hardware.gpio_relay_count >= 8) break;

            GPIORelayDef& def = config.hardware.gpio_relays[config.hardware.gpio_relay_count];

            strncpy(def.name, kv.value()["name"] | kv.key().c_str(), sizeof(def.name) - 1);
            def.pin = config.hardware.gpio_relay_count + 1;  // Default pin assignment

            const char* type = kv.value()["type"];
            if (type) {
                if (strcmp(type, "solenoid_nc") == 0) def.type = RelayType::SOLENOID_NC;
                else if (strcmp(type, "solenoid_no") == 0) def.type = RelayType::SOLENOID_NO;
                else if (strcmp(type, "contactor_coil") == 0) def.type = RelayType::CONTACTOR_COIL;
                else def.type = RelayType::SSR;
            }

            config.hardware.gpio_relay_count++;
        }
    }

    // Fermenters
    JsonArray fermenters = doc["fermenters"];
    if (fermenters) {
        config.fermenter_count = 0;

        for (JsonObject ferm : fermenters) {
            if (config.fermenter_count >= MAX_FERMENTERS) break;

            FermenterDef& def = config.fermenters[config.fermenter_count];

            def.id = ferm["id"] | (config.fermenter_count + 1);

            const char* name = ferm["name"];
            if (name) strncpy(def.name, name, sizeof(def.name) - 1);

            const char* temp_sensor = ferm["temp_sensor"];
            if (temp_sensor) strncpy(def.temp_sensor, temp_sensor, sizeof(def.temp_sensor) - 1);

            const char* pressure_sensor = ferm["pressure_sensor"];
            if (pressure_sensor) strncpy(def.pressure_sensor, pressure_sensor, sizeof(def.pressure_sensor) - 1);

            const char* cooling_relay = ferm["cooling_relay"];
            if (cooling_relay) strncpy(def.cooling_relay, cooling_relay, sizeof(def.cooling_relay) - 1);

            const char* spunding_relay = ferm["spunding_relay"];
            if (spunding_relay) strncpy(def.spunding_relay, spunding_relay, sizeof(def.spunding_relay) - 1);

            config.fermenter_count++;
        }
    }

    return true;
}

bool ConfigLoader::load_from_storage(hal::IStorageInterface* storage, SystemConfig& config) {
    if (!storage) return false;

    // Try to read JSON config from storage
    char buffer[4096];
    size_t len = sizeof(buffer);

    if (!storage->read_blob("config", buffer, &len)) {
        return false;
    }

    buffer[len] = '\0';  // Null terminate
    return load_from_json(buffer, config);
}

bool ConfigLoader::save_to_storage(hal::IStorageInterface* storage, const SystemConfig& config) {
    if (!storage) return false;

    char buffer[4096];
    size_t len = to_json(config, buffer, sizeof(buffer));

    if (len == 0) return false;

    return storage->write_blob("config", buffer, len) && storage->commit();
}

size_t ConfigLoader::to_json(const SystemConfig& config, char* buffer, size_t buffer_size) {
    JsonDocument doc;

    // Timing
    JsonObject timing = doc["timing"].to<JsonObject>();

    JsonObject modbus = timing["modbus"].to<JsonObject>();
    modbus["poll_interval_ms"] = config.modbus_timing.poll_interval_ms;
    modbus["timeout_ms"] = config.modbus_timing.timeout_ms;
    modbus["retry_delay_ms"] = config.modbus_timing.retry_delay_ms;
    modbus["max_retries"] = config.modbus_timing.max_retries;

    JsonObject scheduler = timing["scheduler"].to<JsonObject>();
    scheduler["base_cycle_ms"] = config.scheduler.base_cycle_ms;
    scheduler["base_samples_per_cycle"] = config.scheduler.base_samples_per_cycle;
    scheduler["min_sample_spacing_ms"] = config.scheduler.min_sample_spacing_ms;
    scheduler["bulk_read_enabled"] = config.scheduler.bulk_read_enabled;

    JsonObject pid = timing["pid"].to<JsonObject>();
    pid["calculation_interval_ms"] = config.pid_timing.calculation_interval_ms;
    pid["output_cycle_time_ms"] = config.pid_timing.output_cycle_time_ms;

    JsonObject safety = timing["safety"].to<JsonObject>();
    safety["check_interval_ms"] = config.safety_timing.check_interval_ms;
    safety["max_temp_deviation"] = config.safety_timing.max_temp_deviation;
    safety["max_pressure_bar"] = config.safety_timing.max_pressure_bar;

    // WiFi
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = config.wifi.ssid;
    wifi["hostname"] = config.wifi.hostname;

    // MQTT
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["broker"] = config.mqtt.broker;
    mqtt["port"] = config.mqtt.port;
    mqtt["topic_prefix"] = config.mqtt.topic_prefix;
    mqtt["enabled"] = config.mqtt.enabled;

    // NTP
    JsonObject ntp = doc["ntp"].to<JsonObject>();
    ntp["server"] = config.ntp.server;
    ntp["timezone"] = config.ntp.timezone;
    ntp["enabled"] = config.ntp.enabled;

    // Fermenters
    JsonArray fermenters = doc["fermenters"].to<JsonArray>();
    for (uint8_t i = 0; i < config.fermenter_count; i++) {
        JsonObject ferm = fermenters.add<JsonObject>();
        ferm["id"] = config.fermenters[i].id;
        ferm["name"] = config.fermenters[i].name;
        ferm["temp_sensor"] = config.fermenters[i].temp_sensor;
        ferm["pressure_sensor"] = config.fermenters[i].pressure_sensor;
        ferm["cooling_relay"] = config.fermenters[i].cooling_relay;
        ferm["spunding_relay"] = config.fermenters[i].spunding_relay;
    }

    return serializeJson(doc, buffer, buffer_size);
}

} // namespace core
