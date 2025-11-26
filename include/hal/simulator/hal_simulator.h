#pragma once

#include "hal/interfaces.h"
#include "hal/serial_interface.h"
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <queue>

namespace hal {
namespace simulator {

/**
 * Simulated MODBUS interface for testing
 * Returns configurable sensor values
 */
class SimulatorModbus : public IModbusInterface {
public:
    SimulatorModbus() : transaction_count_(0), error_count_(0), inject_error_(false) {}

    bool read_holding_registers(uint8_t slave_addr, uint16_t start_reg,
                                uint16_t count, uint16_t* data) override {
        transaction_count_++;

        if (inject_error_) {
            error_count_++;
            return false;
        }

        // Generate simulated values
        for (uint16_t i = 0; i < count; i++) {
            uint32_t key = (slave_addr << 16) | (start_reg + i);

            auto it = register_values_.find(key);
            if (it != register_values_.end()) {
                data[i] = it->second;
            } else {
                // Default: simulate temperature around 18°C (180 with 0.1 scale)
                data[i] = 180 + (i * 10);
            }
        }

        return true;
    }

    bool write_register(uint8_t slave_addr, uint16_t reg, uint16_t value) override {
        transaction_count_++;

        if (inject_error_) {
            error_count_++;
            return false;
        }

        uint32_t key = (slave_addr << 16) | reg;
        register_values_[key] = value;
        return true;
    }

    bool write_multiple_registers(uint8_t slave_addr, uint16_t start_reg,
                                  uint16_t count, const uint16_t* data) override {
        transaction_count_++;

        if (inject_error_) {
            error_count_++;
            return false;
        }

        for (uint16_t i = 0; i < count; i++) {
            uint32_t key = (slave_addr << 16) | (start_reg + i);
            register_values_[key] = data[i];
        }

        return true;
    }

    uint32_t get_transaction_count() const override { return transaction_count_; }
    uint32_t get_error_count() const override { return error_count_; }

    // Test helpers
    void set_register(uint8_t slave_addr, uint16_t reg, uint16_t value) {
        uint32_t key = (slave_addr << 16) | reg;
        register_values_[key] = value;
    }

    void set_inject_error(bool inject) { inject_error_ = inject; }
    void reset_counters() { transaction_count_ = 0; error_count_ = 0; }

private:
    std::map<uint32_t, uint16_t> register_values_;
    uint32_t transaction_count_;
    uint32_t error_count_;
    bool inject_error_;
};

/**
 * Simulated GPIO interface for testing
 */
class SimulatorGPIO : public IGPIOInterface {
public:
    // Simulator initialize is a no-op, always succeeds
    bool initialize() { return true; }

    void set_relay(uint8_t relay_id, bool state) override {
        relay_states_[relay_id] = state;
    }

    bool get_relay_state(uint8_t relay_id) const override {
        auto it = relay_states_.find(relay_id);
        if (it != relay_states_.end()) {
            return it->second;
        }
        return false;
    }

    bool get_digital_input(uint8_t pin) const override {
        auto it = input_states_.find(pin);
        if (it != input_states_.end()) {
            return it->second;
        }
        return false;
    }

    // Test helpers
    void set_input(uint8_t pin, bool state) {
        input_states_[pin] = state;
    }

    void reset() {
        relay_states_.clear();
        input_states_.clear();
    }

private:
    std::map<uint8_t, bool> relay_states_;
    std::map<uint8_t, bool> input_states_;
};

/**
 * Simulated storage interface for testing
 * Stores data in memory
 */
class SimulatorStorage : public IStorageInterface {
public:
    bool write_blob(const char* key, const void* data, size_t len) override {
        std::string k(key);
        std::vector<uint8_t> v((uint8_t*)data, (uint8_t*)data + len);
        blobs_[k] = v;
        return true;
    }

    bool read_blob(const char* key, void* data, size_t* len) override {
        std::string k(key);
        auto it = blobs_.find(k);
        if (it == blobs_.end()) {
            return false;
        }

        if (*len < it->second.size()) {
            return false;
        }

        memcpy(data, it->second.data(), it->second.size());
        *len = it->second.size();
        return true;
    }

    bool write_string(const char* key, const char* value) override {
        strings_[key] = value;
        return true;
    }

    bool read_string(const char* key, char* value, size_t max_len) override {
        auto it = strings_.find(key);
        if (it == strings_.end()) {
            return false;
        }

        strncpy(value, it->second.c_str(), max_len - 1);
        value[max_len - 1] = '\0';
        return true;
    }

