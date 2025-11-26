#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "driver/twai.h"
#include "esp_log.h"

namespace hal {
namespace esp32 {

/**
 * ESP32 CAN bus interface using TWAI (Two-Wire Automotive Interface)
 *
 * Waveshare ESP32-S3-POE-ETH-8DI-8DO pin mapping:
 *   CAN TX: GPIO2
 *   CAN RX: GPIO3
 */
class ESP32CAN : public ICANInterface {
public:
    static constexpr const char* TAG = "ESP32CAN";

    // Hardware constants for Waveshare board
    static constexpr int CAN_TX_PIN = 2;
    static constexpr int CAN_RX_PIN = 3;

    ESP32CAN() : initialized_(false), tx_count_(0), rx_count_(0), error_count_(0) {}

    ~ESP32CAN() {
        if (initialized_) {
            stop();
        }
    }

    bool initialize(uint32_t bitrate = 500000) override {
        if (initialized_) {
            ESP_LOGW(TAG, "Already initialized");
            return true;
        }

        // Configure timing based on bitrate
        twai_timing_config_t timing;
        switch (bitrate) {
            case 125000:
                timing = TWAI_TIMING_CONFIG_125KBITS();
                break;
            case 250000:
                timing = TWAI_TIMING_CONFIG_250KBITS();
                break;
            case 500000:
                timing = TWAI_TIMING_CONFIG_500KBITS();
                break;
            case 1000000:
                timing = TWAI_TIMING_CONFIG_1MBITS();
                break;
            default:
                ESP_LOGE(TAG, "Unsupported bitrate: %lu", bitrate);
                return false;
        }

        // General configuration - normal mode
        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
            (gpio_num_t)CAN_TX_PIN,
            (gpio_num_t)CAN_RX_PIN,
            TWAI_MODE_NORMAL
        );

        // Accept all messages (no filter)
        twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        // Install TWAI driver
        esp_err_t ret = twai_driver_install(&general, &timing, &filter);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(ret));
            return false;
        }

        // Start TWAI driver
        ret = twai_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start TWAI: %s", esp_err_to_name(ret));
            twai_driver_uninstall();
            return false;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "CAN initialized at %lu bps (TX=GPIO%d, RX=GPIO%d)",
                 bitrate, CAN_TX_PIN, CAN_RX_PIN);
        return true;
    }

    bool send(const CANMessage& msg, uint32_t timeout_ms = 100) override {
        if (!initialized_) return false;

        // Check for bus-off state and attempt recovery
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (status.state == TWAI_STATE_BUS_OFF) {
                ESP_LOGW(TAG, "Bus-off detected, initiating recovery");
                twai_initiate_recovery();
                vTaskDelay(pdMS_TO_TICKS(100));
                error_count_++;
                return false;
            }
        }

        twai_message_t twai_msg = {};
        twai_msg.identifier = msg.id;
        twai_msg.data_length_code = msg.len;
        twai_msg.extd = msg.extended ? 1 : 0;
        twai_msg.rtr = msg.rtr ? 1 : 0;

        for (int i = 0; i < msg.len && i < 8; i++) {
            twai_msg.data[i] = msg.data[i];
        }

        esp_err_t ret = twai_transmit(&twai_msg, pdMS_TO_TICKS(timeout_ms));
        if (ret == ESP_OK) {
            tx_count_++;
            return true;
        } else {
            error_count_++;
            if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "TX failed: %s", esp_err_to_name(ret));
            }
            return false;
        }
    }

    bool receive(CANMessage& msg, uint32_t timeout_ms = 0) override {
        if (!initialized_) return false;

        twai_message_t twai_msg;
        esp_err_t ret = twai_receive(&twai_msg, pdMS_TO_TICKS(timeout_ms));

        if (ret == ESP_OK) {
            msg.id = twai_msg.identifier;
            msg.len = twai_msg.data_length_code;
            msg.extended = twai_msg.extd;
            msg.rtr = twai_msg.rtr;

            for (int i = 0; i < msg.len && i < 8; i++) {
                msg.data[i] = twai_msg.data[i];
            }

            rx_count_++;
            return true;
        }

        return false;
    }

    bool available() const override {
        if (!initialized_) return false;

        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            return status.msgs_to_rx > 0;
        }
        return false;
    }

    uint32_t get_tx_count() const override { return tx_count_; }
    uint32_t get_rx_count() const override { return rx_count_; }
    uint32_t get_error_count() const override { return error_count_; }

    void stop() override {
        if (!initialized_) return;

        twai_stop();
        twai_driver_uninstall();
        initialized_ = false;
        ESP_LOGI(TAG, "CAN stopped");
    }

    /**
     * Get detailed bus status
     */
    bool get_status(twai_status_info_t& status) const {
        if (!initialized_) return false;
        return twai_get_status_info(&status) == ESP_OK;
    }

    /**
     * Recover from bus-off state
     */
    bool recover() {
        if (!initialized_) return false;
        return twai_initiate_recovery() == ESP_OK;
    }

private:
    bool initialized_;
    uint32_t tx_count_;
    uint32_t rx_count_;
    uint32_t error_count_;
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
