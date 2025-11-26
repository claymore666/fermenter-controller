#pragma once

#include "hal/interfaces.h"
#include <cstdint>
#include <ctime>

#ifdef ESP32_BUILD
#include "esp_sntp.h"
#include "esp_log.h"
#endif

namespace modules {

/**
 * NTP Time Synchronization Module
 * Handles time sync, timezone configuration, and sync status tracking
 */
class NtpModule {
public:
    struct Config {
        const char* server;           // NTP server (default: pool.ntp.org)
        const char* timezone;         // POSIX timezone string
        uint32_t sync_interval_ms;    // Re-sync interval (default: 1 hour)
        uint32_t boot_timeout_ms;     // Initial sync timeout (default: 10s)
    };

    enum class SyncStatus {
        NOT_STARTED,
        IN_PROGRESS,
        SYNCED,
        FAILED
    };

    NtpModule(hal::ITimeInterface* time_hal)
        : time_hal_(time_hal)
        , status_(SyncStatus::NOT_STARTED)
        , last_sync_time_(0)
        , sync_count_(0)
        , fail_count_(0) {
        // Default config
        config_.server = "pool.ntp.org";
        config_.timezone = "UTC0";
        config_.sync_interval_ms = 3600000;  // 1 hour
        config_.boot_timeout_ms = 10000;     // 10 seconds
    }

    /**
     * Configure NTP settings
     */
    void configure(const Config& config) {
        config_ = config;
    }

    /**
     * Initialize and start NTP synchronization
     * @return true if sync started successfully
     */
    bool init() {
#ifdef ESP32_BUILD
        // Set timezone
        setenv("TZ", config_.timezone, 1);
        tzset();

        // Configure SNTP
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, config_.server);

        // Set sync notification callback
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);

        // Initialize SNTP
        esp_sntp_init();

        status_ = SyncStatus::IN_PROGRESS;
        ESP_LOGI("NTP", "Initialized with server: %s, timezone: %s",
                 config_.server, config_.timezone);

        return true;
#else
        // Simulator: just mark as synced
        status_ = SyncStatus::SYNCED;
        last_sync_time_ = time_hal_->get_unix_time();
        sync_count_++;
        return true;
#endif
    }

    /**
     * Wait for initial time sync (blocking)
     * @param timeout_ms Maximum time to wait
     * @return true if synced within timeout
     */
    bool wait_for_sync(uint32_t timeout_ms = 0) {
        if (timeout_ms == 0) {
            timeout_ms = config_.boot_timeout_ms;
        }

#ifdef ESP32_BUILD
        uint32_t start = time_hal_->millis();
        while (!is_synced() && (time_hal_->millis() - start) < timeout_ms) {
            time_hal_->delay_ms(100);
        }

        if (is_synced()) {
            last_sync_time_ = time_hal_->get_unix_time();
            sync_count_++;
            status_ = SyncStatus::SYNCED;
            ESP_LOGI("NTP", "Time synchronized: %lu", last_sync_time_);
            return true;
        } else {
            fail_count_++;
            status_ = SyncStatus::FAILED;
            ESP_LOGW("NTP", "Sync timeout after %lu ms", timeout_ms);
            return false;
        }
#else
        // Simulator always succeeds
        (void)timeout_ms;
        return true;
#endif
    }

    /**
     * Check if time is synchronized
     */
    bool is_synced() const {
#ifdef ESP32_BUILD
        time_t now;
        time(&now);
        // Consider synced if time is after 2020
        return now > 1577836800;
#else
        return status_ == SyncStatus::SYNCED;
#endif
    }

    /**
     * Get current sync status
     */
    SyncStatus get_status() const {
        return status_;
    }

    /**
     * Get current Unix timestamp
     */
    uint32_t get_unix_time() const {
        return time_hal_->get_unix_time();
    }

    /**
     * Get local time struct
     */
    struct tm get_local_time() const {
        time_t now = (time_t)time_hal_->get_unix_time();
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        return timeinfo;
    }

    /**
     * Format current time as ISO 8601 string
     * @param buffer Output buffer (min 26 chars)
     * @param size Buffer size
     */
    void format_iso8601(char* buffer, size_t size) const {
        struct tm timeinfo = get_local_time();
        strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", &timeinfo);
    }

    /**
     * Get time since last sync in seconds
     */
    uint32_t get_seconds_since_sync() const {
        if (last_sync_time_ == 0) return 0;
        return time_hal_->get_unix_time() - last_sync_time_;
    }

    /**
     * Check if re-sync is needed
     */
    bool needs_resync() const {
        if (last_sync_time_ == 0) return true;
        uint32_t elapsed_ms = (time_hal_->get_unix_time() - last_sync_time_) * 1000;
        return elapsed_ms >= config_.sync_interval_ms;
    }

    /**
     * Force a time resync
     */
    void resync() {
#ifdef ESP32_BUILD
        esp_sntp_restart();
        status_ = SyncStatus::IN_PROGRESS;
        ESP_LOGI("NTP", "Resync initiated");
#endif
    }

    /**
     * Get sync statistics
     */
    uint32_t get_sync_count() const { return sync_count_; }
    uint32_t get_fail_count() const { return fail_count_; }
    uint32_t get_last_sync_time() const { return last_sync_time_; }

    /**
     * Set timezone (POSIX format)
     * Examples:
     *   "UTC0"
     *   "EST5EDT,M3.2.0,M11.1.0"
     *   "CET-1CEST,M3.5.0,M10.5.0/3"
     */
    void set_timezone(const char* tz) {
        config_.timezone = tz;
#ifdef ESP32_BUILD
        setenv("TZ", tz, 1);
        tzset();
        ESP_LOGI("NTP", "Timezone set to: %s", tz);
#endif
    }

private:
#ifdef ESP32_BUILD
    static void time_sync_notification_cb(struct timeval* tv) {
        ESP_LOGI("NTP", "Time synchronized from NTP server");
        (void)tv;
    }
#endif

    hal::ITimeInterface* time_hal_;
    Config config_;
    SyncStatus status_;
    uint32_t last_sync_time_;
    uint32_t sync_count_;
    uint32_t fail_count_;
};

} // namespace modules
