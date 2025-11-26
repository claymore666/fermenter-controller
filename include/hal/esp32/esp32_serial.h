#pragma once

#ifdef ESP32_BUILD

#include "hal/serial_interface.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include <cstring>

namespace hal {
namespace esp32 {

/**
 * ESP32 Serial interface using USB Serial/JTAG
 * Uses the built-in USB interface on ESP32-S3
 */
class ESP32Serial : public ISerialInterface {
public:
    static constexpr const char* TAG = "ESP32Serial";
    static constexpr size_t RX_BUF_SIZE = 512;
    static constexpr size_t TX_BUF_SIZE = 512;

    ESP32Serial() : initialized_(false) {}

    bool begin(uint32_t baud) override {
        (void)baud; // USB Serial/JTAG doesn't use baud rate

        // Check if driver is already installed (by ESP-IDF console)
        if (usb_serial_jtag_is_driver_installed()) {
            initialized_ = true;
            ESP_LOGI(TAG, "USB Serial/JTAG already initialized");
            return true;
        }

        // Configure USB Serial/JTAG
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = TX_BUF_SIZE,
            .rx_buffer_size = RX_BUF_SIZE,
        };

        esp_err_t ret = usb_serial_jtag_driver_install(&config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install USB Serial/JTAG driver: %s", esp_err_to_name(ret));
            return false;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "USB Serial/JTAG initialized");
        return true;
    }

    int available() override {
        if (!initialized_) return 0;

        // Try non-blocking read to check if data is available
        // ESP-IDF 5.x doesn't have a direct length query for USB Serial/JTAG
        return 1;  // Always return 1, read() will return -1 if no data
    }

    int read() override {
        if (!initialized_) return -1;

        uint8_t c;
        int len = usb_serial_jtag_read_bytes(&c, 1, 0);
        if (len > 0) {
            return c;
        }
        return -1;
    }

    size_t read(uint8_t* buffer, size_t size) override {
        if (!initialized_) return 0;

        int len = usb_serial_jtag_read_bytes(buffer, size, 0);
        return len > 0 ? len : 0;
    }

    size_t write(const uint8_t* data, size_t len) override {
        if (!initialized_ || !data || len == 0) return 0;

        int written = usb_serial_jtag_write_bytes((const char*)data, len, pdMS_TO_TICKS(100));
        return written > 0 ? written : 0;
    }

    size_t print(const char* str) override {
        return write((const uint8_t*)str, strlen(str));
    }

    size_t println(const char* str) override {
        size_t len = print(str);
        len += write((const uint8_t*)"\r\n", 2);
        return len;
    }

    void flush() override {
        if (!initialized_) return;
        // Wait for TX buffer to empty
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    }

private:
    bool initialized_;
};

/**
 * ESP32 Serial interface using UART
 * Alternative for boards without USB Serial/JTAG
 */
class ESP32UartSerial : public ISerialInterface {
public:
    static constexpr const char* TAG = "ESP32UartSerial";
    static constexpr size_t BUF_SIZE = 256;

    ESP32UartSerial(uart_port_t port = UART_NUM_0, int tx_pin = -1, int rx_pin = -1)
        : port_(port)
        , tx_pin_(tx_pin)
        , rx_pin_(rx_pin)
        , initialized_(false) {}

    bool begin(uint32_t baud) override {
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
            return false;
        }

        if (tx_pin_ >= 0 || rx_pin_ >= 0) {
            ret = uart_set_pin(port_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
                return false;
            }
        }

        initialized_ = true;
        ESP_LOGI(TAG, "UART%d initialized at %lu baud", port_, (unsigned long)baud);
        return true;
    }

    int available() override {
        if (!initialized_) return 0;

        size_t available = 0;
        uart_get_buffered_data_len(port_, &available);
        return (int)available;
    }

    int read() override {
        if (!initialized_) return -1;

        uint8_t c;
        int len = uart_read_bytes(port_, &c, 1, 0);
        if (len > 0) {
            return c;
        }
        return -1;
    }

    size_t read(uint8_t* buffer, size_t size) override {
        if (!initialized_) return 0;

        int len = uart_read_bytes(port_, buffer, size, 0);
        return len > 0 ? len : 0;
    }

    size_t write(const uint8_t* data, size_t len) override {
        if (!initialized_) return 0;

        int written = uart_write_bytes(port_, (const char*)data, len);
        return written > 0 ? written : 0;
    }

    size_t print(const char* str) override {
        return write((const uint8_t*)str, strlen(str));
    }

    size_t println(const char* str) override {
        size_t len = print(str);
        len += write((const uint8_t*)"\r\n", 2);
        return len;
    }

    void flush() override {
        if (!initialized_) return;
        uart_wait_tx_done(port_, pdMS_TO_TICKS(100));
    }

private:
    uart_port_t port_;
    int tx_pin_;
    int rx_pin_;
    bool initialized_;
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
