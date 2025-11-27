#pragma once

/**
 * OTA (Over-The-Air) Firmware Update Manager
 *
 * Handles firmware updates via HTTP upload with:
 * - System pause during update (stops MODBUS, PID, WebSocket)
 * - Progress tracking
 * - Rollback support
 * - Version validation
 */

#ifdef ESP32_BUILD
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

#include "version.h"
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace modules {

/**
 * OTA update state
 */
enum class OtaState {
    IDLE,           // No update in progress
    PREPARING,      // Pausing system, preparing partition
    DOWNLOADING,    // Downloading firmware from URL
    RECEIVING,      // Receiving firmware data (upload)
    VERIFYING,      // Verifying firmware integrity
    COMPLETE,       // Update complete, pending reboot
    FAILED          // Update failed
};

/**
 * OTA update result
 */
struct OtaResult {
    bool success;
    char message[128];

    OtaResult() : success(false) {
        message[0] = '\0';
    }

    void set_error(const char* msg) {
        success = false;
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }

    void set_success(const char* msg) {
        success = true;
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }
};

/**
 * OTA progress information
 */
struct OtaProgress {
    OtaState state;
    uint32_t bytes_received;
    uint32_t bytes_total;
    uint8_t percent;
    char status_message[64];

    OtaProgress()
        : state(OtaState::IDLE)
        , bytes_received(0)
        , bytes_total(0)
        , percent(0) {
        strcpy(status_message, "Idle");
    }

    void reset() {
        state = OtaState::IDLE;
        bytes_received = 0;
        bytes_total = 0;
        percent = 0;
        strcpy(status_message, "Idle");
    }

    void update_progress() {
        if (bytes_total > 0) {
            percent = (uint8_t)((bytes_received * 100ULL) / bytes_total);
        } else {
            percent = 0;
        }
    }
};

/**
 * Partition information
 */
struct PartitionInfo {
    char label[16];
    uint32_t address;
    uint32_t size;
    char app_version[32];
    bool is_running;
    bool is_valid;

    PartitionInfo()
        : address(0)
        , size(0)
        , is_running(false)
        , is_valid(false) {
        label[0] = '\0';
        app_version[0] = '\0';
    }
};

/**
 * Callback type for system pause/resume
 * Called when OTA needs to pause or resume system activities
 */
typedef void (*OtaSystemCallback)(bool pause);

/**
 * OTA Manager
 */
class OtaManager {
public:
    static constexpr size_t WRITE_BUFFER_SIZE = 4096;  // Write in 4KB chunks

    OtaManager()
        : state_(OtaState::IDLE)
        , system_callback_(nullptr)
#ifdef ESP32_BUILD
        , ota_handle_(0)
        , update_partition_(nullptr)
        , mutex_(nullptr)
#endif
    {
        progress_.reset();
#ifdef ESP32_BUILD
        mutex_ = xSemaphoreCreateMutex();
#endif
    }

    ~OtaManager() {
#ifdef ESP32_BUILD
        if (mutex_) {
            vSemaphoreDelete(mutex_);
        }
#endif
    }

    /**
     * Set callback for pausing/resuming system activities
     * @param callback Function to call with pause=true to pause, pause=false to resume
     */
    void set_system_callback(OtaSystemCallback callback) {
        system_callback_ = callback;
    }

    /**
     * Check if OTA update is in progress
     */
    bool is_update_in_progress() const {
        return state_ != OtaState::IDLE && state_ != OtaState::COMPLETE && state_ != OtaState::FAILED;
    }

    /**
     * Get current progress
     */
    OtaProgress get_progress() const {
        return progress_;
    }

    /**
     * Get current state
     */
    OtaState get_state() const {
        return state_;
    }

    /**
     * Begin OTA update
     * @param firmware_size Total size of firmware in bytes (0 if unknown)
     * @return Result with success/failure and message
     */
    OtaResult begin(uint32_t firmware_size) {
        OtaResult result;

#ifdef ESP32_BUILD
        if (!lock()) {
            result.set_error("Failed to acquire OTA lock");
            return result;
        }

        if (is_update_in_progress()) {
            unlock();
            result.set_error("Update already in progress");
            return result;
        }

        // Set state and pause system
        state_ = OtaState::PREPARING;
        progress_.state = OtaState::PREPARING;
        progress_.bytes_total = firmware_size;
        progress_.bytes_received = 0;
        strcpy(progress_.status_message, "Preparing...");

        // Pause system activities (MODBUS, PID, WebSocket, etc.)
        if (system_callback_) {
            ESP_LOGI("OTA", "Pausing system for OTA update");
            system_callback_(true);
        }

        // Get the next OTA partition
        update_partition_ = esp_ota_get_next_update_partition(NULL);
        if (!update_partition_) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            strcpy(progress_.status_message, "No OTA partition found");
            resume_system();
            unlock();
            result.set_error("No OTA partition available");
            return result;
        }

        ESP_LOGI("OTA", "Writing to partition: %s at 0x%08lx, size %lu",
                 update_partition_->label,
                 (unsigned long)update_partition_->address,
                 (unsigned long)update_partition_->size);

        // Check firmware size fits
        if (firmware_size > 0 && firmware_size > update_partition_->size) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            strcpy(progress_.status_message, "Firmware too large");
            resume_system();
            unlock();
            result.set_error("Firmware exceeds partition size");
            return result;
        }

        // Begin OTA write (OTA_SIZE_UNKNOWN allows streaming without knowing size upfront)
        esp_err_t err = esp_ota_begin(update_partition_,
                                       firmware_size > 0 ? firmware_size : OTA_SIZE_UNKNOWN,
                                       &ota_handle_);
        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "OTA begin failed: 0x%x", err);
            resume_system();
            unlock();
            result.set_error(progress_.status_message);
            return result;
        }

        state_ = OtaState::RECEIVING;
        progress_.state = OtaState::RECEIVING;
        strcpy(progress_.status_message, "Receiving firmware...");

        unlock();
        result.set_success("OTA update started");
        return result;
