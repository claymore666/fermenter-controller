#pragma once

#include "hal/interfaces.h"
#include <cstdint>
#include <cstring>

#ifdef ESP32_BUILD
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/apps/netbiosns.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "esp_timer.h"
// TODO: HTTPS support requires cmake configuration for certificate embedding
// #include "esp_https_server.h"
#else
// Simulator stubs for ESP logging
#define ESP_LOGI(tag, fmt, ...) (void)tag
#define ESP_LOGW(tag, fmt, ...) (void)tag
#define ESP_LOGE(tag, fmt, ...) (void)tag
#endif

namespace modules {

/**
 * WiFi Provisioning Module
 *
 * Supports multiple provisioning methods:
 * 1. SmartConfig (ESP-Touch) - Phone app broadcasts credentials
 * 2. SoftAP + Captive Portal - Web interface for manual entry
 * 3. Stored credentials - Auto-connect on boot
 *
 * Flow:
 * 1. On boot, try stored credentials
 * 2. If no credentials or connection fails, start provisioning
 * 3. SmartConfig + AP run simultaneously
 * 4. Once provisioned, save credentials and connect
 */
class WifiProvisioning {
public:
    enum class State {
        IDLE,
        CONNECTING,
        CONNECTED,
        PROVISIONING,      // SmartConfig or AP active
        PROVISIONING_DONE,
        FAILED
    };

    enum class ProvisionMethod {
        NONE,
        STORED,
        SMARTCONFIG,
        CAPTIVE_PORTAL,
        WPS
    };

    struct Config {
        char ap_ssid[33];          // AP SSID (default: "Fermenter-XXXX")
        char ap_password[65];      // AP password (empty = open)
        char hostname[32];         // mDNS hostname
        uint32_t connect_timeout_ms;
        uint32_t provision_timeout_ms;
        bool enable_smartconfig;
        bool enable_captive_portal;
    };

    struct Credentials {
        char ssid[33];
        char password[65];
        bool valid;
    };

    WifiProvisioning(hal::ITimeInterface* time_hal)
        : time_hal_(time_hal)
        , state_(State::IDLE)
        , provision_method_(ProvisionMethod::NONE)
        , initialized_(false) {
        // Default config
        memset(&config_, 0, sizeof(config_));
        strncpy(config_.ap_ssid, "Fermenter-Setup", sizeof(config_.ap_ssid) - 1);
        config_.ap_password[0] = '\0';  // Open AP
        strncpy(config_.hostname, "fermenter", sizeof(config_.hostname) - 1);
        config_.connect_timeout_ms = 10000;
        config_.provision_timeout_ms = 300000;  // 5 minutes
        config_.enable_smartconfig = true;
        config_.enable_captive_portal = true;

        memset(&stored_creds_, 0, sizeof(stored_creds_));
        memset(&pending_creds_, 0, sizeof(pending_creds_));
        memset(ip_address_, 0, sizeof(ip_address_));
    }

    /**
     * Configure provisioning settings
     */
    void configure(const Config& cfg) {
        config_ = cfg;
    }

    /**
     * Initialize WiFi subsystem
     */
    bool init() {
#ifdef ESP32_BUILD
        if (initialized_) return true;

        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        // Initialize TCP/IP and event loop
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        // Create default AP and STA netifs
        ap_netif_ = esp_netif_create_default_wifi_ap();
        sta_netif_ = esp_netif_create_default_wifi_sta();

        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        // Generate unique AP name and hostname from MAC address
        // Hostname format: fermenter-XXXXXX (last 3 MAC bytes, uppercase hex)
        // This matches mDNS hostname and certificate SAN
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(config_.ap_ssid, sizeof(config_.ap_ssid),
                 "Fermenter-%02X%02X", mac[4], mac[5]);
        snprintf(config_.hostname, sizeof(config_.hostname),
                 "fermenter-%02X%02X%02X", mac[3], mac[4], mac[5]);

        // Set DHCP hostname so router registers it correctly
        esp_netif_set_hostname(sta_netif_, config_.hostname);

        // Create event group
        wifi_event_group_ = xEventGroupCreate();

        // Register event handlers
        ESP_ERROR_CHECK(esp_event_handler_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this));
        ESP_ERROR_CHECK(esp_event_handler_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, this));
        ESP_ERROR_CHECK(esp_event_handler_register(
            SC_EVENT, ESP_EVENT_ANY_ID, &smartconfig_event_handler, this));

