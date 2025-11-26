#pragma once

#include "hal/interfaces.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config.h"
#include "modules/safety_controller.h"
#include "modules/fermentation_plan.h"
#include "security/secure_utils.h"
#include "security/cert_generator.h"
#include "version.h"
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <functional>

#ifdef WIFI_NTP_ENABLED
#include "modules/wifi_provisioning.h"
#endif

#ifdef CAN_ENABLED
#include "modules/can_module.h"
#endif

#ifdef ESP32_BUILD
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include <time.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Firmware version - use VERSION_STRING from version.h
#define FIRMWARE_VERSION VERSION_STRING

// Log tag for security events
#ifdef ESP32_BUILD
static const char* HTTP_LOG_TAG = "http_security";
#endif

namespace modules {

/**
 * HTTP Server for web administration
 * Provides REST API mirroring debug console functionality
 */
class HttpServer {
public:
    static constexpr size_t MAX_RESPONSE_SIZE = 4096;
    static constexpr size_t MAX_SESSION_TOKEN = security::SESSION_TOKEN_BUF_SIZE;
    static constexpr uint32_t SESSION_TIMEOUT_MS = 3600000;  // 1 hour

    // Rate limiting constants
    static constexpr uint8_t MAX_LOGIN_ATTEMPTS = 10;
    static constexpr uint32_t LOGIN_LOCKOUT_MS = 300000;     // 5 minutes
    static constexpr uint32_t LOGIN_BACKOFF_BASE_MS = 1000;  // 1 second base

    // Password hash salt (device-specific)
    static constexpr const char* PASSWORD_SALT = "fermenter_v1";

    struct Session {
        char token[MAX_SESSION_TOKEN];
        uint32_t last_activity;
        uint32_t created_at;
        bool valid;
    };

    // Rate limiting state
    struct RateLimitState {
        uint8_t failed_attempts;
        uint32_t last_attempt_time;
        uint32_t lockout_until;
    };

    HttpServer(hal::ITimeInterface* time,
               core::StateManager* state,
               core::EventBus* events,
               core::SystemConfig* config,
               SafetyController* safety,
               FermentationPlanManager* plans,
               hal::IModbusInterface* modbus,
               hal::IStorageInterface* storage = nullptr,
               hal::IGPIOInterface* gpio = nullptr)
        : time_(time)
        , state_(state)
        , events_(events)
        , config_(config)
        , safety_(safety)
        , plans_(plans)
        , modbus_(modbus)
        , storage_(storage)
        , gpio_(gpio)
#ifdef WIFI_NTP_ENABLED
        , wifi_prov_(nullptr)
#endif
#ifdef CAN_ENABLED
        , can_module_(nullptr)
#endif
        , running_(false)
        , provisioned_(false) {
        memset(admin_password_hash_, 0, sizeof(admin_password_hash_));
        memset(&session_, 0, sizeof(session_));
        memset(&rate_limit_, 0, sizeof(rate_limit_));

        // Try to load provisioning state from storage
        load_provisioning_state();
    }

#ifdef WIFI_NTP_ENABLED
    void set_wifi_provisioning(WifiProvisioning* wifi) { wifi_prov_ = wifi; }
#endif

#ifdef CAN_ENABLED
    void set_can_module(void* can) { can_module_ = can; }
#endif

#ifdef ESP32_BUILD
    /**
     * Start HTTPS server
     * @param port Server port (default 443 for HTTPS)
     * @param use_ssl Enable HTTPS with self-signed certificate
     * @return true on success
     */
    bool start(uint16_t port = 443, bool use_ssl = true) {
        if (running_) return true;

        ESP_LOGI("HTTP", "Starting %s server on port %d", use_ssl ? "HTTPS" : "HTTP", port);

        // Mount SPIFFS for static file serving
        esp_vfs_spiffs_conf_t spiffs_conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false
        };
        esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
        if (spiffs_ret != ESP_OK && spiffs_ret != ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE means already mounted (OK)
            if (spiffs_ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGW("HTTP", "SPIFFS partition not found - static files disabled");
            } else if (spiffs_ret == ESP_FAIL) {
                ESP_LOGW("HTTP", "SPIFFS mount failed - static files disabled");
            } else {
                ESP_LOGW("HTTP", "SPIFFS init error: %s", esp_err_to_name(spiffs_ret));
            }
            // Continue without static files - API still works
        } else {
            size_t total = 0, used = 0;
            esp_spiffs_info(NULL, &total, &used);
            ESP_LOGI("HTTP", "SPIFFS mounted: %d/%d bytes used", used, total);
        }

        if (use_ssl) {
            // Generate self-signed certificate if not exists
            if (!load_or_generate_certificate()) {
                ESP_LOGE("HTTP", "Failed to setup SSL certificate");
                return false;
            }

            httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
            config.httpd.server_port = port;
            config.httpd.ctrl_port = port + 1;
            config.httpd.max_uri_handlers = 40;
            config.httpd.stack_size = 8192;
            config.httpd.lru_purge_enable = true;
            config.httpd.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard matching

            // Use embedded certificate
            config.servercert = server_cert_;
            config.servercert_len = server_cert_len_;
            config.prvtkey_pem = server_key_;
            config.prvtkey_len = server_key_len_;

            esp_err_t ret = httpd_ssl_start(&server_, &config);
            if (ret != ESP_OK) {
                ESP_LOGE("HTTP", "Failed to start HTTPS server: %s", esp_err_to_name(ret));
                return false;
            }

            // Also start HTTP redirect server on port 80
            start_http_redirect_server();
        } else {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.server_port = port;
            config.ctrl_port = port + 1;
            config.max_uri_handlers = 40;
            config.stack_size = 8192;
            config.lru_purge_enable = true;
            config.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard matching

            esp_err_t ret = httpd_start(&server_, &config);
            if (ret != ESP_OK) {
                ESP_LOGE("HTTP", "Failed to start HTTP server: %s", esp_err_to_name(ret));
                return false;
            }
        }

        // Register URI handlers
        register_handlers();

        running_ = true;
        ESP_LOGI("HTTP", "Server started successfully");
        return true;
    }

    /**
     * Stop HTTPS server
     */
    void stop() {
        if (!running_) return;

        if (http_redirect_server_) {
            httpd_stop(http_redirect_server_);
            http_redirect_server_ = nullptr;
        }
        if (server_) {
            httpd_stop(server_);
            server_ = nullptr;
        }
        running_ = false;
        ESP_LOGI("HTTP", "Server stopped");
    }

    /**
     * Check if server is running
     */
    bool is_running() const { return running_; }

    /**
     * Check if SSL certificate is ready
     */
    bool is_cert_ready() const { return https_ready_; }

    /**
     * Check if certificate generation is in progress
     */
    bool is_cert_generating() const { return cert_gen_in_progress_; }

    /**
     * Get certificate status string for API
     */
    const char* get_cert_status() const {
        if (cert_gen_in_progress_) return "generating";
        if (https_ready_) return "ready";
        return "not_started";
    }
#endif

    /**
     * Check if device has been provisioned (password set)
     */
    bool is_provisioned() const { return provisioned_; }

    /**
     * Set admin password (hashes before storing)
     * @param password Plain text password
     * @return true if password meets requirements and was set
     */
    bool set_admin_password(const char* password) {
        if (!password) return false;

        // Validate password strength
        if (!security::validate_password_strength(password)) {
            return false;
        }

        // Hash password
        if (!security::hash_password(password, PASSWORD_SALT, admin_password_hash_)) {
            return false;
        }

        provisioned_ = true;

        // Save to NVS
        save_provisioning_state();

        return true;
    }

    /**
     * Check if password is set (for serial console)
     */
    bool has_password() const {
        return admin_password_hash_[0] != '\0';
    }

    /**
     * Reset to factory defaults (clear password, require re-provisioning)
     */
    void factory_reset() {
        memset(admin_password_hash_, 0, sizeof(admin_password_hash_));
        provisioned_ = false;
        logout();

        if (storage_) {
            storage_->write_string("http:prov", "0");  // NVS key max 15 chars
            storage_->write_string("http:pw_hash", "");
        }
    }

    /**
     * Check if token is valid (uses constant-time comparison)
     */
    bool is_authenticated(const char* token) {
        if (!token || !session_.valid) return false;

        // Use constant-time comparison to prevent timing attacks
        if (!security::secure_strcmp(token, session_.token)) return false;

        uint32_t now = time_->millis();
        if (now - session_.last_activity > SESSION_TIMEOUT_MS) {
            session_.valid = false;
#ifdef ESP32_BUILD
            ESP_LOGI(HTTP_LOG_TAG, "Session expired due to inactivity");
#endif
            return false;
        }
        session_.last_activity = now;
        return true;
    }

    /**
     * Check if login is currently rate limited
     */
    bool is_rate_limited() const {
        if (rate_limit_.lockout_until == 0) return false;
        uint32_t now = time_->millis();
        return now < rate_limit_.lockout_until;
    }

    /**
     * Get remaining lockout time in seconds
     */
    uint32_t get_lockout_remaining() const {
        if (!is_rate_limited()) return 0;
        uint32_t now = time_->millis();
        return (rate_limit_.lockout_until - now) / 1000;
    }

    /**
     * Attempt login with rate limiting and secure verification
     * @param password Plain text password
     * @return session token on success, nullptr on failure
     */
    const char* login(const char* password) {
        uint32_t now = time_->millis();

        // Check rate limiting
        if (is_rate_limited()) {
#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Login blocked - rate limited");
#endif
            return nullptr;
        }

        // Device must be provisioned
        if (!provisioned_) {
            return nullptr;
        }

        // Verify password using constant-time comparison
        if (!security::verify_password(password, PASSWORD_SALT, admin_password_hash_)) {
            // Record failed attempt
            rate_limit_.failed_attempts++;
            rate_limit_.last_attempt_time = now;

#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Login failed - attempt %d/%d",
                     rate_limit_.failed_attempts, MAX_LOGIN_ATTEMPTS);
#endif

            // Apply exponential backoff
            if (rate_limit_.failed_attempts >= MAX_LOGIN_ATTEMPTS) {
                rate_limit_.lockout_until = now + LOGIN_LOCKOUT_MS;
#ifdef ESP32_BUILD
                ESP_LOGW(HTTP_LOG_TAG, "Account locked for %lu seconds",
                         (unsigned long)(LOGIN_LOCKOUT_MS / 1000));
#endif
            } else if (rate_limit_.failed_attempts > 3) {
                // Backoff: 1s, 2s, 4s, 8s, etc.
                uint32_t backoff = LOGIN_BACKOFF_BASE_MS << (rate_limit_.failed_attempts - 4);
                if (backoff > LOGIN_LOCKOUT_MS) backoff = LOGIN_LOCKOUT_MS;
                rate_limit_.lockout_until = now + backoff;
            }

            return nullptr;
        }