#else
        result.set_error("OTA not supported in simulator");
        return result;
#endif
    }

    /**
     * Write firmware data chunk
     * @param data Pointer to firmware data
     * @param length Length of data in bytes
     * @return Result with success/failure
     */
    OtaResult write(const void* data, size_t length) {
        OtaResult result;

#ifdef ESP32_BUILD
        if (!lock()) {
            result.set_error("Failed to acquire OTA lock");
            return result;
        }

        if (state_ != OtaState::RECEIVING) {
            unlock();
            result.set_error("OTA not in receiving state");
            return result;
        }

        esp_err_t err = esp_ota_write(ota_handle_, data, length);
        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "Write failed: 0x%x", err);
            unlock();
            result.set_error(progress_.status_message);
            return result;
        }

        progress_.bytes_received += length;
        progress_.update_progress();
        snprintf(progress_.status_message, sizeof(progress_.status_message),
                 "Receiving: %lu/%lu bytes (%d%%)",
                 (unsigned long)progress_.bytes_received,
                 (unsigned long)progress_.bytes_total,
                 progress_.percent);

        // Yield to allow other tasks (watchdog, etc.)
        vTaskDelay(1);

        unlock();
        result.set_success("Data written");
        return result;
#else
        result.set_error("OTA not supported in simulator");
        return result;
