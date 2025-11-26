#pragma once

#include "hal/interfaces.h"
#include <cstdint>
#include <cstring>

namespace modules {

/**
 * WiFi Connection Manager
 * Handles WiFi connection, reconnection, and status monitoring
 */
class WifiModule {
public:
    struct Config {
        char ssid[33];
        char password[65];
        char hostname[32];
        uint32_t reconnect_interval_ms;
        uint32_t connection_timeout_ms;
        bool auto_reconnect;
    };

    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };

    WifiModule(hal::INetworkInterface* network_hal, hal::ITimeInterface* time_hal)
        : network_hal_(network_hal)
        , time_hal_(time_hal)
        , state_(State::DISCONNECTED)
        , last_connect_attempt_(0)
        , connect_count_(0)
        , disconnect_count_(0) {
        // Default config
        memset(&config_, 0, sizeof(config_));
        config_.reconnect_interval_ms = 30000;  // 30 seconds
        config_.connection_timeout_ms = 10000;  // 10 seconds
        config_.auto_reconnect = true;
        strncpy(config_.hostname, "fermenter", sizeof(config_.hostname) - 1);
    }

    /**
     * Configure WiFi credentials and settings
     */
    void configure(const char* ssid, const char* password, const char* hostname = nullptr) {
        strncpy(config_.ssid, ssid, sizeof(config_.ssid) - 1);
        strncpy(config_.password, password, sizeof(config_.password) - 1);
        if (hostname) {
            strncpy(config_.hostname, hostname, sizeof(config_.hostname) - 1);
        }
    }

    /**
     * Configure full settings
     */
    void configure(const Config& cfg) {
        config_ = cfg;
    }

    /**
     * Connect to WiFi network
     * @return true if connection successful
     */
    bool connect() {
        if (strlen(config_.ssid) == 0) {
            return false;
        }

        state_ = State::CONNECTING;
        last_connect_attempt_ = time_hal_->millis();

        bool success = network_hal_->connect(config_.ssid, config_.password);

        if (success && network_hal_->is_connected()) {
            state_ = State::CONNECTED;
            connect_count_++;
            return true;
        } else {
            state_ = State::FAILED;
            return false;
        }
    }

    /**
     * Disconnect from WiFi
     */
    void disconnect() {
        network_hal_->disconnect();
        state_ = State::DISCONNECTED;
        disconnect_count_++;
    }

    /**
     * Update connection state (call periodically)
     * Handles auto-reconnection if enabled
     */
    void update() {
        bool connected = network_hal_->is_connected();

        if (connected) {
            if (state_ != State::CONNECTED) {
                state_ = State::CONNECTED;
            }
        } else {
            if (state_ == State::CONNECTED) {
                state_ = State::DISCONNECTED;
                disconnect_count_++;
            }

            // Auto-reconnect if configured
            if (config_.auto_reconnect && state_ == State::DISCONNECTED) {
                uint32_t elapsed = time_hal_->millis() - last_connect_attempt_;
                if (elapsed >= config_.reconnect_interval_ms) {
                    connect();
                }
            }
        }
    }

    /**
     * Check if connected
     */
    bool is_connected() const {
        return network_hal_->is_connected();
    }

    /**
     * Get current state
     */
    State get_state() const {
        return state_;
    }

    /**
     * Get state as string
     */
    const char* get_state_string() const {
        switch (state_) {
            case State::DISCONNECTED: return "DISCONNECTED";
            case State::CONNECTING:   return "CONNECTING";
            case State::CONNECTED:    return "CONNECTED";
            case State::FAILED:       return "FAILED";
            default:                  return "UNKNOWN";
        }
    }

    /**
     * Get IP address
     */
    const char* get_ip_address() const {
        return network_hal_->get_ip_address();
    }

    /**
     * Get WiFi signal strength (RSSI)
     */
    int get_rssi() const {
        return network_hal_->get_rssi();
    }

    /**
     * Get signal quality as percentage (0-100)
     */
    int get_signal_quality() const {
        int rssi = get_rssi();
        if (rssi <= -100) return 0;
        if (rssi >= -50) return 100;
        return 2 * (rssi + 100);
    }

    /**
     * Get connection statistics
     */
    uint32_t get_connect_count() const { return connect_count_; }
    uint32_t get_disconnect_count() const { return disconnect_count_; }

    /**
     * Get uptime since last connection in milliseconds
     */
    uint32_t get_uptime_ms() const {
        if (state_ != State::CONNECTED) return 0;
        return time_hal_->millis() - last_connect_attempt_;
    }

    /**
     * Get current configuration
     */
    const Config& get_config() const { return config_; }

    /**
     * Set auto-reconnect behavior
     */
    void set_auto_reconnect(bool enabled) {
        config_.auto_reconnect = enabled;
    }

    /**
     * Set reconnect interval
     */
    void set_reconnect_interval(uint32_t interval_ms) {
        config_.reconnect_interval_ms = interval_ms;
    }

private:
    hal::INetworkInterface* network_hal_;
    hal::ITimeInterface* time_hal_;
    Config config_;
    State state_;
    uint32_t last_connect_attempt_;
    uint32_t connect_count_;
    uint32_t disconnect_count_;
};

} // namespace modules
