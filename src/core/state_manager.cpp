#include "core/state_manager.h"

namespace core {

StateManager::StateManager()
    : sensor_count_(0)
    , relay_count_(0)
    , fermenter_count_(0) {
#ifdef ESP32_BUILD
    mutex_ = xSemaphoreCreateMutex();
#endif
}

StateManager::~StateManager() {
#ifdef ESP32_BUILD
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
#endif
}

bool StateManager::lock(uint32_t timeout_ms) {
#ifdef ESP32_BUILD
    // Use timeout instead of blocking forever to prevent deadlocks
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xSemaphoreTake(mutex_, ticks);
    if (result != pdTRUE) {
        // Log warning for timeout - indicates potential deadlock
        // In production, this should trigger an alert
        return false;
    }
    return true;
#else
    // Simulator: std::mutex doesn't have timeout, but we use try_lock for tests
    if (timeout_ms == 0) {
        return mutex_.try_lock();
    }
    mutex_.lock();
    return true;
#endif
}

void StateManager::unlock() {
#ifdef ESP32_BUILD
    xSemaphoreGive(mutex_);
#else
    mutex_.unlock();
#endif
}

void StateManager::initialize(const SystemConfig& config) {
    lock();

    // Reset counts
    sensor_count_ = 0;
    relay_count_ = 0;
    fermenter_count_ = 0;

    // Register sensors from MODBUS devices
    for (uint8_t d = 0; d < config.hardware.modbus_device_count; d++) {
        const auto& device = config.hardware.modbus_devices[d];

        for (uint8_t r = 0; r < device.register_count; r++) {
            const auto& reg = device.registers[r];

            // Determine unit based on sensor name
            const char* unit = "?";
            if (strstr(reg.name, "temp") != nullptr) {
                unit = "°C";
            } else if (strstr(reg.name, "pressure") != nullptr) {
                unit = "bar";
            }

            register_sensor(reg.name, unit, reg.scale);

            // Set filter config
            if (sensor_count_ > 0) {
                auto& sensor = sensors_[sensor_count_ - 1];
                sensor.filter_type = reg.filter;
                sensor.alpha = reg.filter_alpha;
            }
        }
    }

    // Register GPIO relays
    for (uint8_t i = 0; i < config.hardware.gpio_relay_count; i++) {
        const auto& relay = config.hardware.gpio_relays[i];
        register_relay(relay.name, relay.type, relay.pin);
    }

    // Register fermenters
    for (uint8_t i = 0; i < config.fermenter_count; i++) {
        register_fermenter(config.fermenters[i]);
    }

    unlock();
}

// Sensor operations

bool StateManager::register_sensor(const char* name, const char* unit, float scale) {
    if (sensor_count_ >= MAX_SENSORS) {
        return false;
    }

    auto& sensor = sensors_[sensor_count_];
    strncpy(sensor.name, name, MAX_NAME_LENGTH - 1);
    strncpy(sensor.unit, unit, sizeof(sensor.unit) - 1);
    sensor.scale = scale;
    sensor.quality = SensorQuality::UNKNOWN;
    sensor.filter_type = FilterType::EMA;
    sensor.alpha = 0.3f;

    sensor_count_++;
    return true;
}

SensorState* StateManager::get_sensor(const char* name) {
    for (uint8_t i = 0; i < sensor_count_; i++) {
        if (strcmp(sensors_[i].name, name) == 0) {
            return &sensors_[i];
        }
    }
    return nullptr;
}

SensorState* StateManager::get_sensor_by_id(uint8_t id) {
    if (id < sensor_count_) {
        return &sensors_[id];
    }
    return nullptr;
}

uint8_t StateManager::get_sensor_id(const char* name) const {
    for (uint8_t i = 0; i < sensor_count_; i++) {
        if (strcmp(sensors_[i].name, name) == 0) {
            return i;
        }
    }
    return 0xFF; // Not found
}

void StateManager::update_sensor_value(uint8_t sensor_id, float raw_value, uint32_t timestamp) {
    if (sensor_id >= sensor_count_) return;

    lock();
    auto& sensor = sensors_[sensor_id];
    sensor.raw_value = raw_value;
    sensor.timestamp = timestamp;

    // Add to base samples ring buffer
    sensor.base_samples[sensor.base_index] = raw_value;
    sensor.base_index = (sensor.base_index + 1) % 8;

    // Calculate base average
    float sum = 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < 8 && count < 3; i++) {
        if (sensor.base_samples[i] != 0) {
            sum += sensor.base_samples[i];
            count++;
        }
    }
    if (count > 0) {
        sensor.base_average = sum / count;
    }

    unlock();
}

void StateManager::update_sensor_filtered(uint8_t sensor_id, float filtered, float display) {
    if (sensor_id >= sensor_count_) return;

    lock();
    sensors_[sensor_id].filtered_value = filtered;
    sensors_[sensor_id].display_value = display;
    unlock();
}

void StateManager::set_sensor_quality(uint8_t sensor_id, SensorQuality quality) {
    if (sensor_id >= sensor_count_) return;

    lock();
    sensors_[sensor_id].quality = quality;
    unlock();
}

// Relay operations

bool StateManager::register_relay(const char* name, RelayType type, uint8_t gpio_pin,
                                  uint8_t modbus_addr, uint16_t modbus_reg) {
    if (relay_count_ >= MAX_RELAYS) {
        return false;
    }

    auto& relay = relays_[relay_count_];
    strncpy(relay.name, name, MAX_NAME_LENGTH - 1);
    relay.type = type;
    relay.gpio_pin = gpio_pin;
    relay.modbus_addr = modbus_addr;
    relay.modbus_reg = modbus_reg;
    relay.state = false;
    relay.duty_cycle = 0;

    relay_count_++;
    return true;
}