#endif
    }

    /**
     * End OTA update (finalize and validate)
     * @return Result with success/failure
     */
    OtaResult end() {
        OtaResult result;

#ifdef ESP32_BUILD
        if (!lock()) {
            result.set_error("Failed to acquire OTA lock");
            return result;
        }

        if (state_ != OtaState::RECEIVING) {
            unlock();
            result.set_error("OTA not in receiving state");
            return result;
        }

        state_ = OtaState::VERIFYING;
        progress_.state = OtaState::VERIFYING;
        strcpy(progress_.status_message, "Verifying firmware...");

        // Finalize OTA (validates image)
        esp_err_t err = esp_ota_end(ota_handle_);
        ota_handle_ = 0;

        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                strcpy(progress_.status_message, "Invalid firmware image");
                result.set_error("Firmware validation failed");
            } else {
                snprintf(progress_.status_message, sizeof(progress_.status_message),
                         "OTA end failed: 0x%x", err);
                result.set_error(progress_.status_message);
            }
            resume_system();
            unlock();
            return result;
        }

        // Set boot partition to the new firmware
        err = esp_ota_set_boot_partition(update_partition_);
        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "Set boot partition failed: 0x%x", err);
            resume_system();
            unlock();
            result.set_error(progress_.status_message);
            return result;
        }

        state_ = OtaState::COMPLETE;
        progress_.state = OtaState::COMPLETE;
        progress_.percent = 100;
        strcpy(progress_.status_message, "Update complete, reboot required");

        ESP_LOGI("OTA", "Firmware update complete, new boot partition: %s",
                 update_partition_->label);

        unlock();
        result.set_success("Firmware update successful, reboot to apply");
        return result;
#else
        result.set_error("OTA not supported in simulator");
        return result;
#endif
    }

    /**
     * Abort OTA update
     * @return Result with success/failure
     */
    OtaResult abort() {
        OtaResult result;

#ifdef ESP32_BUILD
        if (!lock()) {
            result.set_error("Failed to acquire OTA lock");
            return result;
        }

        if (ota_handle_ != 0) {
            esp_ota_abort(ota_handle_);
            ota_handle_ = 0;
        }

        state_ = OtaState::FAILED;
        progress_.state = OtaState::FAILED;
        strcpy(progress_.status_message, "Update aborted");

        resume_system();

        unlock();
        result.set_success("OTA update aborted");
        return result;
#else
        result.set_error("OTA not supported in simulator");
        return result;
#endif
    }

    /**
     * Reset state after failed or completed update
     */
    void reset() {
#ifdef ESP32_BUILD
        lock();
        if (ota_handle_ != 0) {
            esp_ota_abort(ota_handle_);
            ota_handle_ = 0;
        }
        state_ = OtaState::IDLE;
        progress_.reset();
        resume_system();
        unlock();
#else
        state_ = OtaState::IDLE;
        progress_.reset();
#endif
    }

    /**
     * Mark current firmware as valid (prevents rollback)
     * Call this after successful boot to confirm the update
     * @return Result with success/failure
     */
    OtaResult confirm_update() {
        OtaResult result;

#ifdef ESP32_BUILD
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            result.set_success("Firmware confirmed as valid");
        } else if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
            result.set_success("Already confirmed (not in rollback state)");
        } else {
            result.set_error("Failed to confirm firmware");
        }
#else
        result.set_error("OTA not supported in simulator");
