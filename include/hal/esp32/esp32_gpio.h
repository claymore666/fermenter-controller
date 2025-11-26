#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <cstring>

namespace hal {
namespace esp32 {

/**
 * ESP32 GPIO interface for the Waveshare ESP32-S3-POE-ETH-8DI-8DO board
 *
 * Digital Inputs: Direct GPIO (GPIO4-11 for DI1-DI8)
 * Digital Outputs: I2C via TCA9554PWR expander at address 0x20
 */
class ESP32GPIO : public IGPIOInterface {
public:
    static constexpr const char* TAG = "ESP32GPIO";

    // Hardware constants for Waveshare ESP32-S3-POE-ETH-8DI-8DO
    static constexpr size_t MAX_OUTPUTS = 8;
    static constexpr size_t MAX_INPUTS = 8;

    // I2C configuration for TCA9554PWR
    static constexpr uint8_t TCA9554_ADDR = 0x20;
    static constexpr uint8_t TCA9554_INPUT_REG = 0x00;
    static constexpr uint8_t TCA9554_OUTPUT_REG = 0x01;
    static constexpr uint8_t TCA9554_POLARITY_REG = 0x02;
    static constexpr uint8_t TCA9554_CONFIG_REG = 0x03;

    // I2C pins (shared with RTC)
    static constexpr int I2C_SDA_PIN = 42;
    static constexpr int I2C_SCL_PIN = 41;

    // Digital input GPIO pins (DI1-DI8)
    static constexpr uint8_t DI_PINS[MAX_INPUTS] = {4, 5, 6, 7, 8, 9, 10, 11};

    ESP32GPIO() : initialized_(false), output_state_(0), i2c_bus_(nullptr), tca9554_dev_(nullptr) {
        memset(relay_states_, 0, sizeof(relay_states_));
    }

    ~ESP32GPIO() {
        if (tca9554_dev_) {
            i2c_master_bus_rm_device(tca9554_dev_);
        }
        if (i2c_bus_) {
            i2c_del_master_bus(i2c_bus_);
        }
    }

    /**
     * Initialize I2C and configure TCA9554PWR for outputs
     */
    bool initialize() {
        // Configure I2C master bus
        i2c_master_bus_config_t bus_config = {};
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.scl_io_num = (gpio_num_t)I2C_SCL_PIN;
        bus_config.sda_io_num = (gpio_num_t)I2C_SDA_PIN;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            return false;
        }

        // Add TCA9554PWR device
        i2c_device_config_t dev_config = {};
        dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_config.device_address = TCA9554_ADDR;
        dev_config.scl_speed_hz = 100000;  // 100kHz

        ret = i2c_master_bus_add_device(i2c_bus_, &dev_config, &tca9554_dev_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add TCA9554 device: %s", esp_err_to_name(ret));
            i2c_del_master_bus(i2c_bus_);
            i2c_bus_ = nullptr;
            return false;
        }

        // Configure TCA9554PWR: all pins as outputs (0 = output)
        if (!write_tca9554_register(TCA9554_CONFIG_REG, 0x00)) {
            ESP_LOGE(TAG, "Failed to configure TCA9554PWR");
            return false;
        }

        // Initialize all outputs to OFF
        if (!write_tca9554_register(TCA9554_OUTPUT_REG, 0x00)) {
            ESP_LOGE(TAG, "Failed to initialize TCA9554PWR outputs");
            return false;
        }

        // Configure digital input pins
        for (size_t i = 0; i < MAX_INPUTS; i++) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pin_bit_mask = (1ULL << DI_PINS[i]);
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // Pull LOW when disconnected
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

            ret = gpio_config(&io_conf);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to configure DI%d on GPIO%d", i + 1, DI_PINS[i]);
            }
        }

        initialized_ = true;
        ESP_LOGI(TAG, "GPIO initialized: %d inputs (GPIO4-11), %d outputs (I2C TCA9554)",
                 MAX_INPUTS, MAX_OUTPUTS);
        return true;
    }

    /**
     * Set digital output state (DO1-DO8)
     * @param relay_id Output identifier (0-7 for DO1-DO8)
     * @param state true = ON, false = OFF
     */
    void set_relay(uint8_t relay_id, bool state) override {
        if (!initialized_ || relay_id >= MAX_OUTPUTS) return;

        relay_states_[relay_id] = state;

        // Update output state bitmap
        if (state) {
            output_state_ |= (1 << relay_id);
        } else {
            output_state_ &= ~(1 << relay_id);
        }

        // Write to TCA9554PWR
        if (!write_tca9554_register(TCA9554_OUTPUT_REG, output_state_)) {
            ESP_LOGW(TAG, "Failed to set DO%d", relay_id + 1);
        }
    }

    bool get_relay_state(uint8_t relay_id) const override {
        if (relay_id >= MAX_OUTPUTS) return false;
        return relay_states_[relay_id];
    }

    /**
     * Check if I2C communication with TCA9554 is working
     */
    bool check_i2c() const {
        if (!initialized_ || !tca9554_dev_) return false;

        // Try to read the config register
        uint8_t reg = TCA9554_CONFIG_REG;
        uint8_t data;
        esp_err_t ret = i2c_master_transmit_receive(tca9554_dev_, &reg, 1, &data, 1, 100);

        return (ret == ESP_OK);
    }

    /**
     * Read digital input (DI1-DI8)
     * @param input_id Input identifier (0-7 for DI1-DI8)
     * @return Pin state (true = HIGH/active, false = LOW/inactive)
     * Note: All inputs use optocoupler isolation. Internal pull-down ensures
     *       LOW when disconnected. HIGH indicates active input signal.
     */
    bool get_digital_input(uint8_t input_id) const override {
        if (input_id >= MAX_INPUTS) return false;
        return gpio_get_level((gpio_num_t)DI_PINS[input_id]) == 1;
    }

    /**
     * Set all outputs at once
     * @param value Bitmap (bit 0 = DO1, bit 7 = DO8)
     */
    void set_all_outputs(uint8_t value) {
        if (!initialized_) return;

        output_state_ = value;

        // Update individual states
        for (size_t i = 0; i < MAX_OUTPUTS; i++) {
            relay_states_[i] = (value >> i) & 1;
        }

        write_tca9554_register(TCA9554_OUTPUT_REG, output_state_);
    }

    /**
     * Get all outputs as bitmap
     */
    uint8_t get_all_outputs() const {
        return output_state_;
    }

    /**
     * Get all digital inputs as bitmap
     * @return Bitmap (bit 0 = DI1, bit 7 = DI8)
     */
    uint8_t get_all_inputs() const {
        uint8_t result = 0;
        for (size_t i = 0; i < MAX_INPUTS; i++) {
            if (get_digital_input(i)) {
                result |= (1 << i);
            }
        }
        return result;
    }

private:
    bool initialized_;
    uint8_t output_state_;
    bool relay_states_[MAX_OUTPUTS];
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t tca9554_dev_;

    bool write_tca9554_register(uint8_t reg, uint8_t value) {
        if (!tca9554_dev_) return false;

        uint8_t data[2] = {reg, value};
        esp_err_t ret = i2c_master_transmit(tca9554_dev_, data, 2, 100);

        return ret == ESP_OK;
    }
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
