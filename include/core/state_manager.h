#pragma once

#include "types.h"
#include "config.h"
#include "cpu_history.h"
#include "network_history.h"
#include <cstring>

#ifdef ESP32_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
#include <mutex>
#endif

namespace core {

/**
 * Centralized state manager
 * Thread-safe access to all system state
 */
class StateManager {
public:
    StateManager();
    ~StateManager();

    // Prevent copying
    StateManager(const StateManager&) = delete;
    StateManager& operator=(const StateManager&) = delete;

    // Initialization
    void initialize(const SystemConfig& config);

    // Sensor operations
    bool register_sensor(const char* name, const char* unit, float scale = 1.0f);
    SensorState* get_sensor(const char* name);
    SensorState* get_sensor_by_id(uint8_t id);
    uint8_t get_sensor_id(const char* name) const;
    void update_sensor_value(uint8_t sensor_id, float raw_value, uint32_t timestamp);
    void update_sensor_filtered(uint8_t sensor_id, float filtered, float display);
    void set_sensor_quality(uint8_t sensor_id, SensorQuality quality);

    // Relay operations
    bool register_relay(const char* name, RelayType type, uint8_t gpio_pin = 0,
                       uint8_t modbus_addr = 0, uint16_t modbus_reg = 0);
    RelayState* get_relay(const char* name);
    RelayState* get_relay_by_id(uint8_t id);
    uint8_t get_relay_id(const char* name) const;
    void set_relay_state(uint8_t relay_id, bool state, uint32_t timestamp);
    void set_relay_duty_cycle(uint8_t relay_id, float duty_cycle);

    // Fermenter operations
    bool register_fermenter(const FermenterDef& def);
    FermenterState* get_fermenter(uint8_t id);
    FermenterState* get_fermenter_by_name(const char* name);
    void update_fermenter_temps(uint8_t id, float current, float target);
    void update_fermenter_pressure(uint8_t id, float current, float target);
    void set_fermenter_mode(uint8_t id, FermenterMode mode);
    void update_fermenter_plan_progress(uint8_t id, uint8_t step, float hours_remaining);

    // System state
    SystemState& get_system_state();
    void update_system_uptime(uint32_t seconds);
    void update_system_ntp_status(bool synced, uint32_t boot_time = 0);
    void update_wifi_rssi(int rssi);
    void update_free_heap(uint32_t bytes);
    void update_modbus_stats(uint32_t transactions, uint32_t errors);
    void update_cpu_usage(float percent);
    void update_cpu_freq(uint32_t current_mhz, uint32_t max_mhz);

    // CPU history for graphing
    void add_cpu_history_sample(float percent);
    CpuHistory& get_cpu_history() { return cpu_history_; }
    const CpuHistory& get_cpu_history() const { return cpu_history_; }

    // Network history for graphing
    void sample_network_history(uint32_t link_speed_mbps, uint8_t channel);
    void add_network_tx_bytes(uint32_t bytes);
    void add_network_rx_bytes(uint32_t bytes);
    void reset_network_history() { network_history_.reset(); }
    NetworkHistory& get_network_history() { return network_history_; }
    const NetworkHistory& get_network_history() const { return network_history_; }

    // Bulk access (with lock held)
    uint8_t get_sensor_count() const { return sensor_count_; }
    uint8_t get_relay_count() const { return relay_count_; }
    uint8_t get_fermenter_count() const { return fermenter_count_; }

    // Lock for complex operations
    // Default timeout 1000ms - returns false if lock not acquired
    static constexpr uint32_t DEFAULT_LOCK_TIMEOUT_MS = 1000;
    bool lock(uint32_t timeout_ms = DEFAULT_LOCK_TIMEOUT_MS);
    void unlock();

    /**
     * RAII lock guard for StateManager
     * Automatically unlocks on destruction
     * Usage: auto guard = state.scoped_lock();
     *        if (!guard.acquired()) return; // handle lock failure
     */
    class ScopedLock {
    public:
        ScopedLock(StateManager& mgr, uint32_t timeout_ms = DEFAULT_LOCK_TIMEOUT_MS)
            : mgr_(mgr), acquired_(mgr.lock(timeout_ms)) {}
        ~ScopedLock() { if (acquired_) mgr_.unlock(); }
        bool acquired() const { return acquired_; }
        // Prevent copying
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
    private:
        StateManager& mgr_;
        bool acquired_;
    };

    ScopedLock scoped_lock(uint32_t timeout_ms = DEFAULT_LOCK_TIMEOUT_MS) {
        return ScopedLock(*this, timeout_ms);
    }

private:
    SensorState sensors_[MAX_SENSORS];
    RelayState relays_[MAX_RELAYS];
    FermenterState fermenters_[MAX_FERMENTERS];
    SystemState system_state_;
    CpuHistory cpu_history_;
    NetworkHistory network_history_;

    uint8_t sensor_count_;
    uint8_t relay_count_;
    uint8_t fermenter_count_;

#ifdef ESP32_BUILD
    SemaphoreHandle_t mutex_;
#else
    std::mutex mutex_;
#endif
};

} // namespace core