#endif
        return result;
    }

    /**
     * Rollback to previous firmware
     * @return Result with success/failure (will reboot on success)
     */
    OtaResult rollback() {
        OtaResult result;

#ifdef ESP32_BUILD
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* other = esp_ota_get_next_update_partition(NULL);

        if (!other) {
            result.set_error("No rollback partition available");
            return result;
        }

        // Check if other partition has valid app
        esp_app_desc_t app_desc;
        esp_err_t err = esp_ota_get_partition_description(other, &app_desc);
        if (err != ESP_OK) {
            result.set_error("Rollback partition has no valid firmware");
            return result;
        }

        ESP_LOGI("OTA", "Rolling back from %s to %s (version %s)",
                 running->label, other->label, app_desc.version);

        err = esp_ota_set_boot_partition(other);
        if (err != ESP_OK) {
            result.set_error("Failed to set rollback partition");
            return result;
        }

        result.set_success("Rollback prepared, rebooting...");

        // Reboot to apply rollback
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
#else
        result.set_error("OTA not supported in simulator");
#endif
        return result;
    }

    /**
     * Download and install firmware from URL (e.g., GitHub release)
     *
     * Default URL pulls from GitHub OTA branch:
     * https://raw.githubusercontent.com/claymore666/fermenter-controller/OTA/firmware.bin
     *
     * @param url URL to firmware binary (NULL for default GitHub OTA branch)
     * @return Result with success/failure
     */
    OtaResult download_from_url(const char* url = nullptr) {
        OtaResult result;

#ifdef ESP32_BUILD
        if (!lock()) {
            result.set_error("Failed to acquire OTA lock");
            return result;
        }

        if (is_update_in_progress()) {
            unlock();
            result.set_error("Update already in progress");
            return result;
        }

        // Default to GitHub OTA branch
        const char* firmware_url = url;
        if (!firmware_url || firmware_url[0] == '\0') {
            firmware_url = "https://raw.githubusercontent.com/claymore666/fermenter-controller/OTA/firmware.bin";
        }

        // Set state and pause system
        state_ = OtaState::PREPARING;
        progress_.state = OtaState::PREPARING;
        progress_.bytes_total = 0;  // Unknown until download starts
        progress_.bytes_received = 0;
        strcpy(progress_.status_message, "Preparing download...");

        // Pause system activities
        if (system_callback_) {
            ESP_LOGI("OTA", "Pausing system for OTA download");
            system_callback_(true);
        }

        ESP_LOGI("OTA", "Downloading firmware from: %s", firmware_url);

        // Configure HTTP client
        esp_http_client_config_t http_config = {};
        http_config.url = firmware_url;
        http_config.timeout_ms = 30000;
        http_config.keep_alive_enable = true;
        http_config.crt_bundle_attach = esp_crt_bundle_attach;  // Use certificate bundle for HTTPS

        // Configure OTA
        esp_https_ota_config_t ota_config = {};
        ota_config.http_config = &http_config;

        state_ = OtaState::DOWNLOADING;
        progress_.state = OtaState::DOWNLOADING;
        strcpy(progress_.status_message, "Downloading...");

        // Start OTA download
        esp_https_ota_handle_t https_ota_handle = NULL;
        esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "OTA begin failed: 0x%x", err);
            resume_system();
            unlock();
            result.set_error(progress_.status_message);
            return result;
        }

        // Get image size if available
        int image_size = esp_https_ota_get_image_size(https_ota_handle);
        if (image_size > 0) {
            progress_.bytes_total = image_size;
            ESP_LOGI("OTA", "Firmware size: %d bytes", image_size);
        }

        // Download and flash in chunks
        while (true) {
            err = esp_https_ota_perform(https_ota_handle);
            if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                break;
            }

            // Update progress
            int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
            progress_.bytes_received = read_len;
            progress_.update_progress();
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "Downloading: %lu/%lu bytes (%d%%)",
                     (unsigned long)progress_.bytes_received,
                     (unsigned long)progress_.bytes_total,
                     progress_.percent);

            // Yield to watchdog
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (err != ESP_OK) {
            esp_https_ota_abort(https_ota_handle);
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            snprintf(progress_.status_message, sizeof(progress_.status_message),
                     "Download failed: 0x%x", err);
            resume_system();
            unlock();
            result.set_error(progress_.status_message);
            return result;
        }

        // Verify the firmware image
        state_ = OtaState::VERIFYING;
        progress_.state = OtaState::VERIFYING;
        strcpy(progress_.status_message, "Verifying firmware...");

        if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
            esp_https_ota_abort(https_ota_handle);
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            strcpy(progress_.status_message, "Incomplete download");
            resume_system();
            unlock();
            result.set_error("Firmware download incomplete");
            return result;
        }

        // Finish OTA (validates and sets boot partition)
        err = esp_https_ota_finish(https_ota_handle);
        if (err != ESP_OK) {
            state_ = OtaState::FAILED;
            progress_.state = OtaState::FAILED;
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                strcpy(progress_.status_message, "Invalid firmware image");
                result.set_error("Firmware validation failed");
            } else {
                snprintf(progress_.status_message, sizeof(progress_.status_message),
                         "OTA finish failed: 0x%x", err);
                result.set_error(progress_.status_message);
            }
            resume_system();
            unlock();
            return result;
        }

        state_ = OtaState::COMPLETE;
        progress_.state = OtaState::COMPLETE;
        progress_.percent = 100;
        strcpy(progress_.status_message, "Download complete, reboot required");

        ESP_LOGI("OTA", "Firmware download complete from %s", firmware_url);

        unlock();
        result.set_success("Firmware download successful, reboot to apply");
        return result;