    bool write_int(const char* key, int32_t value) override {
        ints_[key] = value;
        return true;
    }

    bool read_int(const char* key, int32_t* value) override {
        auto it = ints_.find(key);
        if (it == ints_.end()) {
            return false;
        }

        *value = it->second;
        return true;
    }

    bool erase_key(const char* key) override {
        blobs_.erase(key);
        strings_.erase(key);
        ints_.erase(key);
        return true;
    }

    bool commit() override {
        return true;  // No-op for simulator
    }

    // Test helpers
    void reset() {
        blobs_.clear();
        strings_.clear();
        ints_.clear();
    }

    bool has_key(const char* key) const {
        return blobs_.count(key) || strings_.count(key) || ints_.count(key);
    }

private:
    std::map<std::string, std::vector<uint8_t>> blobs_;
    std::map<std::string, std::string> strings_;
    std::map<std::string, int32_t> ints_;
};

/**
 * Simulated network interface for testing
 */
class SimulatorNetwork : public INetworkInterface {
public:
    SimulatorNetwork() : connected_(false), rssi_(-50) {}

    bool connect(const char* ssid, const char* password) override {
        (void)password;
        ssid_ = ssid;
        connected_ = true;
        ip_address_ = "192.168.1.100";
        return true;
    }

    bool is_connected() const override { return connected_; }

    bool disconnect() override {
        connected_ = false;
        return true;
    }

    const char* get_ip_address() const override {
        if (!connected_) return nullptr;
        return ip_address_.c_str();
    }

    int get_rssi() const override { return rssi_; }

    // Test helpers
    void set_connected(bool connected) { connected_ = connected; }
    void set_rssi(int rssi) { rssi_ = rssi; }
    void set_ip(const char* ip) { ip_address_ = ip; }

private:
    bool connected_;
    std::string ssid_;
    std::string ip_address_;
    int rssi_;
};

/**
 * Simulated time interface for testing
 * Allows manual time control for deterministic tests
 */
class SimulatorTime : public ITimeInterface {
public:
    SimulatorTime() : millis_(0), unix_time_(1700000000) {}

    uint32_t millis() const override { return millis_; }

    uint32_t get_unix_time() const override { return unix_time_; }

    void delay_ms(uint32_t ms) override {
        millis_ += ms;
    }

    // Test helpers
    void set_millis(uint32_t ms) { millis_ = ms; }
    void advance_millis(uint32_t ms) { millis_ += ms; }
    void set_unix_time(uint32_t t) { unix_time_ = t; }
    void advance_unix_time(uint32_t seconds) { unix_time_ += seconds; }

private:
    uint32_t millis_;
    uint32_t unix_time_;
};

/**
 * Simulated serial interface for testing debug console
 * Uses internal buffers for input/output
 */
class SimulatorSerial : public ISerialInterface {
public:
    SimulatorSerial() : initialized_(false) {}

    bool begin(uint32_t baud) override {
        (void)baud;
        initialized_ = true;
        return true;
    }

    int available() override {
        return input_buffer_.size();
    }

    int read() override {
        if (input_buffer_.empty()) return -1;
        char c = input_buffer_.front();
        input_buffer_.pop();
        return c;
    }

    size_t read(uint8_t* buffer, size_t size) override {
        size_t count = 0;
        while (count < size && !input_buffer_.empty()) {
            buffer[count++] = input_buffer_.front();
            input_buffer_.pop();
        }
        return count;
    }

    size_t write(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            output_buffer_.push_back(data[i]);
        }
        return len;
    }

    size_t print(const char* str) override {
        size_t len = strlen(str);
        return write((const uint8_t*)str, len);
    }

    size_t println(const char* str) override {
        size_t len = print(str);
        len += write((const uint8_t*)"\r\n", 2);
        return len;
    }

    void flush() override {
        // Nothing to flush in simulator
    }

    // Test helpers
    void inject_input(const char* str) {
        while (*str) {
            input_buffer_.push(*str++);
        }
    }

    std::string get_output() {
        std::string result(output_buffer_.begin(), output_buffer_.end());
        output_buffer_.clear();
        return result;
    }

    void clear_output() {
        output_buffer_.clear();
    }

private:
    bool initialized_;
    std::queue<char> input_buffer_;
    std::vector<char> output_buffer_;
};

} // namespace simulator
} // namespace hal
