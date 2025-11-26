#pragma once

#include "hal/interfaces.h"
#include <cstdint>

#ifdef ESP32_BUILD
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace modules {

/**
 * Status LED Module
 *
 * Controls WS2812 RGB LED to indicate system state:
 * - Blue:   Provisioning mode (waiting for WiFi setup)
 * - Green:  All OK (no errors, no warnings)
 * - Yellow: Warning (NTP not synced, WiFi disconnected, etc.)
 * - Red:    Error (sensor failure, critical alarm)
 *
 * Priority: Red > Yellow > Blue > Green
 */
class StatusLed {
public:
    enum class Color {
        OFF,
        RED,
        YELLOW,
        GREEN,
        BLUE,
        WHITE,
        CYAN,
        MAGENTA
    };

    enum class Pattern {
        SOLID,
        BLINK_SLOW,    // 1 Hz
        BLINK_FAST,    // 4 Hz
        PULSE,         // Fade in/out
        BREATHE        // Slow pulse
    };

    struct State {
        bool provisioning = false;
        bool ap_client_connected = false;  // Client connected to AP during provisioning
        bool wifi_connected = false;
        bool ntp_synced = false;
        bool has_errors = false;
        bool has_warnings = false;
        bool has_alarms = false;
        bool cert_generating = false;      // SSL certificate being generated
    };

    StatusLed(hal::ITimeInterface* time_hal, uint8_t gpio_pin = 38)
        : time_hal_(time_hal)
        , gpio_pin_(gpio_pin)
        , current_color_(Color::OFF)
        , current_pattern_(Pattern::SOLID)
        , brightness_(32)  // 0-255, default low
        , last_update_(0)
        , blink_state_(false)
        , pulse_value_(0)
        , pulse_direction_(1)
        , initialized_(false) {
        // Explicitly initialize all state flags
        state_.provisioning = false;
        state_.ap_client_connected = false;
        state_.wifi_connected = false;
        state_.ntp_synced = false;
        state_.has_errors = false;
        state_.has_warnings = false;
        state_.has_alarms = false;
        state_.cert_generating = false;
    }

    /**
     * Initialize LED hardware
     */
    bool init() {
#ifdef ESP32_BUILD
        led_strip_config_t strip_config = {
            .strip_gpio_num = gpio_pin_,
            .max_leds = 1,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags = { .invert_out = false }
        };

        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
            .mem_block_symbols = 64,
            .flags = { .with_dma = false }
        };

        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_);
        if (err != ESP_OK) {
            ESP_LOGE("LED", "Failed to create LED strip: %d", err);
            return false;
        }

        led_strip_clear(led_strip_);
        initialized_ = true;
        ESP_LOGI("LED", "Status LED initialized on GPIO%d", gpio_pin_);
        return true;
#else
        initialized_ = true;
        return true;
#endif
    }

    /**
     * Update LED state based on system conditions
     * Call this periodically (e.g., every 50ms)
     */
    void update() {
        if (!initialized_) return;

        uint32_t now = time_hal_->millis();

        // Determine color based on priority
        Color target_color = Color::GREEN;
        Pattern target_pattern = Pattern::SOLID;

        // Priority: Red > Blue (provisioning/cert) > Yellow > Green
        // Note: Blue provisioning overrides yellow because WiFi will be disconnected during setup
        if (state_.has_errors || state_.has_alarms) {
            target_color = Color::RED;
            target_pattern = state_.has_alarms ? Pattern::BLINK_FAST : Pattern::SOLID;
        } else if (state_.provisioning) {
            target_color = Color::BLUE;
            // Blink fast when client connected, breathe otherwise
            target_pattern = state_.ap_client_connected ? Pattern::BLINK_FAST : Pattern::BREATHE;
        } else if (state_.cert_generating) {
            // Blue blink at 500ms during certificate generation
            target_color = Color::BLUE;
            target_pattern = Pattern::BLINK_SLOW;  // 500ms on/off
        } else if (state_.has_warnings || !state_.wifi_connected || !state_.ntp_synced) {
            target_color = Color::YELLOW;
            target_pattern = Pattern::BLINK_SLOW;
        } else {
            target_color = Color::GREEN;
            target_pattern = Pattern::SOLID;
        }

        current_color_ = target_color;
        current_pattern_ = target_pattern;

        // Update pattern animation
        uint32_t elapsed = now - last_update_;
        if (elapsed < 50) return;  // Limit update rate
        last_update_ = now;

        uint8_t effective_brightness = brightness_;

        switch (current_pattern_) {
            case Pattern::SOLID:
                effective_brightness = brightness_;
                break;

            case Pattern::BLINK_SLOW:
                if ((now / 500) % 2 == 0) {
                    effective_brightness = brightness_;
                } else {
                    effective_brightness = 0;
                }
                break;

            case Pattern::BLINK_FAST:
                if ((now / 125) % 2 == 0) {
                    effective_brightness = brightness_;
                } else {
                    effective_brightness = 0;
                }
                break;

            case Pattern::PULSE:
                pulse_value_ += pulse_direction_ * 8;
                if (pulse_value_ >= 255) {
                    pulse_value_ = 255;
                    pulse_direction_ = -1;
                } else if (pulse_value_ <= 0) {
                    pulse_value_ = 0;
                    pulse_direction_ = 1;
                }
                effective_brightness = (brightness_ * pulse_value_) / 255;
                break;

            case Pattern::BREATHE:
                // Slower, smoother pulse
                pulse_value_ += pulse_direction_ * 3;
                if (pulse_value_ >= 255) {
                    pulse_value_ = 255;
                    pulse_direction_ = -1;
                } else if (pulse_value_ <= 10) {
                    pulse_value_ = 10;
                    pulse_direction_ = 1;
                }
                effective_brightness = (brightness_ * pulse_value_) / 255;
                break;
        }

        // Set LED color
        set_color_brightness(current_color_, effective_brightness);
    }

    /**
     * Set system state for automatic color determination
     */
    void set_state(const State& state) {
        state_ = state;
    }

    /**
     * Update individual state flags
     */
    void set_provisioning(bool active) { state_.provisioning = active; }
    void set_ap_client_connected(bool connected) { state_.ap_client_connected = connected; }
    void set_wifi_connected(bool connected) { state_.wifi_connected = connected; }
    void set_ntp_synced(bool synced) { state_.ntp_synced = synced; }
    void set_has_errors(bool errors) { state_.has_errors = errors; }
    void set_has_warnings(bool warnings) { state_.has_warnings = warnings; }
    void set_has_alarms(bool alarms) { state_.has_alarms = alarms; }
    void set_cert_generating(bool generating) { state_.cert_generating = generating; }

    /**
     * Set LED brightness (0-255)
     */
    void set_brightness(uint8_t brightness) {
        brightness_ = brightness;
    }

    /**
     * Force a specific color (overrides automatic)
     */
    void set_color(Color color) {
        current_color_ = color;
        set_color_brightness(color, brightness_);
    }

    /**
     * Turn LED off
     */
    void off() {
        set_color_brightness(Color::OFF, 0);
    }

    /**
     * Start background task for LED updates
     * This ensures consistent timing for blinking/pulsing patterns
     */
    bool start_task() {
#ifdef ESP32_BUILD
        if (task_handle_ != nullptr) {
            return true;  // Already running
        }

        task_running_ = true;
        BaseType_t ret = xTaskCreate(
            led_task_func,
            "led_task",
            2048,  // Small stack - just LED updates
            this,
            2,     // Priority 2 - higher than idle, lower than critical
            &task_handle_
        );

        if (ret != pdPASS) {
            ESP_LOGE("LED", "Failed to create LED task");
            task_running_ = false;
            return false;
        }

        ESP_LOGI("LED", "LED background task started");
        return true;
#else
        return true;  // Simulator doesn't need task
#endif
    }

    /**
     * Stop background task
     */
    void stop_task() {
#ifdef ESP32_BUILD
        if (task_handle_ != nullptr) {
            task_running_ = false;
            vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
            if (task_handle_ != nullptr) {
                vTaskDelete(task_handle_);
                task_handle_ = nullptr;
            }
        }
#endif
    }

    /**
     * Get current color
     */
    Color get_color() const { return current_color_; }

    /**
     * Get current state
     */
    const State& get_state() const { return state_; }

    /**
     * Get color name as string
     */
    static const char* color_to_string(Color color) {
        switch (color) {
            case Color::OFF:     return "OFF";
            case Color::RED:     return "RED";
            case Color::YELLOW:  return "YELLOW";
            case Color::GREEN:   return "GREEN";
            case Color::BLUE:    return "BLUE";
            case Color::WHITE:   return "WHITE";
            case Color::CYAN:    return "CYAN";
            case Color::MAGENTA: return "MAGENTA";
            default:             return "UNKNOWN";
        }
    }