        initialized_ = true;
        ESP_LOGI("Prov", "WiFi provisioning initialized");
        return true;
#else
        initialized_ = true;
        return true;
#endif
    }

    /**
     * Start provisioning process
     * Tries stored credentials first, then starts provisioning if needed
     */
    bool start() {
        if (!initialized_ && !init()) {
            return false;
        }

        // Already connected? Just return success
        if (state_ == State::CONNECTED) {
            ESP_LOGI("Prov", "Already connected");
            return true;
        }

        // Load stored credentials
#ifdef ESP32_BUILD
        if (load_credentials()) {
            ESP_LOGI("Prov", "Found stored credentials for: %s", stored_creds_.ssid);

            // Check if auto-connect is disabled
            if (!auto_connect_) {
                ESP_LOGI("Prov", "Auto-connect disabled, staying disconnected");
                state_ = State::IDLE;
                return false;
            }

            // Try to connect with stored credentials
            if (connect_sta(stored_creds_.ssid, stored_creds_.password)) {
                provision_method_ = ProvisionMethod::STORED;
                return true;
            }
            ESP_LOGW("Prov", "Stored credentials failed, starting provisioning");
        }

        // Start provisioning mode
        return start_provisioning();
#else
        // Simulator: just start and consider connected
        state_ = State::CONNECTED;
        strncpy(ip_address_, "192.168.1.100", sizeof(ip_address_));
        provision_method_ = ProvisionMethod::STORED;
        return true;
#endif
    }

    /**
     * Start provisioning mode (AP + SmartConfig)
     */
    bool start_provisioning() {
#ifdef ESP32_BUILD
        state_ = State::PROVISIONING;

        // Start AP mode
        if (config_.enable_captive_portal) {
            start_ap();
        }

        // Start SmartConfig
        if (config_.enable_smartconfig) {
            start_smartconfig();
        }

        ESP_LOGI("Prov", "Provisioning started");
        ESP_LOGI("Prov", "  AP: %s", config_.ap_ssid);
        if (config_.enable_smartconfig) {
            ESP_LOGI("Prov", "  SmartConfig: Waiting for ESP-Touch...");
        }

        return false;  // Not connected yet, waiting for provisioning
#else
        state_ = State::PROVISIONING;
        return true;
#endif
    }

    /**
     * Stop provisioning and use provided credentials
     */
    bool provision(const char* ssid, const char* password) {
#ifdef ESP32_BUILD
        // Stop provisioning services
        stop_provisioning();

        // Save credentials
        strncpy(stored_creds_.ssid, ssid, sizeof(stored_creds_.ssid) - 1);
        strncpy(stored_creds_.password, password, sizeof(stored_creds_.password) - 1);
        stored_creds_.valid = true;
        save_credentials();

        // Connect with new credentials
        if (connect_sta(ssid, password)) {
            provision_method_ = ProvisionMethod::CAPTIVE_PORTAL;
            // state_ is already CONNECTED from connect_sta()
            return true;
        }

        state_ = State::FAILED;
        return false;
#else
        strncpy(stored_creds_.ssid, ssid, sizeof(stored_creds_.ssid) - 1);
        strncpy(stored_creds_.password, password, sizeof(stored_creds_.password) - 1);
        stored_creds_.valid = true;
        state_ = State::CONNECTED;
        strncpy(ip_address_, "192.168.1.100", sizeof(ip_address_));
        return true;
#endif
    }

    /**
     * Check if connected
     */
    bool is_connected() const {
        return state_ == State::CONNECTED;
    }

    /**
     * Check if provisioning is active
     */
    bool is_provisioning() const {
        return state_ == State::PROVISIONING;
    }

    /**
     * Get current state
     */
    State get_state() const { return state_; }

    /**
     * Get provision method used
     */
    ProvisionMethod get_provision_method() const { return provision_method_; }

    /**
     * Get IP address
     */
    const char* get_ip_address() const {
        if (state_ != State::CONNECTED) return nullptr;
        return ip_address_;
    }

    /**
     * Get network mask
     */
    const char* get_netmask() const {
        if (state_ != State::CONNECTED) return nullptr;
        return netmask_;
    }

    /**
     * Get gateway address
     */
    const char* get_gateway() const {
        if (state_ != State::CONNECTED) return nullptr;
        return gateway_;
    }

    /**
     * Get WiFi RSSI (signal strength)
     */
    int get_rssi() const {
        if (state_ != State::CONNECTED) return 0;
#ifdef ESP32_BUILD
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return ap_info.rssi;
        }