        // Success - reset rate limiting
        rate_limit_.failed_attempts = 0;
        rate_limit_.lockout_until = 0;

#ifdef ESP32_BUILD
        ESP_LOGI(HTTP_LOG_TAG, "Login successful from %s", current_client_ip_);
#endif

        // Generate cryptographic session token
        security::generate_session_token(session_.token);
        session_.last_activity = now;
        session_.created_at = now;
        session_.valid = true;

        return session_.token;
    }

    /**
     * Logout current session
     */
    void logout() {
        session_.valid = false;
        memset(session_.token, 0, sizeof(session_.token));
#ifdef ESP32_BUILD
        ESP_LOGI(HTTP_LOG_TAG, "User logged out");
#endif
    }

    /**
     * Process REST API request
     * @param method HTTP method (GET, POST, PUT, DELETE)
     * @param path Request path
     * @param body Request body (for POST/PUT)
     * @param token Auth token (from header/cookie)
     * @param response Output buffer for response
     * @param response_size Size of response buffer
     * @return HTTP status code
     */
    int handle_request(const char* method, const char* path, const char* body,
                       const char* token, char* response, size_t response_size) {
        // Setup endpoint (for first-boot provisioning) - always accessible
        if (strcmp(path, "/api/setup") == 0) {
            if (strcmp(method, "GET") == 0) {
                // Return provisioning status
                snprintf(response, response_size,
                    "{\"provisioned\":%s,\"password_requirements\":{\"min_length\":%zu,\"categories\":2}}",
                    provisioned_ ? "true" : "false",
                    security::MIN_PASSWORD_LENGTH);
                return 200;
            } else if (strcmp(method, "POST") == 0) {
                return handle_setup(body, response, response_size);
            }
        }

        // Login endpoint - handles rate limiting internally
        if (strcmp(path, "/api/login") == 0 && strcmp(method, "POST") == 0) {
            return handle_login(body, response, response_size);
        }

        // Health check (no auth, minimal info)
        if (strcmp(path, "/api/health") == 0 && strcmp(method, "GET") == 0) {
            snprintf(response, response_size, "{\"status\":\"ok\",\"provisioned\":%s}",
                     provisioned_ ? "true" : "false");
            return 200;
        }

        // SSL certificate status (no auth)
        if (strcmp(path, "/api/ssl/status") == 0 && strcmp(method, "GET") == 0) {
#ifdef ESP32_BUILD
            snprintf(response, response_size,
                     "{\"cert_status\":\"%s\",\"https_enabled\":%s,\"https_port\":443}",
                     get_cert_status(),
                     (https_ready_ && server_) ? "true" : "false");
#else
            snprintf(response, response_size,
                     "{\"cert_status\":\"simulator\",\"https_enabled\":false}");
#endif
            return 200;
        }

        // If not provisioned, require setup first
        if (!provisioned_) {
            snprintf(response, response_size,
                "{\"error\":\"Device not provisioned\",\"setup_required\":true}");
            return 403;
        }

        // All other endpoints require auth
        if (!is_authenticated(token)) {
            snprintf(response, response_size, "{\"error\":\"Unauthorized\"}");
            return 401;
        }

        // Logout
        if (strcmp(path, "/api/logout") == 0 && strcmp(method, "POST") == 0) {
            logout();
            snprintf(response, response_size, "{\"success\":true}");
            return 200;
        }

        // Change password
        if (strcmp(path, "/api/password") == 0 && strcmp(method, "POST") == 0) {
            return handle_password_change(body, response, response_size);
        }

        // Factory reset
        if (strcmp(path, "/api/factory_reset") == 0 && strcmp(method, "POST") == 0) {
            return handle_factory_reset(response, response_size);
        }

        // Route to handlers
        if (strcmp(method, "GET") == 0) {
            return handle_get(path, response, response_size);
        } else if (strcmp(method, "POST") == 0) {
            return handle_post(path, body, response, response_size);
        }

        snprintf(response, response_size, "{\"error\":\"Not found\"}");
        return 404;
    }

    /**
     * Build current state as JSON for WebSocket broadcast
     */
    void build_state_json(char* buffer, size_t size) {
        auto& sys = state_->get_system_state();

        int offset = snprintf(buffer, size,
            "{\"type\":\"state\",\"timestamp\":%lu,"
            "\"system\":{\"uptime\":%lu,\"heap\":%lu,\"wifi_rssi\":%d,\"ntp_synced\":%s},"
            "\"sensors\":{",
            (unsigned long)time_->millis() / 1000,
            (unsigned long)sys.uptime_seconds,
            (unsigned long)sys.free_heap,
            sys.wifi_rssi,
            sys.ntp_synced ? "true" : "false");

        // Add sensors
        uint8_t sensor_count = state_->get_sensor_count();
        for (uint8_t i = 0; i < sensor_count && offset < (int)size - 100; i++) {
            auto* sensor = state_->get_sensor_by_id(i);
            if (sensor) {
                if (i > 0) offset += snprintf(buffer + offset, size - offset, ",");
                offset += snprintf(buffer + offset, size - offset,
                    "\"%s\":{\"value\":%.3f,\"quality\":\"%s\"}",
                    sensor->name, sensor->filtered_value,
                    quality_to_string(sensor->quality));
            }
        }

        offset += snprintf(buffer + offset, size - offset, "},\"relays\":{");

        // Add relays
        uint8_t relay_count = state_->get_relay_count();
        for (uint8_t i = 0; i < relay_count && offset < (int)size - 100; i++) {
            auto* relay = state_->get_relay_by_id(i);
            if (relay) {
                if (i > 0) offset += snprintf(buffer + offset, size - offset, ",");
                offset += snprintf(buffer + offset, size - offset,
                    "\"%s\":%s", relay->name, relay->state ? "true" : "false");
            }
        }

        offset += snprintf(buffer + offset, size - offset, "},\"fermenters\":{");

        // Add fermenters
        bool first_ferm = true;
        for (uint8_t i = 1; i <= core::MAX_FERMENTERS && offset < (int)size - 200; i++) {
            auto* ferm = state_->get_fermenter(i);
            if (ferm && ferm->id != 0) {
                if (!first_ferm) offset += snprintf(buffer + offset, size - offset, ",");
                first_ferm = false;
                offset += snprintf(buffer + offset, size - offset,
                    "\"F%d\":{\"temp\":%.2f,\"setpoint\":%.2f,\"pressure\":%.3f,"
                    "\"mode\":\"%s\",\"pid_output\":%.1f,\"plan_active\":%s,"
                    "\"current_step\":%d,\"hours_remaining\":%.1f}",
                    i, ferm->current_temp, ferm->target_temp, ferm->current_pressure,
                    mode_to_string(ferm->mode), ferm->pid_output,
                    ferm->plan_active ? "true" : "false",
                    ferm->current_step, ferm->hours_remaining);
            }
        }

        offset += snprintf(buffer + offset, size - offset, "},\"alarms\":[");

        // Add alarms
        bool first_alarm = true;
        if (safety_) {
            for (uint8_t i = 1; i <= core::MAX_FERMENTERS && offset < (int)size - 100; i++) {
                auto* alarm = safety_->get_alarm_state(i);
                if (alarm && (alarm->temp_high_alarm || alarm->temp_low_alarm ||
                              alarm->pressure_high_alarm || alarm->sensor_failure_alarm)) {
                    if (!first_alarm) offset += snprintf(buffer + offset, size - offset, ",");
                    first_alarm = false;
                    offset += snprintf(buffer + offset, size - offset,
                        "{\"fermenter\":%d,\"temp_high\":%s,\"temp_low\":%s,"
                        "\"pressure_high\":%s,\"sensor_failure\":%s}",
                        i, alarm->temp_high_alarm ? "true" : "false",
                        alarm->temp_low_alarm ? "true" : "false",
                        alarm->pressure_high_alarm ? "true" : "false",
                        alarm->sensor_failure_alarm ? "true" : "false");
                }
            }
        }

        snprintf(buffer + offset, size - offset, "]}");
    }

private:
    hal::ITimeInterface* time_;
    core::StateManager* state_;
    core::EventBus* events_;
    core::SystemConfig* config_;
    SafetyController* safety_;
    FermentationPlanManager* plans_;
    hal::IModbusInterface* modbus_;
    hal::IStorageInterface* storage_;
    hal::IGPIOInterface* gpio_;
#ifdef WIFI_NTP_ENABLED
    WifiProvisioning* wifi_prov_;
#endif
#ifdef CAN_ENABLED
    void* can_module_;
#endif

    char admin_password_hash_[security::PASSWORD_HASH_BUF_SIZE];
    Session session_;
    RateLimitState rate_limit_;
    bool running_;
    bool provisioned_;
    char current_client_ip_[16];  // Temporarily stores client IP during request processing

    /**
     * Load provisioning state from NVS
     */
    void load_provisioning_state() {
        if (!storage_) {
            provisioned_ = false;
            return;
        }

        char provisioned_str[8] = {0};
        if (storage_->read_string("http:prov", provisioned_str, sizeof(provisioned_str))) {
            provisioned_ = (strcmp(provisioned_str, "1") == 0);
        }

        if (provisioned_) {
            // Load password hash
            if (!storage_->read_string("http:pw_hash", admin_password_hash_, sizeof(admin_password_hash_))) {
                // Hash corrupted, reset provisioning
                provisioned_ = false;
            }
        }
    }

    /**
     * Save provisioning state to NVS
     */
    void save_provisioning_state() {
        if (!storage_) return;

        storage_->write_string("http:prov", provisioned_ ? "1" : "0");
        if (provisioned_) {
            storage_->write_string("http:pw_hash", admin_password_hash_);
        }
    }

