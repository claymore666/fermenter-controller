#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

namespace hal {
namespace esp32 {

/**
 * ESP32 Storage interface using NVS (Non-Volatile Storage)
 */
class ESP32Storage : public IStorageInterface {
public:
    static constexpr const char* TAG = "ESP32Storage";
    static constexpr const char* NVS_NAMESPACE = "fermenter";

    ESP32Storage() : initialized_(false), handle_(0) {}

    /**
     * Initialize NVS storage
     */
    bool initialize() {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition needs erase");
            nvs_flash_erase();
            ret = nvs_flash_init();
        }

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(ret));
            return false;
        }

        ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
            return false;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "NVS storage initialized");
        return true;
    }

    bool write_blob(const char* key, const void* data, size_t len) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_set_blob(handle_, key, data, len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write blob '%s': %s", key, esp_err_to_name(ret));
            return false;
        }

        ret = nvs_commit(handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    bool read_blob(const char* key, void* data, size_t* len) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_get_blob(handle_, key, data, len);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Key '%s' not found", key);
            return false;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read blob '%s': %s", key, esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    bool write_string(const char* key, const char* value) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_set_str(handle_, key, value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write string '%s': %s", key, esp_err_to_name(ret));
            return false;
        }

        ret = nvs_commit(handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    bool read_string(const char* key, char* value, size_t max_len) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_get_str(handle_, key, value, &max_len);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return false;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read string '%s': %s", key, esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    bool write_int(const char* key, int32_t value) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_set_i32(handle_, key, value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write int '%s': %s", key, esp_err_to_name(ret));
            return false;
        }

        ret = nvs_commit(handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
            return false;
        }

        return true;
    }

    bool read_int(const char* key, int32_t* value) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_get_i32(handle_, key, value);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return false;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read int '%s': %s", key, esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    bool erase_key(const char* key) override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_erase_key(handle_, key);
        if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to erase '%s': %s", key, esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    bool commit() override {
        if (!initialized_) return false;

        esp_err_t ret = nvs_commit(handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(ret));
            return false;
        }
        return true;
    }

    ~ESP32Storage() {
        if (initialized_) {
            nvs_close(handle_);
        }
    }

private:
    bool initialized_;
    nvs_handle_t handle_;
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