#else
        (void)url;
        result.set_error("OTA not supported in simulator");
        return result;
#endif
    }

    /**
     * Get information about current and update partitions
     */
    void get_partition_info(PartitionInfo& running, PartitionInfo& other) {
#ifdef ESP32_BUILD
        // Running partition
        const esp_partition_t* running_part = esp_ota_get_running_partition();
        if (running_part) {
            strncpy(running.label, running_part->label, sizeof(running.label) - 1);
            running.address = running_part->address;
            running.size = running_part->size;
            running.is_running = true;
            running.is_valid = true;

            esp_app_desc_t desc;
            if (esp_ota_get_partition_description(running_part, &desc) == ESP_OK) {
                strncpy(running.app_version, desc.version, sizeof(running.app_version) - 1);
            } else {
                strcpy(running.app_version, VERSION_STRING);
            }
        }

        // Other (update target) partition
        const esp_partition_t* other_part = esp_ota_get_next_update_partition(NULL);
        if (other_part) {
            strncpy(other.label, other_part->label, sizeof(other.label) - 1);
            other.address = other_part->address;
            other.size = other_part->size;
            other.is_running = false;

            esp_app_desc_t desc;
            if (esp_ota_get_partition_description(other_part, &desc) == ESP_OK) {
                strncpy(other.app_version, desc.version, sizeof(other.app_version) - 1);
                other.is_valid = true;
            } else {
                strcpy(other.app_version, "(empty)");
                other.is_valid = false;
            }
        }
#else
        strcpy(running.label, "app0");
        running.address = 0x10000;
        running.size = 4 * 1024 * 1024;
        strcpy(running.app_version, VERSION_STRING);
        running.is_running = true;
        running.is_valid = true;

        strcpy(other.label, "app1");
        other.address = 0x410000;
        other.size = 4 * 1024 * 1024;
        strcpy(other.app_version, "(simulator)");
        other.is_running = false;
        other.is_valid = false;
#endif
    }

    /**
     * Check if running from OTA partition that needs confirmation
     * @return true if firmware needs confirmation to prevent rollback
     */
    bool needs_confirmation() {
#ifdef ESP32_BUILD
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
            return state == ESP_OTA_IMG_PENDING_VERIFY;
        }
#endif
        return false;
    }

private:
    OtaState state_;
    OtaProgress progress_;
    OtaSystemCallback system_callback_;

#ifdef ESP32_BUILD
    esp_ota_handle_t ota_handle_;
    const esp_partition_t* update_partition_;
    SemaphoreHandle_t mutex_;

    bool lock() {
        if (!mutex_) return false;
        return xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) == pdTRUE;
    }

    void unlock() {
        if (mutex_) {
            xSemaphoreGive(mutex_);
        }
    }

    void resume_system() {
        if (system_callback_) {
            ESP_LOGI("OTA", "Resuming system after OTA");
            system_callback_(false);
        }
    }
#else
    bool lock() { return true; }
    void unlock() {}
    void resume_system() {}
#endif
};

} // namespace modules
