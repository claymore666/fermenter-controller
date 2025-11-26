#pragma once

#ifdef ESP32_BUILD

#include "hal/interfaces.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace hal {
namespace esp32 {

// Event bits for WiFi connection state
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * WiFi statistics for bandwidth monitoring
 */
struct WifiStats {
    uint64_t tx_bytes;          // Total bytes transmitted
    uint64_t rx_bytes;          // Total bytes received
    uint32_t link_speed_mbps;   // Negotiated PHY rate (approximate)
    int8_t rssi;                // Signal strength in dBm
    uint8_t channel;            // WiFi channel
};

/**
 * ESP32 WiFi interface implementation
 * Uses ESP-IDF WiFi station mode
 */
class ESP32WiFi : public INetworkInterface {
public:
    ESP32WiFi() : initialized_(false), connected_(false), retry_count_(0), sta_netif_(nullptr) {
        memset(ip_address_, 0, sizeof(ip_address_));
        wifi_event_group_ = xEventGroupCreate();
    }

    ~ESP32WiFi() {
        if (initialized_) {
            disconnect();
            esp_wifi_deinit();
        }
        if (wifi_event_group_) {
            vEventGroupDelete(wifi_event_group_);
        }
    }

    /**
     * Initialize WiFi subsystem
     * Must be called before connect()
     */
    bool init() {
        if (initialized_) return true;

        // Initialize TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        sta_netif_ = esp_netif_create_default_wifi_sta();

        // Initialize WiFi with default config
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        // Register event handlers
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID,
            &wifi_event_handler, this, &wifi_event_instance_));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP,
            &ip_event_handler, this, &ip_event_instance_));

        // Set WiFi mode to station
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        initialized_ = true;
        return true;
    }

    bool connect(const char* ssid, const char* password) override {
        if (!initialized_) {
            if (!init()) return false;
        }

        // Configure WiFi
        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        retry_count_ = 0;

        // Wait for connection or failure
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group_,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(10000)  // 10 second timeout
        );

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI("WiFi", "Connected to %s", ssid);
            return true;
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGE("WiFi", "Failed to connect to %s", ssid);
            return false;
        } else {
            ESP_LOGE("WiFi", "Connection timeout to %s", ssid);
            return false;
        }
    }

    bool is_connected() const override {
        return connected_;
    }

    bool disconnect() override {
        if (!initialized_) return true;

        esp_wifi_disconnect();
        esp_wifi_stop();
        connected_ = false;
        memset(ip_address_, 0, sizeof(ip_address_));

        // Clear event bits
        xEventGroupClearBits(wifi_event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

        return true;
    }

    const char* get_ip_address() const override {
        if (!connected_) return nullptr;
        return ip_address_;
    }

    int get_rssi() const override {
        if (!connected_) return 0;

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            return ap_info.rssi;
        }
        return 0;
    }

    /**
     * Get MAC address as string
     */
    const char* get_mac_address() {
        uint8_t mac[6];
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
            snprintf(mac_address_, sizeof(mac_address_),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return mac_address_;
        }
        return nullptr;
    }

    /**
     * Set hostname for mDNS
     */
    void set_hostname(const char* hostname) {
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_set_hostname(netif, hostname);
        }
    }

    /**
     * Get comprehensive WiFi statistics for bandwidth monitoring
     * Note: Traffic counters (tx_bytes, rx_bytes) are tracked separately
     * via HTTP server response sizes since ESP-IDF doesn't expose them directly.
     */
    WifiStats get_wifi_stats() const {
        WifiStats stats = {0, 0, 0, 0, 0};

        if (!connected_) {
            return stats;
        }

        // Get AP info for RSSI, channel, and PHY mode
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            stats.rssi = ap_info.rssi;
            stats.channel = ap_info.primary;

            // Determine link speed from PHY mode
            // These are approximate maximum rates
            if (ap_info.phy_11n) {
                // 802.11n - check if 40MHz or 20MHz
                // ap_info.second field indicates secondary channel (0 = 20MHz, other = 40MHz)
                if (ap_info.second != WIFI_SECOND_CHAN_NONE) {
                    stats.link_speed_mbps = 150;  // HT40
                } else {
                    stats.link_speed_mbps = 72;   // HT20
                }
            } else if (ap_info.phy_11g) {
                stats.link_speed_mbps = 54;       // 802.11g
            } else if (ap_info.phy_11b) {
                stats.link_speed_mbps = 11;       // 802.11b
            } else {
                stats.link_speed_mbps = 54;       // Default assumption
            }
        }

        // Note: tx_bytes and rx_bytes remain 0 here
        // Traffic tracking will be done at HTTP server level
        return stats;
    }

    /**
     * Get link speed in Mbps (convenience method)
     */
    uint32_t get_link_speed_mbps() const {
        return get_wifi_stats().link_speed_mbps;
    }

    /**
     * Get WiFi channel (convenience method)
     */
    uint8_t get_channel() const {
        return get_wifi_stats().channel;
    }

private:
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data) {
        ESP32WiFi* self = static_cast<ESP32WiFi*>(arg);

        if (event_base == WIFI_EVENT) {
            switch (event_id) {
                case WIFI_EVENT_STA_START:
                    esp_wifi_connect();
                    break;

                case WIFI_EVENT_STA_DISCONNECTED:
                    self->connected_ = false;
                    if (self->retry_count_ < MAX_RETRY) {
                        esp_wifi_connect();
                        self->retry_count_++;
                        ESP_LOGI("WiFi", "Retry connection (%d/%d)",
                                 self->retry_count_, MAX_RETRY);
                    } else {
                        xEventGroupSetBits(self->wifi_event_group_, WIFI_FAIL_BIT);
                    }
                    break;

                case WIFI_EVENT_STA_CONNECTED:
                    ESP_LOGI("WiFi", "Connected to AP");
                    break;
            }
        }
    }

    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data) {
        ESP32WiFi* self = static_cast<ESP32WiFi*>(arg);

        if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;

            snprintf(self->ip_address_, sizeof(self->ip_address_),
                     IPSTR, IP2STR(&event->ip_info.ip));

            ESP_LOGI("WiFi", "Got IP: %s", self->ip_address_);

            self->connected_ = true;
            self->retry_count_ = 0;
            xEventGroupSetBits(self->wifi_event_group_, WIFI_CONNECTED_BIT);
        }
    }

    static constexpr int MAX_RETRY = 5;

    bool initialized_;
    bool connected_;
    int retry_count_;
    char ip_address_[16];
    char mac_address_[18];

    esp_netif_t* sta_netif_;
    EventGroupHandle_t wifi_event_group_;
    esp_event_handler_instance_t wifi_event_instance_;
    esp_event_handler_instance_t ip_event_instance_;
};

} // namespace esp32
} // namespace hal

#endif // ESP32_BUILD