#endif
        return 0;
    }

    /**
     * Get negotiated link speed in Mbps (approximate based on PHY mode)
     */
    uint32_t get_link_speed_mbps() const {
        if (state_ != State::CONNECTED) return 0;
#ifdef ESP32_BUILD
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            // Determine link speed from PHY mode
            if (ap_info.phy_11n) {
                // 802.11n - check if 40MHz or 20MHz
                if (ap_info.second != WIFI_SECOND_CHAN_NONE) {
                    return 150;  // HT40
                } else {
                    return 72;   // HT20
                }
            } else if (ap_info.phy_11g) {
                return 54;       // 802.11g
            } else if (ap_info.phy_11b) {
                return 11;       // 802.11b
            } else {
                return 54;       // Default assumption
            }
        }
#endif
        return 0;
    }

    /**
     * Get WiFi channel
     */
    uint8_t get_channel() const {
        if (state_ != State::CONNECTED) return 0;
#ifdef ESP32_BUILD
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return ap_info.primary;
        }
#endif
        return 0;
    }

    /**
     * Get AP IP address (for captive portal)
     */
    const char* get_ap_ip_address() const {
        return "192.168.4.1";  // Default ESP32 AP IP
    }

    /**
     * Get stored SSID
     */
    const char* get_ssid() const {
        return stored_creds_.ssid;
    }

    /**
     * Clear stored credentials
     */
    void clear_credentials() {
#ifdef ESP32_BUILD
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_key(nvs, "ssid");
            nvs_erase_key(nvs, "password");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
#endif
        memset(&stored_creds_, 0, sizeof(stored_creds_));
        ESP_LOGI("Prov", "Credentials cleared");
    }

    /**
     * Disconnect from WiFi (persistent - won't auto-connect on reboot)
     */
    void disconnect() {
#ifdef ESP32_BUILD
        // Stop SmartConfig if running
        esp_smartconfig_stop();
        esp_wifi_disconnect();
        esp_wifi_stop();
#endif
        state_ = State::IDLE;
        set_auto_connect(false);
        ESP_LOGI("Prov", "Disconnected, auto-connect disabled");
    }

    /**
     * Set auto-connect flag (persisted to NVS)
     */
    void set_auto_connect(bool enabled) {
#ifdef ESP32_BUILD
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_u8(nvs, "auto_connect", enabled ? 1 : 0);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
#endif
        auto_connect_ = enabled;
    }

    /**
     * Get auto-connect flag
     */
    bool get_auto_connect() const {
        return auto_connect_;
    }

    /**
     * Get state as string
     */
    const char* get_state_string() const {
        switch (state_) {
            case State::IDLE:              return "IDLE";
            case State::CONNECTING:        return "CONNECTING";
            case State::CONNECTED:         return "CONNECTED";
            case State::PROVISIONING:      return "PROVISIONING";
            case State::PROVISIONING_DONE: return "PROVISIONING_DONE";
            case State::FAILED:            return "FAILED";
            default:                       return "UNKNOWN";
        }
    }

