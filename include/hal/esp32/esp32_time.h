#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include <ctime>

namespace hal {
namespace esp32 {

/**
 * ESP32 Time interface implementation
 * Uses esp_timer for milliseconds and SNTP for Unix time
 */
class ESP32Time : public ITimeInterface {
public:
    ESP32Time() : boot_time_ms_(esp_timer_get_time() / 1000) {}

    uint32_t millis() const override {
        return (uint32_t)((esp_timer_get_time() / 1000) - boot_time_ms_);
    }

    uint32_t get_unix_time() const override {
        time_t now;
        time(&now);
        // Return 0 if time is before 2020 (not synced)
        if (now < 1577836800) {  // 2020-01-01 00:00:00 UTC
            return 0;
        }
        return (uint32_t)now;
    }

    void delay_ms(uint32_t ms) override {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    /**
     * Initialize SNTP for time synchronization
     * @param ntp_server NTP server address (default: pool.ntp.org)
     */
    void init_sntp(const char* ntp_server = "pool.ntp.org") {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, ntp_server);
        esp_sntp_init();
    }

    /**
     * Check if time is synchronized via NTP
     */
    bool is_time_synced() const {
        return get_unix_time() != 0;
    }

private:
    uint64_t boot_time_ms_;
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