#ifdef ESP32_BUILD
    httpd_handle_t server_ = nullptr;
    httpd_handle_t http_redirect_server_ = nullptr;  // HTTP->HTTPS redirect server
    TaskHandle_t cert_gen_task_ = nullptr;           // Background cert generation task
    bool https_ready_ = false;                       // HTTPS server ready flag
    bool cert_gen_in_progress_ = false;              // Certificate generation in progress

    // Self-signed certificate (embedded)
    static constexpr size_t CERT_MAX_SIZE = 2048;
    uint8_t server_cert_[CERT_MAX_SIZE];
    size_t server_cert_len_ = 0;
    uint8_t server_key_[CERT_MAX_SIZE];
    size_t server_key_len_ = 0;

    // Static instance for handlers
    static HttpServer* instance_;

    /**
     * Load certificate from NVS or generate unique per-device self-signed cert
     */
    bool load_or_generate_certificate() {
        // Try to load existing certificate from NVS
        if (storage_) {
            char cert_buf[CERT_MAX_SIZE];
            char key_buf[CERT_MAX_SIZE];
            if (storage_->read_string("ssl:cert", cert_buf, sizeof(cert_buf)) &&
                storage_->read_string("ssl:key", key_buf, sizeof(key_buf)) &&
                strlen(cert_buf) > 100 && strlen(key_buf) > 100) {  // Sanity check
                memcpy(server_cert_, cert_buf, strlen(cert_buf) + 1);
                server_cert_len_ = strlen(cert_buf) + 1;
                memcpy(server_key_, key_buf, strlen(key_buf) + 1);
                server_key_len_ = strlen(key_buf) + 1;

                char serial[18];
                security::get_device_serial(serial, sizeof(serial));
                ESP_LOGI("HTTP", "Loaded SSL certificate from NVS (device: %s)", serial);
                return true;
            }
        }

        // Generate new unique certificate for this device
        ESP_LOGI("HTTP", "No certificate found - generating unique per-device certificate...");
        ESP_LOGW("HTTP", "This may take 30-60 seconds on first boot");

        char cert_pem[security::CERT_PEM_MAX_SIZE];
        char key_pem[security::KEY_PEM_MAX_SIZE];
        size_t cert_len = 0, key_len = 0;

        security::CertGeneratorResult result = security::generate_self_signed_cert(
            cert_pem, sizeof(cert_pem), &cert_len,
            key_pem, sizeof(key_pem), &key_len);

        if (!result.success) {
            ESP_LOGE("HTTP", "Certificate generation failed: %s", result.error_msg);
            return false;
        }

        // Copy to server buffers
        if (cert_len > CERT_MAX_SIZE || key_len > CERT_MAX_SIZE) {
            ESP_LOGE("HTTP", "Generated certificate too large");
            return false;
        }

        memcpy(server_cert_, cert_pem, cert_len);
        server_cert_len_ = cert_len;
        memcpy(server_key_, key_pem, key_len);
        server_key_len_ = key_len;

        // Save to NVS for persistence
        if (storage_) {
            if (!storage_->write_string("ssl:cert", cert_pem)) {
                ESP_LOGW("HTTP", "Failed to save certificate to NVS");
            }
            if (!storage_->write_string("ssl:key", key_pem)) {
                ESP_LOGW("HTTP", "Failed to save private key to NVS");
            } else {
                ESP_LOGI("HTTP", "Certificate saved to NVS for future boots");
            }
        }

        char serial[18];
        security::get_device_serial(serial, sizeof(serial));
        ESP_LOGI("HTTP", "Generated unique SSL certificate for device %s (valid %d years)",
                 serial, security::CERT_VALIDITY_YEARS);
        return true;
    }

    /**
     * Background task for certificate generation
     * Runs with lower priority to allow watchdog/IDLE tasks to run
     */
    static void cert_gen_task(void* param) {
        HttpServer* self = static_cast<HttpServer*>(param);
        if (!self) {
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGI("HTTP", "Background certificate generation starting...");
        self->cert_gen_in_progress_ = true;

        // Yield immediately to let other tasks run
        vTaskDelay(pdMS_TO_TICKS(100));

        bool success = self->load_or_generate_certificate();

        if (success) {
            self->https_ready_ = true;  // Certificate is ready
            ESP_LOGI("HTTP", "===========================================");
            ESP_LOGI("HTTP", "SSL Certificate ready! (cert_len=%d, key_len=%d)",
                     (int)self->server_cert_len_, (int)self->server_key_len_);
            ESP_LOGI("HTTP", "===========================================");

            // Start HTTPS server and convert HTTP to redirect-only
            self->start_https_server_internal();
        } else {
            ESP_LOGE("HTTP", "Certificate generation failed");
        }

        self->cert_gen_in_progress_ = false;
        self->cert_gen_task_ = nullptr;
        vTaskDelete(NULL);
    }

    /**
     * Start HTTPS server after certificate is ready
     * Called from background task after cert generation completes
     */
    void start_https_server_internal() {
        if (server_) {
            ESP_LOGW("HTTP", "HTTPS server already running");
            return;
        }

        ESP_LOGI("HTTP", "Starting HTTPS server on port 443...");

        httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
        config.httpd.server_port = 443;
        config.httpd.ctrl_port = 32769;  // Different from HTTP
        config.httpd.max_uri_handlers = 40;
        config.httpd.stack_size = 8192;
        config.httpd.lru_purge_enable = true;
        config.httpd.uri_match_fn = httpd_uri_match_wildcard;

        // Use generated certificate
        config.servercert = server_cert_;
        config.servercert_len = server_cert_len_;
        config.prvtkey_pem = server_key_;
        config.prvtkey_len = server_key_len_;

        esp_err_t ret = httpd_ssl_start(&server_, &config);
        if (ret != ESP_OK) {
            ESP_LOGE("HTTP", "Failed to start HTTPS server: %s", esp_err_to_name(ret));
            return;
        }

        // Register all handlers on HTTPS server
        register_handlers_on_server(server_);

        ESP_LOGI("HTTP", "HTTPS server started on port 443");

        // Convert HTTP server on port 80 to redirect-only
        convert_http_to_redirect();
    }

    /**
     * Convert HTTP server from full service to redirect-only
     */
    void convert_http_to_redirect() {
        if (!http_redirect_server_) {
            ESP_LOGW("HTTP", "HTTP server not running");
            return;
        }

        ESP_LOGI("HTTP", "Converting HTTP (port 80) to HTTPS redirect...");

        // Stop existing HTTP server
        httpd_stop(http_redirect_server_);
        http_redirect_server_ = nullptr;

        // Restart as redirect-only server
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.ctrl_port = 32768;
        config.max_uri_handlers = 4;
        config.stack_size = 4096;  // Smaller stack for redirects
        config.uri_match_fn = httpd_uri_match_wildcard;

        esp_err_t ret = httpd_start(&http_redirect_server_, &config);
        if (ret != ESP_OK) {
            ESP_LOGW("HTTP", "HTTP redirect server failed: %s", esp_err_to_name(ret));
            return;
        }

        // Register only redirect handlers
        httpd_uri_t redirect_get = { .uri = "/*", .method = HTTP_GET, .handler = http_redirect_handler, .user_ctx = this };
        httpd_uri_t redirect_post = { .uri = "/*", .method = HTTP_POST, .handler = http_redirect_handler, .user_ctx = this };
        httpd_uri_t redirect_put = { .uri = "/*", .method = HTTP_PUT, .handler = http_redirect_handler, .user_ctx = this };
        httpd_register_uri_handler(http_redirect_server_, &redirect_get);
        httpd_register_uri_handler(http_redirect_server_, &redirect_post);
        httpd_register_uri_handler(http_redirect_server_, &redirect_put);

        ESP_LOGI("HTTP", "HTTP->HTTPS redirect enabled on port 80");
    }

public:
    /**
     * Start HTTP server immediately with HTTPS starting in background
     * HTTP serves content while certificate is generated, then HTTPS takes over
     * @param http_port HTTP port (default 80)
     * @return true on success
     */
    bool start_with_background_https(uint16_t http_port = 80) {
        if (running_) return true;

        ESP_LOGI("HTTP", "Starting HTTP server on port %d (HTTPS will start in background)", http_port);

        // Mount SPIFFS first
        esp_vfs_spiffs_conf_t spiffs_conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false
        };
        esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
        if (spiffs_ret != ESP_OK && spiffs_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW("HTTP", "SPIFFS init: %s", esp_err_to_name(spiffs_ret));
        } else {
            size_t total = 0, used = 0;
            esp_spiffs_info(NULL, &total, &used);
            ESP_LOGI("HTTP", "SPIFFS mounted: %d/%d bytes used", used, total);
        }

        // Start HTTP server on port 80 immediately
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = http_port;
        config.ctrl_port = http_port + 1;
        config.max_uri_handlers = 40;
        config.stack_size = 8192;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;

        esp_err_t ret = httpd_start(&http_redirect_server_, &config);
        if (ret != ESP_OK) {
            ESP_LOGE("HTTP", "Failed to start HTTP server: %s", esp_err_to_name(ret));
            return false;
        }

        // Register handlers on HTTP server (will serve content until HTTPS is ready)
        instance_ = this;
        register_handlers_on_server(http_redirect_server_);

        running_ = true;
        ESP_LOGI("HTTP", "HTTP server started on port %d", http_port);

        // Start background certificate generation task
        // Use low priority (1) so IDLE task (priority 0) can still run for watchdog
        // Stack needs to be large enough for SSL server startup (16KB)
        BaseType_t task_ret = xTaskCreate(
            cert_gen_task,
            "cert_gen",
            16384,  // Large stack for SSL initialization
            this,
            1,      // Low priority
            &cert_gen_task_
        );

        if (task_ret != pdPASS) {
            ESP_LOGW("HTTP", "Failed to create cert gen task, HTTPS disabled");
        } else {
            ESP_LOGI("HTTP", "Background certificate generation started");
        }

        return true;
    }

    /**
     * Register handlers on a specific server handle
     */
    void register_handlers_on_server(httpd_handle_t server) {
        // Static file handlers
        httpd_uri_t static_root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_admin = {
            .uri = "/admin",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_admin_slash = {
            .uri = "/admin/",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_handler = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };

        // API handlers
        httpd_uri_t api_setup_get = { .uri = "/api/setup", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_setup_post = { .uri = "/api/setup", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_login = { .uri = "/api/login", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_logout = { .uri = "/api/logout", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_password = { .uri = "/api/password", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_factory_reset = { .uri = "/api/factory_reset", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_health = { .uri = "/api/health", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_ssl_status = { .uri = "/api/ssl/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_status = { .uri = "/api/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensors = { .uri = "/api/sensors", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_relays = { .uri = "/api/relays", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenters = { .uri = "/api/fermenters", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_alarms = { .uri = "/api/alarms", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_modbus = { .uri = "/api/modbus/stats", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_inputs = { .uri = "/api/inputs", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_outputs = { .uri = "/api/outputs", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_config = { .uri = "/api/config", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_modules = { .uri = "/api/modules", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_can = { .uri = "/api/can/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_cpu_history = { .uri = "/api/cpu/history", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_network_history = { .uri = "/api/network/history", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_wifi_summary = { .uri = "/api/wifi/summary", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_reboot = { .uri = "/api/reboot", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };

        // Wildcard handlers
        httpd_uri_t api_relay_set = { .uri = "/api/relay/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_output_set = { .uri = "/api/output/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenter_get = { .uri = "/api/fermenter/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenter_set = { .uri = "/api/fermenter/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_pid_get = { .uri = "/api/pid/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_pid_set = { .uri = "/api/pid/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensor_get = { .uri = "/api/sensor/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensor_config = { .uri = "/api/sensor/*/config", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };

        // Register API handlers first (more specific)
        httpd_register_uri_handler(server, &api_setup_get);
        httpd_register_uri_handler(server, &api_setup_post);
        httpd_register_uri_handler(server, &api_login);
        httpd_register_uri_handler(server, &api_logout);
        httpd_register_uri_handler(server, &api_password);
        httpd_register_uri_handler(server, &api_factory_reset);
        httpd_register_uri_handler(server, &api_health);
        httpd_register_uri_handler(server, &api_ssl_status);
        httpd_register_uri_handler(server, &api_status);
        httpd_register_uri_handler(server, &api_sensors);
        httpd_register_uri_handler(server, &api_relays);
        httpd_register_uri_handler(server, &api_fermenters);
        httpd_register_uri_handler(server, &api_alarms);
        httpd_register_uri_handler(server, &api_modbus);
        httpd_register_uri_handler(server, &api_inputs);
        httpd_register_uri_handler(server, &api_outputs);
        httpd_register_uri_handler(server, &api_config);
        httpd_register_uri_handler(server, &api_modules);
        httpd_register_uri_handler(server, &api_can);
        httpd_register_uri_handler(server, &api_cpu_history);
        httpd_register_uri_handler(server, &api_network_history);
        httpd_register_uri_handler(server, &api_wifi_summary);
        httpd_register_uri_handler(server, &api_reboot);
        httpd_register_uri_handler(server, &api_relay_set);
        httpd_register_uri_handler(server, &api_output_set);
        httpd_register_uri_handler(server, &api_fermenter_get);
        httpd_register_uri_handler(server, &api_fermenter_set);
        httpd_register_uri_handler(server, &api_pid_get);
        httpd_register_uri_handler(server, &api_pid_set);
        httpd_register_uri_handler(server, &api_sensor_get);
        httpd_register_uri_handler(server, &api_sensor_config);

        // Static handlers last
        httpd_register_uri_handler(server, &static_root);
        httpd_register_uri_handler(server, &static_admin);
        httpd_register_uri_handler(server, &static_admin_slash);
        httpd_register_uri_handler(server, &static_handler);
    }

    /**
     * Register all URI handlers
     */
    void register_handlers() {
        instance_ = this;

        // Static file handlers (must be last - catches all non-API paths)
        httpd_uri_t static_root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_admin = {
            .uri = "/admin",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_admin_slash = {
            .uri = "/admin/",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };
        httpd_uri_t static_handler = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = static_file_handler,
            .user_ctx = this
        };

        // API handlers
        httpd_uri_t api_setup_get = { .uri = "/api/setup", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_setup_post = { .uri = "/api/setup", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_login = { .uri = "/api/login", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_logout = { .uri = "/api/logout", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_password = { .uri = "/api/password", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_factory_reset = { .uri = "/api/factory_reset", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_health = { .uri = "/api/health", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_ssl_status = { .uri = "/api/ssl/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_status = { .uri = "/api/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensors = { .uri = "/api/sensors", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_relays = { .uri = "/api/relays", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenters = { .uri = "/api/fermenters", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_alarms = { .uri = "/api/alarms", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_modbus = { .uri = "/api/modbus/stats", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_inputs = { .uri = "/api/inputs", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_outputs = { .uri = "/api/outputs", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_config = { .uri = "/api/config", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_modules = { .uri = "/api/modules", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_can = { .uri = "/api/can/status", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_cpu_history = { .uri = "/api/cpu/history", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_network_history = { .uri = "/api/network/history", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_wifi_summary = { .uri = "/api/wifi/summary", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_reboot = { .uri = "/api/reboot", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };

        // Wildcard handlers for dynamic paths
        httpd_uri_t api_relay_set = { .uri = "/api/relay/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_output_set = { .uri = "/api/output/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenter_get = { .uri = "/api/fermenter/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_fermenter_set = { .uri = "/api/fermenter/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_pid_get = { .uri = "/api/pid/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_pid_set = { .uri = "/api/pid/*", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensor_get = { .uri = "/api/sensor/*", .method = HTTP_GET, .handler = api_handler, .user_ctx = this };
        httpd_uri_t api_sensor_config = { .uri = "/api/sensor/*/config", .method = HTTP_POST, .handler = api_handler, .user_ctx = this };

        // Register in order (specific first, then wildcards)
        httpd_register_uri_handler(server_, &api_setup_get);
        httpd_register_uri_handler(server_, &api_setup_post);
        httpd_register_uri_handler(server_, &api_login);
        httpd_register_uri_handler(server_, &api_logout);
        httpd_register_uri_handler(server_, &api_password);
        httpd_register_uri_handler(server_, &api_factory_reset);
        httpd_register_uri_handler(server_, &api_health);
        httpd_register_uri_handler(server_, &api_ssl_status);
        httpd_register_uri_handler(server_, &api_status);
        httpd_register_uri_handler(server_, &api_sensors);
        httpd_register_uri_handler(server_, &api_relays);
        httpd_register_uri_handler(server_, &api_fermenters);
        httpd_register_uri_handler(server_, &api_alarms);
        httpd_register_uri_handler(server_, &api_modbus);
        httpd_register_uri_handler(server_, &api_inputs);
        httpd_register_uri_handler(server_, &api_outputs);
        httpd_register_uri_handler(server_, &api_config);
        httpd_register_uri_handler(server_, &api_modules);
        httpd_register_uri_handler(server_, &api_can);
        httpd_register_uri_handler(server_, &api_cpu_history);
        httpd_register_uri_handler(server_, &api_network_history);
        httpd_register_uri_handler(server_, &api_wifi_summary);
        httpd_register_uri_handler(server_, &api_reboot);
        httpd_register_uri_handler(server_, &api_relay_set);
        httpd_register_uri_handler(server_, &api_output_set);
        httpd_register_uri_handler(server_, &api_fermenter_get);
        httpd_register_uri_handler(server_, &api_fermenter_set);
        httpd_register_uri_handler(server_, &api_pid_get);
        httpd_register_uri_handler(server_, &api_pid_set);
        httpd_register_uri_handler(server_, &api_sensor_get);
        httpd_register_uri_handler(server_, &api_sensor_config);

        // Static file handlers (register last so API routes take priority)
        httpd_register_uri_handler(server_, &static_root);
        httpd_register_uri_handler(server_, &static_admin);
        httpd_register_uri_handler(server_, &static_admin_slash);
        httpd_register_uri_handler(server_, &static_handler);
    }

    /**
     * API request handler
     */
    static esp_err_t api_handler(httpd_req_t* req) {
        HttpServer* self = static_cast<HttpServer*>(req->user_ctx);
        if (!self) return ESP_FAIL;

        // Get client IP address
        int sockfd = httpd_req_to_sockfd(req);
        struct sockaddr_in6 addr;
        socklen_t addr_len = sizeof(addr);
        if (getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) == 0) {
            // Handle IPv4-mapped IPv6 addresses
            if (addr.sin6_family == AF_INET6) {
                // Check if it's an IPv4-mapped address (::ffff:x.x.x.x)
                uint8_t* bytes = addr.sin6_addr.s6_addr;
                if (bytes[10] == 0xff && bytes[11] == 0xff) {
                    // IPv4-mapped, extract IPv4 address from last 4 bytes
                    snprintf(self->current_client_ip_, sizeof(self->current_client_ip_),
                             "%d.%d.%d.%d", bytes[12], bytes[13], bytes[14], bytes[15]);
                } else {
                    // Pure IPv6
                    inet_ntop(AF_INET6, &addr.sin6_addr, self->current_client_ip_, sizeof(self->current_client_ip_));
                }
            } else {
                // IPv4
                struct sockaddr_in* addr4 = (struct sockaddr_in*)&addr;
                inet_ntop(AF_INET, &addr4->sin_addr, self->current_client_ip_, sizeof(self->current_client_ip_));
            }
        } else {
            strncpy(self->current_client_ip_, "unknown", sizeof(self->current_client_ip_));
        }

        // Get method string
        const char* method = "GET";
        if (req->method == HTTP_POST) method = "POST";
        else if (req->method == HTTP_PUT) method = "PUT";
        else if (req->method == HTTP_DELETE) method = "DELETE";

        // Get auth token from header
        char auth_header[128] = {0};
        httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header));
        const char* token = nullptr;
        if (strncmp(auth_header, "Bearer ", 7) == 0) {
            token = auth_header + 7;
        }

        // Read body for POST/PUT
        char body[1024] = {0};
        if (req->method == HTTP_POST || req->method == HTTP_PUT) {
            int ret = httpd_req_recv(req, body, sizeof(body) - 1);
            if (ret > 0) body[ret] = '\0';
        }

        // Process request
        char response[MAX_RESPONSE_SIZE];
        int status = self->handle_request(method, req->uri, body, token, response, sizeof(response));

        // Set response headers
        httpd_resp_set_type(req, "application/json");
        // CORS: No wildcard - same-origin policy by default
        // Only allow requests from the same origin for security
        // If cross-origin access is needed, configure specific allowed origins
        httpd_resp_set_status(req, status == 200 ? "200 OK" :
                                   status == 400 ? "400 Bad Request" :
                                   status == 401 ? "401 Unauthorized" :
                                   status == 403 ? "403 Forbidden" :
                                   status == 404 ? "404 Not Found" :
                                   status == 429 ? "429 Too Many Requests" : "500 Internal Server Error");

        // Send response
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }

    /**
     * Send styled 404 page
     * Matches the dark theme of the admin interface
     */
    static void send_styled_404(httpd_req_t* req) {
        static const char* NOT_FOUND_HTML =
            "<!DOCTYPE html>"
            "<html lang=\"en\">"
            "<head>"
            "<meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
            "<title>404 - Not Found</title>"
            "<style>"
            "body{background:#1a1d21;color:#e9ecef;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,\"Helvetica Neue\",Arial,sans-serif;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;}"
            ".container{text-align:center;max-width:400px;padding:2rem;}"
            ".card{background:#212529;border:1px solid #343a40;border-radius:0.375rem;padding:2rem;}"
            "h1{font-size:4rem;margin:0;color:#ffc107;}"
            "h2{color:#adb5bd;font-weight:normal;margin:1rem 0;}"
            "p{color:#6c757d;margin-bottom:1.5rem;}"
            "a{display:inline-block;background:#0d6efd;color:#fff;padding:0.5rem 1.5rem;border-radius:0.375rem;text-decoration:none;transition:background 0.15s;}"
            "a:hover{background:#0b5ed7;}"
            "</style>"
            "</head>"
            "<body>"
            "<div class=\"container\">"
            "<div class=\"card\">"
            "<h1>Oops!</h1>"
            "<h2>404 - Page Not Found</h2>"
            "<p>The page you're looking for doesn't exist or has been moved.</p>"
            "<a href=\"/admin/\">Back to Dashboard</a>"
            "</div>"
            "</div>"
            "</body>"
            "</html>";

        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, NOT_FOUND_HTML, strlen(NOT_FOUND_HTML));
    }

    /**
     * Static file handler - serves files from SPIFFS
     * Includes path traversal protection
     */
    static esp_err_t static_file_handler(httpd_req_t* req) {
        char filepath[256];

        // Limit URI length to prevent buffer overflow
        size_t uri_len = strlen(req->uri);
        if (uri_len > 200) {
            httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
            return ESP_OK;
        }

        // PATH TRAVERSAL PROTECTION: Reject paths with ".." or "//"
        if (strstr(req->uri, "..") != nullptr) {
            ESP_LOGW("HTTP", "Path traversal attempt blocked: %s", req->uri);
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid path");
            return ESP_OK;
        }
        if (strstr(req->uri, "//") != nullptr) {
            ESP_LOGW("HTTP", "Double slash in path blocked: %s", req->uri);
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid path");
            return ESP_OK;
        }

        // Reject paths with null bytes (injection attempt)
        for (size_t i = 0; i < uri_len; i++) {
            if (req->uri[i] == '\0') break;
            // Also reject backslash (Windows path traversal)
            if (req->uri[i] == '\\') {
                ESP_LOGW("HTTP", "Backslash in path blocked: %s", req->uri);
                httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid path");
                return ESP_OK;
            }
        }

        // Redirect root to /admin/
        if (strcmp(req->uri, "/") == 0) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/admin/");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }

        // Default to index.html for admin paths
        if (strcmp(req->uri, "/admin") == 0 || strcmp(req->uri, "/admin/") == 0) {
            snprintf(filepath, sizeof(filepath), "/spiffs/admin/index.html");
        } else {
            snprintf(filepath, sizeof(filepath), "/spiffs%.200s", req->uri);
        }

        // Final validation: ensure path is within /spiffs
        if (!security::path_within_base(filepath, "/spiffs")) {
            ESP_LOGW("HTTP", "Path escaped base directory: %s", filepath);
            httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access denied");
            return ESP_OK;
        }

        // Open file
        FILE* f = fopen(filepath, "r");
        if (!f) {
            // Try with index.html appended (also validate)
            char index_path[256];
            snprintf(index_path, sizeof(index_path), "/spiffs%.180s/index.html", req->uri);
            if (security::path_within_base(index_path, "/spiffs")) {
                f = fopen(index_path, "r");
                if (f) {
                    strncpy(filepath, index_path, sizeof(filepath) - 1);
                    filepath[sizeof(filepath) - 1] = '\0';
                }
            }
        }

        if (!f) {
            send_styled_404(req);
            return ESP_OK;
        }

        // Determine content type
        const char* content_type = "text/plain";
        if (strstr(filepath, ".html")) content_type = "text/html";
        else if (strstr(filepath, ".css")) content_type = "text/css";
        else if (strstr(filepath, ".js")) content_type = "application/javascript";
        else if (strstr(filepath, ".json")) content_type = "application/json";
        else if (strstr(filepath, ".png")) content_type = "image/png";
        else if (strstr(filepath, ".ico")) content_type = "image/x-icon";

        httpd_resp_set_type(req, content_type);

        // Stream file
        char chunk[1024];
        size_t read_bytes;
        while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            httpd_resp_send_chunk(req, chunk, read_bytes);
        }
        httpd_resp_send_chunk(req, NULL, 0);

        fclose(f);
        return ESP_OK;
    }

    /**
     * HTTP to HTTPS redirect handler
     * Redirects all HTTP requests to HTTPS
     */
    static esp_err_t http_redirect_handler(httpd_req_t* req) {
        // Get the host header to build redirect URL
        char host[64] = "192.168.0.139";  // Default fallback
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));

        // Remove port if present (e.g., "host:80" -> "host")
        char* port_sep = strchr(host, ':');
        if (port_sep) *port_sep = '\0';

        // Build redirect URL - limit URI to prevent buffer overflow
        char redirect_url[256];
        char uri_truncated[180];
        strncpy(uri_truncated, req->uri, sizeof(uri_truncated) - 1);
        uri_truncated[sizeof(uri_truncated) - 1] = '\0';
        snprintf(redirect_url, sizeof(redirect_url), "https://%s%s", host, uri_truncated);

        // Send 301 redirect
        httpd_resp_set_status(req, "301 Moved Permanently");
        httpd_resp_set_hdr(req, "Location", redirect_url);
        httpd_resp_send(req, NULL, 0);

        ESP_LOGI("HTTP", "Redirecting to HTTPS: %s", redirect_url);
        return ESP_OK;
    }

    /**
     * Start HTTP redirect server (port 80 -> HTTPS)
     */
    bool start_http_redirect_server() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.ctrl_port = 32768;  // Different control port than HTTPS
        config.max_uri_handlers = 2;
        config.stack_size = 4096;  // Smaller stack, just redirects
        config.uri_match_fn = httpd_uri_match_wildcard;

        esp_err_t ret = httpd_start(&http_redirect_server_, &config);
        if (ret != ESP_OK) {
            ESP_LOGW("HTTP", "HTTP redirect server failed to start: %s", esp_err_to_name(ret));
            return false;
        }

        // Register wildcard handler for all paths
        httpd_uri_t redirect_handler = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = http_redirect_handler,
            .user_ctx = this
        };
        httpd_register_uri_handler(http_redirect_server_, &redirect_handler);

        // Also handle POST, PUT, DELETE etc with redirect
        httpd_uri_t redirect_post = { .uri = "/*", .method = HTTP_POST, .handler = http_redirect_handler, .user_ctx = this };
        httpd_uri_t redirect_put = { .uri = "/*", .method = HTTP_PUT, .handler = http_redirect_handler, .user_ctx = this };
        httpd_register_uri_handler(http_redirect_server_, &redirect_post);
        httpd_register_uri_handler(http_redirect_server_, &redirect_put);

        ESP_LOGI("HTTP", "HTTP redirect server started on port 80");
        return true;
    }
#endif

    const char* quality_to_string(core::SensorQuality q) {
        switch (q) {
            case core::SensorQuality::GOOD: return "GOOD";
            case core::SensorQuality::BAD: return "BAD";
            case core::SensorQuality::SUSPECT: return "SUSPECT";
            case core::SensorQuality::WARMING_UP: return "WARMING_UP";
            default: return "UNKNOWN";
        }
    }

    const char* mode_to_string(core::FermenterMode m) {
        switch (m) {
            case core::FermenterMode::OFF: return "OFF";
            case core::FermenterMode::MANUAL: return "MANUAL";
            case core::FermenterMode::PLAN: return "PLAN";
            case core::FermenterMode::AUTOTUNE: return "AUTOTUNE";
            default: return "UNKNOWN";
        }
    }

    /**
     * Handle first-boot password setup
     */
    int handle_setup(const char* body, char* response, size_t response_size) {
        // If already provisioned, reject (use factory_reset first)
        if (provisioned_) {
#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Setup rejected - already provisioned");
#endif
            snprintf(response, response_size,
                "{\"error\":\"Device already provisioned. Use factory reset to change password.\"}");
            return 400;
        }

        // Parse password from body
        char password[security::MAX_PASSWORD_LENGTH + 1];
        if (!parse_json_string(body, "password", password, sizeof(password))) {
            snprintf(response, response_size, "{\"error\":\"Missing password field\"}");
            return 400;
        }

        // Validate and set password
        if (!set_admin_password(password)) {
#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Setup rejected - weak password");
#endif
            snprintf(response, response_size,
                "{\"error\":\"Password does not meet requirements\","
                "\"requirements\":{\"min_length\":%zu,\"categories\":2,"
                "\"description\":\"At least %zu characters with 2 of: uppercase, lowercase, digit\"}}",
                security::MIN_PASSWORD_LENGTH, security::MIN_PASSWORD_LENGTH);
            return 400;
        }

#ifdef ESP32_BUILD
        ESP_LOGI(HTTP_LOG_TAG, "Device provisioned successfully");
#endif
        snprintf(response, response_size,
            "{\"success\":true,\"message\":\"Device provisioned successfully\"}");
        return 200;
    }

    int handle_login(const char* body, char* response, size_t response_size) {
        // Check rate limiting first
        if (is_rate_limited()) {
            uint32_t remaining = get_lockout_remaining();
            snprintf(response, response_size,
                "{\"error\":\"Too many failed attempts\",\"retry_after\":%lu,\"locked\":true}",
                (unsigned long)remaining);
            return 429;  // Too Many Requests
        }

        // Check if device is provisioned
        if (!provisioned_) {
            snprintf(response, response_size,
                "{\"error\":\"Device not provisioned\",\"setup_required\":true}");
            return 403;
        }

        // Parse password from body
        char password[security::MAX_PASSWORD_LENGTH + 1];
        if (!parse_json_string(body, "password", password, sizeof(password))) {
            snprintf(response, response_size, "{\"error\":\"Missing password field\"}");
            return 400;
        }

        const char* token = login(password);
        if (token) {
            snprintf(response, response_size, "{\"success\":true,\"token\":\"%s\"}", token);
            return 200;
        } else {
            // Check if we're now rate limited after this attempt
            if (is_rate_limited()) {
                uint32_t remaining = get_lockout_remaining();
                snprintf(response, response_size,
                    "{\"error\":\"Invalid password\",\"retry_after\":%lu,\"locked\":true}",
                    (unsigned long)remaining);
                return 429;
            }
            snprintf(response, response_size,
                "{\"error\":\"Invalid password\",\"attempts_remaining\":%d}",
                MAX_LOGIN_ATTEMPTS - rate_limit_.failed_attempts);
            return 401;
        }
    }

    int handle_password_change(const char* body, char* response, size_t response_size) {
        // Parse current and new passwords
        char current_password[security::MAX_PASSWORD_LENGTH + 1];
        char new_password[security::MAX_PASSWORD_LENGTH + 1];

        if (!parse_json_string(body, "current_password", current_password, sizeof(current_password))) {
            snprintf(response, response_size, "{\"error\":\"Missing current_password field\"}");
            return 400;
        }

        if (!parse_json_string(body, "new_password", new_password, sizeof(new_password))) {
            snprintf(response, response_size, "{\"error\":\"Missing new_password field\"}");
            return 400;
        }

        // Verify current password
        if (!security::verify_password(current_password, PASSWORD_SALT, admin_password_hash_)) {
#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Password change failed - incorrect current password");
#endif
            snprintf(response, response_size, "{\"error\":\"Current password is incorrect\"}");
            return 401;
        }

        // Validate new password strength
        if (!security::validate_password_strength(new_password)) {
#ifdef ESP32_BUILD
            ESP_LOGW(HTTP_LOG_TAG, "Password change failed - weak new password");
#endif
            snprintf(response, response_size,
                "{\"error\":\"New password does not meet requirements\","
                "\"requirements\":{\"min_length\":%zu,\"categories\":2}}",
                security::MIN_PASSWORD_LENGTH);
            return 400;
        }

        // Set new password
        if (!set_admin_password(new_password)) {
            snprintf(response, response_size, "{\"error\":\"Failed to set new password\"}");
            return 500;
        }

#ifdef ESP32_BUILD
        ESP_LOGI(HTTP_LOG_TAG, "Password changed successfully");
#endif
        snprintf(response, response_size, "{\"success\":true,\"message\":\"Password changed successfully\"}");
        return 200;
    }

    int handle_factory_reset(char* response, size_t response_size) {
#ifdef ESP32_BUILD
        ESP_LOGW(HTTP_LOG_TAG, "Factory reset initiated");

        // Clear NVS
        nvs_handle_t nvs;
        if (nvs_open("http_auth", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_all(nvs);
            nvs_commit(nvs);
            nvs_close(nvs);
        }

        // Clear WiFi credentials
        nvs_handle_t wifi_nvs;
        if (nvs_open("wifi_creds", NVS_READWRITE, &wifi_nvs) == ESP_OK) {
            nvs_erase_all(wifi_nvs);
            nvs_commit(wifi_nvs);
            nvs_close(wifi_nvs);
        }

        snprintf(response, response_size, "{\"success\":true,\"message\":\"Factory reset complete. Rebooting...\"}");

        // Schedule reboot
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
#else
        // Simulator - just clear provisioned flag
        provisioned_ = false;
        memset(admin_password_hash_, 0, sizeof(admin_password_hash_));
        snprintf(response, response_size, "{\"success\":true,\"message\":\"Factory reset complete (simulator)\"}");
#endif
        return 200;
    }

    /**
     * Parse a string value from simple JSON
     * @param json JSON string to parse
     * @param key Key to find
     * @param value Output buffer
     * @param value_size Size of output buffer
     * @return true if found and parsed
     */
    bool parse_json_string(const char* json, const char* key, char* value, size_t value_size) {
        if (!json || !key || !value || value_size == 0) return false;

        // Build search pattern: "key"
        char pattern[64];
        snprintf(pattern, sizeof(pattern), "\"%s\"", key);

        const char* key_start = strstr(json, pattern);
        if (!key_start) return false;

        // Find colon
        const char* colon = strchr(key_start + strlen(pattern), ':');
        if (!colon) return false;

        // Find opening quote
        const char* value_start = strchr(colon, '"');
        if (!value_start) return false;
        value_start++;

        // Find closing quote
        const char* value_end = strchr(value_start, '"');
        if (!value_end) return false;

        // Copy value
        size_t len = value_end - value_start;
        if (len >= value_size) len = value_size - 1;

        memcpy(value, value_start, len);
        value[len] = '\0';

        return true;
    }

    int handle_get(const char* path, char* response, size_t response_size) {
        if (strcmp(path, "/api/status") == 0) {
            return api_status(response, response_size);
        } else if (strcmp(path, "/api/sensors") == 0) {
            return api_sensors(response, response_size);
        } else if (strncmp(path, "/api/sensor/", 12) == 0) {
            return api_sensor(path + 12, response, response_size);
        } else if (strcmp(path, "/api/relays") == 0) {
            return api_relays(response, response_size);
        } else if (strcmp(path, "/api/fermenters") == 0) {
            return api_fermenters(response, response_size);
        } else if (strncmp(path, "/api/fermenter/", 15) == 0) {
            return api_fermenter(path + 15, response, response_size);
        } else if (strncmp(path, "/api/pid/", 9) == 0) {
            return api_pid(path + 9, response, response_size);
        } else if (strcmp(path, "/api/alarms") == 0) {
            return api_alarms(response, response_size);
        } else if (strcmp(path, "/api/modbus/stats") == 0) {
            return api_modbus_stats(response, response_size);
        } else if (strcmp(path, "/api/inputs") == 0) {
            return api_inputs(response, response_size);
        } else if (strcmp(path, "/api/outputs") == 0) {
            return api_outputs(response, response_size);
        } else if (strcmp(path, "/api/config") == 0) {
            return api_config(response, response_size);
        } else if (strcmp(path, "/api/modules") == 0) {
            return api_modules(response, response_size);
        }
#ifdef WIFI_NTP_ENABLED
        else if (strcmp(path, "/api/wifi") == 0) {
            return api_wifi_status(response, response_size);
        }
#endif
#ifdef CAN_ENABLED
        else if (strcmp(path, "/api/can/status") == 0) {
            return api_can_status(response, response_size);
        }
#endif
        else if (strcmp(path, "/api/cpu/history") == 0) {
            return api_cpu_history(response, response_size);
        }
        else if (strcmp(path, "/api/network/history") == 0) {
            return api_network_history(response, response_size);
        }
        else if (strcmp(path, "/api/wifi/summary") == 0) {
            return api_wifi_summary(response, response_size);
        }

        snprintf(response, response_size, "{\"error\":\"Not found\"}");
        return 404;
    }

    int handle_post(const char* path, const char* body, char* response, size_t response_size) {
        if (strncmp(path, "/api/relay/", 11) == 0) {
            return api_relay_set(path + 11, body, response, response_size);
        } else if (strncmp(path, "/api/fermenter/", 15) == 0) {
            return api_fermenter_set(path + 15, body, response, response_size);
        } else if (strncmp(path, "/api/pid/", 9) == 0) {
            return api_pid_set(path + 9, body, response, response_size);
        } else if (strncmp(path, "/api/output/", 12) == 0) {
            return api_output_set(path + 12, body, response, response_size);
        } else if (strcmp(path, "/api/reboot") == 0) {
            return api_reboot(response, response_size);
        }

        snprintf(response, response_size, "{\"error\":\"Not found\"}");
        return 404;
    }

    // API Handlers
    int api_status(char* response, size_t response_size) {
        auto& sys = state_->get_system_state();

        uint32_t uptime_h = sys.uptime_seconds / 3600;
        uint32_t uptime_m = (sys.uptime_seconds % 3600) / 60;
        uint32_t uptime_s = sys.uptime_seconds % 60;

        // Build number from compile date
        const char* build_date = __DATE__;  // "Nov 24 2025"
        const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        int month = 1;
        for (int i = 0; i < 12; i++) {
            if (strncmp(build_date, months + i * 3, 3) == 0) {
                month = i + 1;
                break;
            }
        }
        int day = atoi(build_date + 4);
        int year = atoi(build_date + 7) % 100;
        char build_num[8];
        snprintf(build_num, sizeof(build_num), "%02d%02d%02d", year, month, day);

        // Get system time if NTP synced
        char time_str[32] = "N/A";
        char timezone_str[16] = "UTC";
#ifdef ESP32_BUILD
        if (sys.ntp_synced) {
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
            // Get timezone abbreviation using strftime %Z
            strftime(timezone_str, sizeof(timezone_str), "%Z", &timeinfo);
        }
#endif

        // Flash usage (approximate based on firmware size)
        uint32_t flash_used = 1015808;   // Will be updated at build time
        uint32_t flash_total = 4194304;  // 4MB OTA partition

        // Get hostname from WiFi MAC address
        char hostname[32] = "fermenter";
#ifdef ESP32_BUILD
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(hostname, sizeof(hostname), "fermenter-%02X%02X%02X.local",
                 mac[3], mac[4], mac[5]);
#endif

        snprintf(response, response_size,
            "{\"version\":\"%s\",\"build\":\"%s\",\"built\":\"%s %s\","
            "\"hostname\":\"%s\","
            "\"uptime\":\"%luh %lum %lus\",\"uptime_seconds\":%lu,"
            "\"free_heap\":%lu,\"cpu_usage\":%.1f,\"cpu_freq_mhz\":%lu,\"cpu_freq_max_mhz\":%lu,"
            "\"wifi_rssi\":%d,\"ntp_synced\":%s,"
            "\"sensor_count\":%d,\"fermenter_count\":%d,"
            "\"modbus_transactions\":%lu,\"modbus_errors\":%lu,"
            "\"system_time\":\"%s\",\"timezone\":\"%s\","
            "\"flash_used\":%lu,\"flash_total\":%lu}",
            FIRMWARE_VERSION, build_num, __DATE__, __TIME__,
            hostname,
            (unsigned long)uptime_h, (unsigned long)uptime_m, (unsigned long)uptime_s,
            (unsigned long)sys.uptime_seconds,
            (unsigned long)sys.free_heap, sys.cpu_usage,
            (unsigned long)sys.cpu_freq_mhz, (unsigned long)sys.cpu_freq_max_mhz,
            sys.wifi_rssi,
            sys.ntp_synced ? "true" : "false",
            state_->get_sensor_count(), state_->get_fermenter_count(),
            (unsigned long)sys.modbus_transactions, (unsigned long)sys.modbus_errors,
            time_str, timezone_str,
            (unsigned long)flash_used, (unsigned long)flash_total);

        return 200;
    }

    int api_sensors(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"sensors\":[");

        uint8_t count = state_->get_sensor_count();
        for (uint8_t i = 0; i < count && offset < (int)response_size - 150; i++) {
            auto* sensor = state_->get_sensor_by_id(i);
            if (sensor) {
                if (i > 0) offset += snprintf(response + offset, response_size - offset, ",");
                offset += snprintf(response + offset, response_size - offset,
                    "{\"name\":\"%s\",\"value\":%.3f,\"unit\":\"%s\",\"quality\":\"%s\"}",
                    sensor->name, sensor->filtered_value, sensor->unit,
                    quality_to_string(sensor->quality));
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_sensor(const char* name, char* response, size_t response_size) {
        auto* sensor = state_->get_sensor(name);
        if (!sensor) {
            snprintf(response, response_size, "{\"error\":\"Sensor not found\"}");
            return 404;
        }

        snprintf(response, response_size,
            "{\"name\":\"%s\",\"raw_value\":%.3f,\"filtered_value\":%.3f,"
            "\"display_value\":%.3f,\"unit\":\"%s\",\"quality\":\"%s\","
            "\"filter_type\":%d,\"alpha\":%.2f,\"scale\":%.6f,\"timestamp\":%lu}",
            sensor->name, sensor->raw_value, sensor->filtered_value,
            sensor->display_value, sensor->unit, quality_to_string(sensor->quality),
            (int)sensor->filter_type, sensor->alpha, sensor->scale,
            (unsigned long)sensor->timestamp);

        return 200;
    }

    int api_relays(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"relays\":[");

        uint8_t count = state_->get_relay_count();
        for (uint8_t i = 0; i < count && offset < (int)response_size - 100; i++) {
            auto* relay = state_->get_relay_by_id(i);
            if (relay) {
                if (i > 0) offset += snprintf(response + offset, response_size - offset, ",");
                offset += snprintf(response + offset, response_size - offset,
                    "{\"name\":\"%s\",\"state\":%s,\"last_change\":%lu}",
                    relay->name, relay->state ? "true" : "false",
                    (unsigned long)relay->last_change);
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_relay_set(const char* name, const char* body, char* response, size_t response_size) {
        // Find relay
        uint8_t count = state_->get_relay_count();
        core::RelayState* relay = nullptr;
        uint8_t relay_id = 0;

        for (uint8_t i = 0; i < count; i++) {
            auto* r = state_->get_relay_by_id(i);
            if (r && strcmp(r->name, name) == 0) {
                relay = r;
                relay_id = i;
                break;
            }
        }

        if (!relay) {
            snprintf(response, response_size, "{\"error\":\"Relay not found\"}");
            return 404;
        }

        // Parse state from body
        bool new_state = (strstr(body, "\"state\":true") != nullptr ||
                          strstr(body, "\"state\":1") != nullptr);

        state_->set_relay_state(relay_id, new_state, time_->millis());

        snprintf(response, response_size,
            "{\"success\":true,\"relay\":\"%s\",\"state\":%s}",
            name, new_state ? "true" : "false");
        return 200;
    }

    int api_fermenters(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"fermenters\":[");

        bool first = true;
        for (uint8_t i = 1; i <= core::MAX_FERMENTERS && offset < (int)response_size - 200; i++) {
            auto* ferm = state_->get_fermenter(i);
            if (ferm && ferm->id != 0) {
                if (!first) offset += snprintf(response + offset, response_size - offset, ",");
                first = false;
                offset += snprintf(response + offset, response_size - offset,
                    "{\"id\":%d,\"name\":\"%s\",\"temp\":%.2f,\"setpoint\":%.2f,"
                    "\"pressure\":%.3f,\"mode\":\"%s\",\"pid_output\":%.1f}",
                    i, ferm->name, ferm->current_temp, ferm->target_temp,
                    ferm->current_pressure, mode_to_string(ferm->mode), ferm->pid_output);
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_fermenter(const char* id_str, char* response, size_t response_size) {
        uint8_t id = atoi(id_str);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            snprintf(response, response_size, "{\"error\":\"Fermenter not found\"}");
            return 404;
        }

        snprintf(response, response_size,
            "{\"id\":%d,\"name\":\"%s\",\"temp\":%.2f,\"setpoint\":%.2f,"
            "\"pressure\":%.3f,\"target_pressure\":%.3f,\"mode\":\"%s\","
            "\"pid_output\":%.1f,\"plan_active\":%s,\"current_step\":%d,"
            "\"hours_remaining\":%.1f}",
            id, ferm->name, ferm->current_temp, ferm->target_temp,
            ferm->current_pressure, ferm->target_pressure,
            mode_to_string(ferm->mode), ferm->pid_output,
            ferm->plan_active ? "true" : "false",
            ferm->current_step, ferm->hours_remaining);

        return 200;
    }

    int api_fermenter_set(const char* id_str, const char* body, char* response, size_t response_size) {
        uint8_t id = atoi(id_str);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            snprintf(response, response_size, "{\"error\":\"Fermenter not found\"}");
            return 404;
        }

        // Parse setpoint
        const char* setpoint_str = strstr(body, "\"setpoint\"");
        if (setpoint_str) {
            setpoint_str = strchr(setpoint_str, ':');
            if (setpoint_str) {
                ferm->target_temp = atof(setpoint_str + 1);
            }
        }

        // Parse mode
        const char* mode_str = strstr(body, "\"mode\"");
        if (mode_str) {
            if (strstr(mode_str, "\"OFF\"") || strstr(mode_str, "\"off\"")) {
                ferm->mode = core::FermenterMode::OFF;
            } else if (strstr(mode_str, "\"MANUAL\"") || strstr(mode_str, "\"manual\"")) {
                ferm->mode = core::FermenterMode::MANUAL;
            } else if (strstr(mode_str, "\"PLAN\"") || strstr(mode_str, "\"plan\"")) {
                ferm->mode = core::FermenterMode::PLAN;
            }
        }

        snprintf(response, response_size,
            "{\"success\":true,\"id\":%d,\"setpoint\":%.1f,\"mode\":\"%s\"}",
            id, ferm->target_temp, mode_to_string(ferm->mode));
        return 200;
    }

    int api_pid(const char* id_str, char* response, size_t response_size) {
        uint8_t id = atoi(id_str);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            snprintf(response, response_size, "{\"error\":\"Fermenter not found\"}");
            return 404;
        }

        snprintf(response, response_size,
            "{\"fermenter_id\":%d,\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f,"
            "\"output\":%.1f,\"output_min\":%.0f,\"output_max\":%.0f,"
            "\"integral\":%.3f,\"last_error\":%.3f}",
            id, ferm->pid_params.kp, ferm->pid_params.ki, ferm->pid_params.kd,
            ferm->pid_output, ferm->pid_params.output_min, ferm->pid_params.output_max,
            ferm->pid_integral, ferm->pid_last_error);

        return 200;
    }

    int api_pid_set(const char* id_str, const char* body, char* response, size_t response_size) {
        uint8_t id = atoi(id_str);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            snprintf(response, response_size, "{\"error\":\"Fermenter not found\"}");
            return 404;
        }

        // Parse PID parameters
        const char* kp_str = strstr(body, "\"kp\"");
        const char* ki_str = strstr(body, "\"ki\"");
        const char* kd_str = strstr(body, "\"kd\"");

        if (kp_str) {
            kp_str = strchr(kp_str, ':');
            if (kp_str) ferm->pid_params.kp = atof(kp_str + 1);
        }
        if (ki_str) {
            ki_str = strchr(ki_str, ':');
            if (ki_str) ferm->pid_params.ki = atof(ki_str + 1);
        }
        if (kd_str) {
            kd_str = strchr(kd_str, ':');
            if (kd_str) ferm->pid_params.kd = atof(kd_str + 1);
        }

        snprintf(response, response_size,
            "{\"success\":true,\"id\":%d,\"kp\":%.3f,\"ki\":%.3f,\"kd\":%.3f}",
            id, ferm->pid_params.kp, ferm->pid_params.ki, ferm->pid_params.kd);
        return 200;
    }

    int api_alarms(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"alarms\":[");

        if (safety_) {
            bool first = true;
            for (uint8_t i = 1; i <= core::MAX_FERMENTERS && offset < (int)response_size - 150; i++) {
                auto* alarm = safety_->get_alarm_state(i);
                if (alarm && (alarm->temp_high_alarm || alarm->temp_low_alarm ||
                              alarm->pressure_high_alarm || alarm->sensor_failure_alarm)) {
                    if (!first) offset += snprintf(response + offset, response_size - offset, ",");
                    first = false;
                    offset += snprintf(response + offset, response_size - offset,
                        "{\"fermenter\":%d,\"temp_high\":%s,\"temp_low\":%s,"
                        "\"pressure_high\":%s,\"sensor_failure\":%s}",
                        i, alarm->temp_high_alarm ? "true" : "false",
                        alarm->temp_low_alarm ? "true" : "false",
                        alarm->pressure_high_alarm ? "true" : "false",
                        alarm->sensor_failure_alarm ? "true" : "false");
                }
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_modbus_stats(char* response, size_t response_size) {
        auto& sys = state_->get_system_state();

        float error_rate = 0;
        if (sys.modbus_transactions > 0) {
            error_rate = 100.0f * sys.modbus_errors / sys.modbus_transactions;
        }

        snprintf(response, response_size,
            "{\"transactions\":%lu,\"errors\":%lu,\"error_rate\":%.2f}",
            (unsigned long)sys.modbus_transactions,
            (unsigned long)sys.modbus_errors,
            error_rate);

        return 200;
    }

    int api_inputs(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"inputs\":[");

        if (gpio_) {
            for (uint8_t i = 0; i < 8; i++) {
                if (i > 0) offset += snprintf(response + offset, response_size - offset, ",");
                bool state = gpio_->get_digital_input(i);
                offset += snprintf(response + offset, response_size - offset,
                    "{\"id\":%d,\"state\":%s}", i + 1, state ? "true" : "false");
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_outputs(char* response, size_t response_size) {
        int offset = snprintf(response, response_size, "{\"outputs\":[");

        if (gpio_) {
            for (uint8_t i = 0; i < 8; i++) {
                if (i > 0) offset += snprintf(response + offset, response_size - offset, ",");
                bool state = gpio_->get_relay_state(i);
                offset += snprintf(response + offset, response_size - offset,
                    "{\"id\":%d,\"state\":%s}", i + 1, state ? "true" : "false");
            }
        }

        snprintf(response + offset, response_size - offset, "]}");
        return 200;
    }

    int api_output_set(const char* id_str, const char* body, char* response, size_t response_size) {
        if (!gpio_) {
            snprintf(response, response_size, "{\"error\":\"GPIO not available\"}");
            return 500;
        }

        uint8_t id = atoi(id_str);
        if (id < 1 || id > 8) {
            snprintf(response, response_size, "{\"error\":\"Invalid output ID (1-8)\"}");
            return 400;
        }

        bool new_state = (strstr(body, "\"state\":true") != nullptr ||
                          strstr(body, "\"state\":1") != nullptr);

        gpio_->set_relay(id - 1, new_state);

        snprintf(response, response_size,
            "{\"success\":true,\"output\":%d,\"state\":%s}",
            id, new_state ? "true" : "false");
        return 200;
    }

    int api_config(char* response, size_t response_size) {
        if (!config_) {
            snprintf(response, response_size, "{\"error\":\"Config not loaded\"}");
            return 500;
        }

        snprintf(response, response_size,
            "{\"fermenter_count\":%d,\"modbus_device_count\":%d,"
            "\"gpio_relay_count\":%d,\"timing\":{"
            "\"modbus_poll_ms\":%lu,\"pid_interval_ms\":%lu,"
            "\"safety_check_ms\":%lu}}",
            config_->fermenter_count,
            config_->hardware.modbus_device_count,
            config_->hardware.gpio_relay_count,
            (unsigned long)config_->modbus_timing.poll_interval_ms,
            (unsigned long)config_->pid_timing.calculation_interval_ms,
            (unsigned long)config_->safety_timing.check_interval_ms);

        return 200;
    }

    int api_modules(char* response, size_t response_size) {
        snprintf(response, response_size,
            "{\"modules\":{"
            "\"wifi\":%s,"
            "\"ntp\":%s,"
            "\"http\":%s,"
            "\"mqtt\":%s,"
            "\"can\":%s,"
            "\"debug_console\":%s"
            "}}",
#ifdef WIFI_NTP_ENABLED
            "true", "true",
#else
            "false", "false",
#endif
#ifdef HTTP_ENABLED
            "true",
#else
            "false",
#endif
#ifdef MQTT_ENABLED
            "true",
#else
            "false",
#endif
#ifdef CAN_ENABLED
            "true",
#else
            "false",
#endif
#ifdef DEBUG_CONSOLE_ENABLED
            "true"
#else
            "false"
#endif
        );

        return 200;
    }

    int api_cpu_history(char* response, size_t response_size) {
        const auto& history = state_->get_cpu_history();
        uint8_t count = history.get_sample_count();

        // Build JSON array of samples
        int offset = snprintf(response, response_size,
            "{\"samples\":[");

        if (count > 0) {
            uint8_t samples[core::CpuHistory::MAX_SAMPLES];
            uint8_t actual_count = history.get_samples(samples);

            for (uint8_t i = 0; i < actual_count; i++) {
                if (i > 0) {
                    offset += snprintf(response + offset, response_size - offset, ",");
                }
                offset += snprintf(response + offset, response_size - offset, "%d", samples[i]);
            }
        }

        snprintf(response + offset, response_size - offset,
            "],\"interval_sec\":%d,\"count\":%d}",
            core::CpuHistory::SAMPLE_INTERVAL_MS / 1000,
            count);

        return 200;
    }

    int api_network_history(char* response, size_t response_size) {
        const auto& history = state_->get_network_history();
        uint8_t count = history.get_sample_count();

        // Build JSON array of samples (utilization %)
        int offset = snprintf(response, response_size,
            "{\"samples\":[");

        if (count > 0) {
            uint8_t samples[core::NetworkHistory::MAX_SAMPLES];
            uint8_t actual_count = history.get_samples(samples);

            for (uint8_t i = 0; i < actual_count; i++) {
                if (i > 0) {
                    offset += snprintf(response + offset, response_size - offset, ",");
                }
                offset += snprintf(response + offset, response_size - offset, "%d", samples[i]);
            }
        }

        snprintf(response + offset, response_size - offset,
            "],\"interval_sec\":%d,\"count\":%d,\"link_speed_mbps\":%lu,\"channel\":%d}",
            core::NetworkHistory::SAMPLE_INTERVAL_MS / 1000,
            count,
            (unsigned long)history.get_link_speed_mbps(),
            history.get_channel());

        return 200;
    }

    int api_wifi_summary(char* response, size_t response_size) {
        const auto& history = state_->get_network_history();

#ifdef WIFI_NTP_ENABLED
        if (wifi_prov_) {
            snprintf(response, response_size,
                "{\"connected\":%s,\"ssid\":\"%s\",\"link_speed_mbps\":%lu,"
                "\"channel\":%d,\"tx_bytes\":%llu,\"rx_bytes\":%llu}",
                wifi_prov_->is_connected() ? "true" : "false",
                wifi_prov_->get_ssid() ? wifi_prov_->get_ssid() : "",
                (unsigned long)history.get_link_speed_mbps(),
                history.get_channel(),
                (unsigned long long)history.get_total_tx_bytes(),
                (unsigned long long)history.get_total_rx_bytes());
            return 200;
        }
#endif
        snprintf(response, response_size,
            "{\"connected\":false,\"ssid\":\"\",\"link_speed_mbps\":0,"
            "\"channel\":0,\"tx_bytes\":0,\"rx_bytes\":0}");
        return 200;
    }

#ifdef WIFI_NTP_ENABLED
    int api_wifi_status(char* response, size_t response_size) {
        if (!wifi_prov_) {
            snprintf(response, response_size, "{\"error\":\"WiFi not available\"}");
            return 500;
        }

        snprintf(response, response_size,
            "{\"connected\":%s,\"ip\":\"%s\",\"ssid\":\"%s\","
            "\"state\":\"%s\",\"provisioning\":%s}",
            wifi_prov_->is_connected() ? "true" : "false",
            wifi_prov_->get_ip_address(),
            wifi_prov_->get_ssid(),
            wifi_prov_->get_state_string(),
            wifi_prov_->is_provisioning() ? "true" : "false");

        return 200;
    }
#endif

#ifdef CAN_ENABLED
    int api_can_status(char* response, size_t response_size) {
        if (!can_module_) {
            snprintf(response, response_size, "{\"error\":\"CAN not available\"}");
            return 500;
        }

        auto* can = static_cast<CANModule*>(can_module_);
        auto stats = can->get_stats();

        snprintf(response, response_size,
            "{\"tx\":%lu,\"rx\":%lu,\"errors\":%lu,\"state\":\"%s\",\"bitrate\":%lu}",
            (unsigned long)stats.tx_count,
            (unsigned long)stats.rx_count,
            (unsigned long)stats.error_count,
            stats.bus_ok ? "OK" : "ERROR",
            (unsigned long)500000);

        return 200;
    }
#endif

    int api_reboot(char* response, size_t response_size) {
        snprintf(response, response_size, "{\"success\":true,\"message\":\"Rebooting...\"}");
        // Actual reboot should be triggered after response is sent
        return 200;
    }
};

#ifdef ESP32_BUILD
// Static instance pointer for handlers
HttpServer* HttpServer::instance_ = nullptr;
#endif

} // namespace modules