private:
#ifdef ESP32_BUILD
    /**
     * Background task for LED updates
     */
    static void led_task_func(void* param) {
        StatusLed* self = static_cast<StatusLed*>(param);

        while (self->task_running_) {
            self->update();
            vTaskDelay(pdMS_TO_TICKS(50));  // 20Hz update rate for smooth animations
        }

        self->task_handle_ = nullptr;
        vTaskDelete(NULL);
    }
#endif

    void set_color_brightness(Color color, uint8_t brightness) {
#ifdef ESP32_BUILD
        if (!led_strip_) return;

        uint8_t r = 0, g = 0, b = 0;

        switch (color) {
            case Color::OFF:
                r = g = b = 0;
                break;
            case Color::RED:
                r = brightness;
                break;
            case Color::GREEN:
                g = brightness;
                break;
            case Color::BLUE:
                b = brightness;
                break;
            case Color::YELLOW:
                r = brightness;
                g = brightness / 2;  // Reduce green for warmer yellow
                break;
            case Color::WHITE:
                r = g = b = brightness;
                break;
            case Color::CYAN:
                g = brightness;
                b = brightness;
                break;
            case Color::MAGENTA:
                r = brightness;
                b = brightness;
                break;
        }

        // Swap R and G - LED appears to be RGB despite GRB format
        led_strip_set_pixel(led_strip_, 0, g, r, b);
        led_strip_refresh(led_strip_);
#else
        (void)color;
        (void)brightness;
#endif
    }

    hal::ITimeInterface* time_hal_;
    uint8_t gpio_pin_;
    Color current_color_;
    Pattern current_pattern_;
    uint8_t brightness_;
    uint32_t last_update_;
    bool blink_state_;
    int16_t pulse_value_;
    int8_t pulse_direction_;
    bool initialized_;
    State state_;

#ifdef ESP32_BUILD
    led_strip_handle_t led_strip_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    volatile bool task_running_ = false;
#endif
};

} // namespace modules
