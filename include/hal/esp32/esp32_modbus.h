#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cstring>

namespace hal {
namespace esp32 {

/**
 * ESP32 MODBUS RTU Master implementation
 * Uses UART for serial communication with MODBUS slave devices
 */
class ESP32Modbus : public IModbusInterface {
public:
    static constexpr const char* TAG = "ESP32Modbus";
    static constexpr size_t BUF_SIZE = 256;
    static constexpr uint32_t TIMEOUT_MS = 100;

    ESP32Modbus(uart_port_t port = UART_NUM_1, int tx_pin = 17, int rx_pin = 18, int de_pin = 21)
        : port_(port)
        , tx_pin_(tx_pin)
        , rx_pin_(rx_pin)
        , de_pin_(de_pin)
        , initialized_(false)
        , transaction_count_(0)
        , error_count_(0) {}

    /**
     * Initialize MODBUS communication
     * @param baud Baud rate (typically 9600, 19200, 57600, or 115200)
     */
    bool initialize(uint32_t baud = 57600) {
        uart_config_t uart_config = {
            .baud_rate = (int)baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
            .flags = {.allow_pd = 0, .backup_before_sleep = 0},
        };

        esp_err_t ret = uart_driver_install(port_, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
            return false;
        }

        ret = uart_param_config(port_, &uart_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
            uart_driver_delete(port_);
            return false;
        }

        ret = uart_set_pin(port_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
            uart_driver_delete(port_);
            return false;
        }

        // Configure DE/RE pin for RS485 if specified
        if (de_pin_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << de_pin_);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)de_pin_, 0);  // Start in receive mode
        }

        initialized_ = true;
        ESP_LOGI(TAG, "MODBUS initialized on UART%d at %lu baud", port_, (unsigned long)baud);
        return true;
    }

    bool read_holding_registers(uint8_t slave_addr, uint16_t start_reg,
                                uint16_t count, uint16_t* data) override {
        if (!initialized_) return false;
        if (!data) return false;
        if (count == 0 || count > 125) return false;  // Max 125 registers per request

        // Flush any stale data in RX buffer
        uart_flush_input(port_);

        // Build request: [slave][0x03][reg_hi][reg_lo][count_hi][count_lo][crc_lo][crc_hi]
        uint8_t request[8];
        request[0] = slave_addr;
        request[1] = 0x03;  // Read holding registers
        request[2] = (start_reg >> 8) & 0xFF;
        request[3] = start_reg & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;

        uint16_t crc = calculate_crc(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        // Send request
        set_transmit_mode(true);
        uart_write_bytes(port_, (const char*)request, 8);
        uart_wait_tx_done(port_, pdMS_TO_TICKS(100));
        set_transmit_mode(false);

        // Receive response
        uint8_t response[256];
        size_t expected_len = 3 + (count * 2) + 2;  // slave + func + byte_count + data + crc

        int len = uart_read_bytes(port_, response, expected_len, pdMS_TO_TICKS(TIMEOUT_MS));

        transaction_count_++;

        if (len < (int)expected_len) {
            error_count_++;
            ESP_LOGW(TAG, "Timeout reading from slave %d", slave_addr);
            return false;
        }

        // Verify response
        if (response[0] != slave_addr || response[1] != 0x03) {
            error_count_++;
            ESP_LOGW(TAG, "Invalid response from slave %d", slave_addr);
            return false;
        }

        // Verify CRC
        uint16_t response_crc = response[len - 2] | (response[len - 1] << 8);
        if (calculate_crc(response, len - 2) != response_crc) {
            error_count_++;
            ESP_LOGW(TAG, "CRC error from slave %d", slave_addr);
            return false;
        }

        // Extract data (big-endian)
        for (uint16_t i = 0; i < count; i++) {
            data[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
        }

        return true;
    }

    bool write_register(uint8_t slave_addr, uint16_t reg, uint16_t value) override {
        if (!initialized_) return false;

        // Flush any stale data in RX buffer
        uart_flush_input(port_);

        // Build request: [slave][0x06][reg_hi][reg_lo][val_hi][val_lo][crc_lo][crc_hi]
        uint8_t request[8];
        request[0] = slave_addr;
        request[1] = 0x06;  // Write single register
        request[2] = (reg >> 8) & 0xFF;
        request[3] = reg & 0xFF;
        request[4] = (value >> 8) & 0xFF;
        request[5] = value & 0xFF;

        uint16_t crc = calculate_crc(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        // Send request
        set_transmit_mode(true);
        uart_write_bytes(port_, (const char*)request, 8);
        uart_wait_tx_done(port_, pdMS_TO_TICKS(100));
        set_transmit_mode(false);

        // Receive response (echo)
        uint8_t response[8];
        int len = uart_read_bytes(port_, response, 8, pdMS_TO_TICKS(TIMEOUT_MS));

        transaction_count_++;

        if (len < 8) {
            error_count_++;
            return false;
        }

        // Verify echo
        if (memcmp(request, response, 6) != 0) {
            error_count_++;
            return false;
        }

        return true;
    }

    bool write_multiple_registers(uint8_t slave_addr, uint16_t start_reg,
                                  uint16_t count, const uint16_t* data) override {
        if (!initialized_) return false;
        if (!data) return false;
        if (count == 0 || count > 123) return false;  // Max 123 registers per request

        // Flush any stale data in RX buffer
        uart_flush_input(port_);

        // Build request
        uint8_t request[256];
        request[0] = slave_addr;
        request[1] = 0x10;  // Write multiple registers
        request[2] = (start_reg >> 8) & 0xFF;
        request[3] = start_reg & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;
        request[6] = count * 2;  // Byte count

        // Add data (big-endian)
        for (uint16_t i = 0; i < count; i++) {
            request[7 + i * 2] = (data[i] >> 8) & 0xFF;
            request[8 + i * 2] = data[i] & 0xFF;
        }

        size_t request_len = 7 + count * 2;
        uint16_t crc = calculate_crc(request, request_len);
        request[request_len] = crc & 0xFF;
        request[request_len + 1] = (crc >> 8) & 0xFF;
        request_len += 2;

        // Send request
        set_transmit_mode(true);
        uart_write_bytes(port_, (const char*)request, request_len);
        uart_wait_tx_done(port_, pdMS_TO_TICKS(100));
        set_transmit_mode(false);

        // Receive response
        uint8_t response[8];
        int len = uart_read_bytes(port_, response, 8, pdMS_TO_TICKS(TIMEOUT_MS));

        transaction_count_++;

        if (len < 8) {
            error_count_++;
            return false;
        }

        // Verify response
        if (response[0] != slave_addr || response[1] != 0x10) {
            error_count_++;
            return false;
        }

        return true;
    }

    uint32_t get_transaction_count() const override { return transaction_count_; }
    uint32_t get_error_count() const override { return error_count_; }

private:
    uart_port_t port_;
    int tx_pin_;
    int rx_pin_;
    int de_pin_;
    bool initialized_;
    uint32_t transaction_count_;
    uint32_t error_count_;

    void set_transmit_mode(bool transmit) {
        if (de_pin_ >= 0) {
            gpio_set_level((gpio_num_t)de_pin_, transmit ? 1 : 0);
        }
    }

    uint16_t calculate_crc(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