private:
#ifdef ESP32_BUILD
    bool connect_sta(const char* ssid, const char* password) {
        state_ = State::CONNECTING;

        // Configure STA
        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());  // Explicitly connect

        // Wait for connection
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group_,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE, pdFALSE,
            pdMS_TO_TICKS(config_.connect_timeout_ms)
        );

        if (bits & WIFI_CONNECTED_BIT) {
            state_ = State::CONNECTED;
            ESP_LOGI("Prov", "Connected to %s", ssid);
            return true;
        }

        ESP_LOGW("Prov", "Failed to connect to %s", ssid);
        esp_wifi_stop();
        state_ = State::FAILED;
        return false;
    }

    void start_ap() {
        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.ap.ssid, config_.ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
        wifi_config.ap.ssid_len = strlen(config_.ap_ssid);

        if (strlen(config_.ap_password) > 0) {
            strncpy((char*)wifi_config.ap.password, config_.ap_password,
                    sizeof(wifi_config.ap.password) - 1);
            wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        wifi_config.ap.max_connection = 4;
        wifi_config.ap.channel = 1;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        // Start captive portal HTTP server
        start_captive_portal();

        // Start DNS server for captive portal redirect
        start_dns_server();

        ESP_LOGI("Prov", "AP started: %s", config_.ap_ssid);
    }

    void start_smartconfig() {
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&sc_cfg));
        ESP_LOGI("Prov", "SmartConfig started");
    }

    void stop_provisioning() {
        if (config_.enable_smartconfig) {
            esp_smartconfig_stop();
        }
        if (captive_portal_server_) {
            httpd_stop(captive_portal_server_);
            captive_portal_server_ = nullptr;
        }
        // Stop DNS server
        if (dns_task_handle_) {
            vTaskDelete(dns_task_handle_);
            dns_task_handle_ = nullptr;
        }
        if (dns_socket_ >= 0) {
            close(dns_socket_);
            dns_socket_ = -1;
        }
        // Stop provision timer if running
        if (provision_timer_) {
            esp_timer_stop(provision_timer_);
            esp_timer_delete(provision_timer_);
            provision_timer_ = nullptr;
        }
        ESP_LOGI("Prov", "Provisioning stopped");
    }

    void start_dns_server() {
        // Create UDP socket for DNS
        dns_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (dns_socket_ < 0) {
            ESP_LOGE("DNS", "Failed to create socket");
            return;
        }

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(53);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(dns_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ESP_LOGE("DNS", "Failed to bind socket");
            close(dns_socket_);
            dns_socket_ = -1;
            return;
        }

        // Create DNS server task
        xTaskCreate(dns_server_task, "dns_server", 4096, this, 5, &dns_task_handle_);
        ESP_LOGI("DNS", "DNS server started");
    }

    static void dns_server_task(void* arg) {
        WifiProvisioning* self = static_cast<WifiProvisioning*>(arg);
        uint8_t buffer[512];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            int len = recvfrom(self->dns_socket_, buffer, sizeof(buffer), 0,
                              (struct sockaddr*)&client_addr, &client_len);
            if (len < 12) continue;  // DNS header is 12 bytes

            // Build DNS response - redirect all queries to 192.168.4.1
            uint8_t response[512];
            memcpy(response, buffer, len);

            // Set response flags: QR=1 (response), AA=1 (authoritative)
            response[2] = 0x84;  // QR=1, Opcode=0, AA=1, TC=0, RD=0
            response[3] = 0x00;  // RA=0, Z=0, RCODE=0 (no error)

            // Set answer count to 1
            response[6] = 0x00;
            response[7] = 0x01;

            // Find end of question section
            int pos = 12;
            while (pos < len && buffer[pos] != 0) {
                pos += buffer[pos] + 1;
            }
            pos += 5;  // Skip null terminator + QTYPE + QCLASS

            // Add answer section
            // Name pointer to question
            response[pos++] = 0xc0;
            response[pos++] = 0x0c;

            // Type A (1)
            response[pos++] = 0x00;
            response[pos++] = 0x01;

            // Class IN (1)
            response[pos++] = 0x00;
            response[pos++] = 0x01;

            // TTL (60 seconds)
            response[pos++] = 0x00;
            response[pos++] = 0x00;
            response[pos++] = 0x00;
            response[pos++] = 0x3c;

            // Data length (4 bytes for IPv4)
            response[pos++] = 0x00;
            response[pos++] = 0x04;

            // IP address 192.168.4.1
            response[pos++] = 192;
            response[pos++] = 168;
            response[pos++] = 4;
            response[pos++] = 1;

            sendto(self->dns_socket_, response, pos, 0,
                   (struct sockaddr*)&client_addr, client_len);
        }
    }

    void start_captive_portal() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 8;
        config.stack_size = 8192;

        if (httpd_start(&captive_portal_server_, &config) == ESP_OK) {
            // Register URI handlers
            httpd_uri_t root = {
                .uri = "/",
                .method = HTTP_GET,
                .handler = captive_portal_handler,
                .user_ctx = this
            };
            httpd_register_uri_handler(captive_portal_server_, &root);

            httpd_uri_t configure = {
                .uri = "/configure",
                .method = HTTP_POST,
                .handler = configure_handler,
                .user_ctx = this
            };
            httpd_register_uri_handler(captive_portal_server_, &configure);

            // Captive portal detection endpoints
            httpd_uri_t generate_204 = {
                .uri = "/generate_204",
                .method = HTTP_GET,
                .handler = captive_portal_handler,
                .user_ctx = this
            };
            httpd_register_uri_handler(captive_portal_server_, &generate_204);

            ESP_LOGI("Prov", "Captive portal started on http://192.168.4.1");
        }
    }

    static esp_err_t captive_portal_handler(httpd_req_t* req) {
        const char* html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Fermenter WiFi Setup</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; font-size: 24px; }
        label { display: block; margin-top: 10px; color: #666; }
        input { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 12px; margin-top: 20px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #45a049; }
        .info { color: #666; font-size: 12px; margin-top: 10px; }
        .alt { text-align: center; margin-top: 20px; padding-top: 20px; border-top: 1px solid #eee; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Fermenter WiFi Setup</h1>
        <form action="/configure" method="POST">
            <label for="ssid">WiFi Network</label>
            <input type="text" id="ssid" name="ssid" required placeholder="Enter WiFi name">

            <label for="password">Password</label>
            <input type="password" id="password" name="password" placeholder="Enter password">

            <button type="submit">Connect</button>
        </form>
        <p class="info">After connecting, the device will restart and join your WiFi network.</p>
        <div class="alt">
            <p>Or use <strong>ESP-Touch</strong> app for automatic setup</p>
        </div>
    </div>
</body>
</html>
)";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html, strlen(html));
        return ESP_OK;
    }

    static void url_decode(char* str) {
        char* src = str;
        char* dst = str;
        while (*src) {
            if (*src == '+') {
                *dst++ = ' ';
                src++;
            } else if (*src == '%' && src[1] && src[2]) {
                char hex[3] = {src[1], src[2], 0};
                *dst++ = (char)strtol(hex, nullptr, 16);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
    }

    static esp_err_t configure_handler(httpd_req_t* req) {
        WifiProvisioning* self = (WifiProvisioning*)req->user_ctx;

        char content[256];
        int received = httpd_req_recv(req, content, sizeof(content) - 1);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
            return ESP_FAIL;
        }
        content[received] = '\0';

        // Parse form data (ssid=xxx&password=yyy)
        char ssid[33] = {0};
        char password[65] = {0};

        char* ssid_start = strstr(content, "ssid=");
        char* pass_start = strstr(content, "password=");

        if (ssid_start) {
            ssid_start += 5;
            char* end = strchr(ssid_start, '&');
            size_t len = end ? (size_t)(end - ssid_start) : strlen(ssid_start);
            if (len > sizeof(ssid) - 1) len = sizeof(ssid) - 1;
            strncpy(ssid, ssid_start, len);
        }

        if (pass_start) {
            pass_start += 9;
            size_t len = strlen(pass_start);
            if (len > sizeof(password) - 1) len = sizeof(password) - 1;
            strncpy(password, pass_start, len);
        }

        // URL decode
        url_decode(ssid);
        url_decode(password);

        ESP_LOGI("Prov", "Received credentials for: %s", ssid);

        // Send response
        const char* response = R"(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Connecting...</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; text-align: center; }
        .container { max-width: 400px; margin: 50px auto; background: white; padding: 40px; border-radius: 8px; }
        h1 { color: #4CAF50; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Connecting...</h1>
        <p>The fermenter is now connecting to your WiFi network.</p>
        <p>This page will close automatically.</p>
    </div>
</body>
</html>
)";
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, response, strlen(response));

        // Store pending credentials and schedule provisioning with delay
        strncpy(self->pending_creds_.ssid, ssid, sizeof(self->pending_creds_.ssid) - 1);
        strncpy(self->pending_creds_.password, password, sizeof(self->pending_creds_.password) - 1);
        self->pending_creds_.valid = true;

        // Create and start one-shot timer to apply credentials after 500ms
        if (self->provision_timer_ == nullptr) {
            esp_timer_create_args_t timer_args = {
                .callback = provision_timer_callback,
                .arg = self,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "prov_timer",
                .skip_unhandled_events = false
            };
            esp_timer_create(&timer_args, &self->provision_timer_);
        }
        esp_timer_start_once(self->provision_timer_, 500000);  // 500ms delay

        return ESP_OK;
    }

    bool load_credentials() {
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) {
            return false;
        }

        size_t ssid_len = sizeof(stored_creds_.ssid);
        size_t pass_len = sizeof(stored_creds_.password);

        bool success =
            nvs_get_str(nvs, "ssid", stored_creds_.ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs, "password", stored_creds_.password, &pass_len) == ESP_OK;

        // Load auto-connect flag (default to true if not set)
        uint8_t auto_conn = 1;
        nvs_get_u8(nvs, "auto_connect", &auto_conn);
        auto_connect_ = (auto_conn != 0);

        nvs_close(nvs);
        stored_creds_.valid = success && strlen(stored_creds_.ssid) > 0;
        return stored_creds_.valid;
    }

    void save_credentials() {
        nvs_handle_t nvs;
        if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "ssid", stored_creds_.ssid);
            nvs_set_str(nvs, "password", stored_creds_.password);
            nvs_commit(nvs);
            nvs_close(nvs);
            ESP_LOGI("Prov", "Credentials saved for: %s", stored_creds_.ssid);
        }
    }

    // Event handlers
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data) {
        WifiProvisioning* self = static_cast<WifiProvisioning*>(arg);

        if (event_id == WIFI_EVENT_STA_START) {
            // Only connect if not already connecting (connect_sta handles it explicitly)
            if (self->state_ != State::CONNECTING) {
                esp_wifi_connect();
            }
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (self->state_ == State::CONNECTING) {
                xEventGroupSetBits(self->wifi_event_group_, WIFI_FAIL_BIT);
            } else if (self->state_ == State::CONNECTED) {
                self->state_ = State::FAILED;
                // Could trigger reconnection here
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
            self->ap_client_count_++;
            ESP_LOGI("Prov", "Device connected to AP, MAC: " MACSTR " (clients: %d)",
                     MAC2STR(event->mac), self->ap_client_count_);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
            if (self->ap_client_count_ > 0) self->ap_client_count_--;
            ESP_LOGI("Prov", "Device disconnected from AP, MAC: " MACSTR " (clients: %d)",
                     MAC2STR(event->mac), self->ap_client_count_);
        }
    }

    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data) {
        WifiProvisioning* self = static_cast<WifiProvisioning*>(arg);

        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            snprintf(self->ip_address_, sizeof(self->ip_address_),
                     IPSTR, IP2STR(&event->ip_info.ip));
            snprintf(self->netmask_, sizeof(self->netmask_),
                     IPSTR, IP2STR(&event->ip_info.netmask));
            snprintf(self->gateway_, sizeof(self->gateway_),
                     IPSTR, IP2STR(&event->ip_info.gw));
            self->state_ = State::CONNECTED;
            xEventGroupSetBits(self->wifi_event_group_, WIFI_CONNECTED_BIT);
            ESP_LOGI("Prov", "Got IP: %s", self->ip_address_);
        }
    }

    static void smartconfig_event_handler(void* arg, esp_event_base_t event_base,
                                          int32_t event_id, void* event_data) {
        WifiProvisioning* self = static_cast<WifiProvisioning*>(arg);

        if (event_id == SC_EVENT_SCAN_DONE) {
            ESP_LOGI("Prov", "SmartConfig scan done");
        } else if (event_id == SC_EVENT_FOUND_CHANNEL) {
            ESP_LOGI("Prov", "SmartConfig found channel");
        } else if (event_id == SC_EVENT_GOT_SSID_PSWD) {
            smartconfig_event_got_ssid_pswd_t* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;

            char ssid[33] = {0};
            char password[65] = {0};
            memcpy(ssid, evt->ssid, sizeof(evt->ssid));
            memcpy(password, evt->password, sizeof(evt->password));

            ESP_LOGI("Prov", "SmartConfig got SSID: %s", ssid);

            // Save and connect
            self->provision(ssid, password);
            self->provision_method_ = ProvisionMethod::SMARTCONFIG;
        } else if (event_id == SC_EVENT_SEND_ACK_DONE) {
            ESP_LOGI("Prov", "SmartConfig ACK sent");
        }
    }

    static constexpr int WIFI_CONNECTED_BIT = BIT0;
    static constexpr int WIFI_FAIL_BIT = BIT1;

    EventGroupHandle_t wifi_event_group_;
    esp_netif_t* ap_netif_;
    esp_netif_t* sta_netif_;
    httpd_handle_t captive_portal_server_ = nullptr;
    TaskHandle_t dns_task_handle_ = nullptr;
    int dns_socket_ = -1;
    esp_timer_handle_t provision_timer_ = nullptr;

    static void provision_timer_callback(void* arg) {
        WifiProvisioning* self = static_cast<WifiProvisioning*>(arg);
        if (self->pending_creds_.valid) {
            ESP_LOGI("Prov", "Applying pending credentials for: %s", self->pending_creds_.ssid);
            self->provision(self->pending_creds_.ssid, self->pending_creds_.password);
            self->pending_creds_.valid = false;
        }
    }

#endif // ESP32_BUILD

    hal::ITimeInterface* time_hal_;
    Config config_;
    State state_;
    ProvisionMethod provision_method_;
    Credentials stored_creds_;
    Credentials pending_creds_;
    char ip_address_[16];
    char netmask_[16];
    char gateway_[16];
    bool initialized_;
    bool auto_connect_ = true;  // Auto-connect on boot (persisted to NVS)
    uint8_t ap_client_count_ = 0;

public:
    /**
     * Check if any clients are connected to the AP
     */
    bool has_ap_clients() const { return ap_client_count_ > 0; }
    uint8_t get_ap_client_count() const { return ap_client_count_; }
};

} // namespace modules