RelayState* StateManager::get_relay(const char* name) {
    for (uint8_t i = 0; i < relay_count_; i++) {
        if (strcmp(relays_[i].name, name) == 0) {
            return &relays_[i];
        }
    }
    return nullptr;
}

RelayState* StateManager::get_relay_by_id(uint8_t id) {
    if (id < relay_count_) {
        return &relays_[id];
    }
    return nullptr;
}

uint8_t StateManager::get_relay_id(const char* name) const {
    for (uint8_t i = 0; i < relay_count_; i++) {
        if (strcmp(relays_[i].name, name) == 0) {
            return i;
        }
    }
    return 0xFF;
}

void StateManager::set_relay_state(uint8_t relay_id, bool state, uint32_t timestamp) {
    if (relay_id >= relay_count_) return;

    lock();
    relays_[relay_id].state = state;
    relays_[relay_id].last_change = timestamp;
    unlock();
}

void StateManager::set_relay_duty_cycle(uint8_t relay_id, float duty_cycle) {
    if (relay_id >= relay_count_) return;

    lock();
    relays_[relay_id].duty_cycle = duty_cycle;
    unlock();
}

// Fermenter operations

bool StateManager::register_fermenter(const FermenterDef& def) {
    if (fermenter_count_ >= MAX_FERMENTERS) {
        return false;
    }

    auto& ferm = fermenters_[fermenter_count_];
    ferm.id = def.id;
    strncpy(ferm.name, def.name, MAX_NAME_LENGTH - 1);
    ferm.mode = FermenterMode::OFF;

    // Map sensors and relays by name
    ferm.temp_sensor_id = get_sensor_id(def.temp_sensor);
    ferm.pressure_sensor_id = get_sensor_id(def.pressure_sensor);
    ferm.cooling_relay_id = get_relay_id(def.cooling_relay);
    ferm.spunding_relay_id = get_relay_id(def.spunding_relay);

    fermenter_count_++;
    return true;
}

FermenterState* StateManager::get_fermenter(uint8_t id) {
    for (uint8_t i = 0; i < fermenter_count_; i++) {
        if (fermenters_[i].id == id) {
            return &fermenters_[i];
        }
    }
    return nullptr;
}

FermenterState* StateManager::get_fermenter_by_name(const char* name) {
    for (uint8_t i = 0; i < fermenter_count_; i++) {
        if (strcmp(fermenters_[i].name, name) == 0) {
            return &fermenters_[i];
        }
    }
    return nullptr;
}

void StateManager::update_fermenter_temps(uint8_t id, float current, float target) {
    auto* ferm = get_fermenter(id);
    if (!ferm) return;

    lock();
    ferm->current_temp = current;
    ferm->target_temp = target;
    unlock();
}

void StateManager::update_fermenter_pressure(uint8_t id, float current, float target) {
    auto* ferm = get_fermenter(id);
    if (!ferm) return;

    lock();
    ferm->current_pressure = current;
    ferm->target_pressure = target;
    unlock();
}

void StateManager::set_fermenter_mode(uint8_t id, FermenterMode mode) {
    auto* ferm = get_fermenter(id);
    if (!ferm) return;

    lock();
    ferm->mode = mode;
    unlock();
}

void StateManager::update_fermenter_plan_progress(uint8_t id, uint8_t step, float hours_remaining) {
    auto* ferm = get_fermenter(id);
    if (!ferm) return;

    lock();
    ferm->current_step = step;
    ferm->hours_remaining = hours_remaining;
    unlock();
}

// System state

SystemState& StateManager::get_system_state() {
    return system_state_;
}

void StateManager::update_system_uptime(uint32_t seconds) {
    lock();
    system_state_.uptime_seconds = seconds;
    unlock();
}

void StateManager::update_system_ntp_status(bool synced, uint32_t boot_time) {
    lock();
    system_state_.ntp_synced = synced;
    if (boot_time > 0) {
        system_state_.last_boot = boot_time;
    }
    unlock();
}

void StateManager::update_wifi_rssi(int rssi) {
    lock();
    system_state_.wifi_rssi = rssi;
    unlock();
}

void StateManager::update_free_heap(uint32_t bytes) {
    lock();
    system_state_.free_heap = bytes;
    unlock();
}

void StateManager::update_modbus_stats(uint32_t transactions, uint32_t errors) {
    lock();
    system_state_.modbus_transactions = transactions;
    system_state_.modbus_errors = errors;
    unlock();
}

void StateManager::update_cpu_usage(float percent) {
    lock();
    system_state_.cpu_usage = percent;
    unlock();
}

void StateManager::update_cpu_freq(uint32_t current_mhz, uint32_t max_mhz) {
    lock();
    system_state_.cpu_freq_mhz = current_mhz;
    system_state_.cpu_freq_max_mhz = max_mhz;
    unlock();
}

void StateManager::add_cpu_history_sample(float percent) {
    lock();
    cpu_history_.add_sample(percent);
    unlock();
}

void StateManager::sample_network_history(uint32_t link_speed_mbps, uint8_t channel) {
    lock();
    network_history_.sample(link_speed_mbps, channel);
    unlock();
}

void StateManager::add_network_tx_bytes(uint32_t bytes) {
    lock();
    network_history_.add_tx_bytes(bytes);
    unlock();
}

void StateManager::add_network_rx_bytes(uint32_t bytes) {
    lock();
    network_history_.add_rx_bytes(bytes);
    unlock();
}

} // namespace core
