#pragma once

/**
 * mDNS Service for ESP32 Fermentation Controller
 *
 * Advertises the device on the local network as:
 *   fermenter-XXXXXX.local (where XXXXXX is last 3 bytes of MAC)
 *
 * This allows accessing the device by hostname instead of IP address,
 * and enables proper HTTPS certificate validation when the certificate
 * includes the hostname in the Subject Alternative Name (SAN).
 */

#include <cstdio>

#ifdef ESP32_BUILD

#include "mdns.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

namespace modules {

class MdnsService {
public:
    MdnsService() : initialized_(false) {
        hostname_[0] = '\0';
    }

    /**
     * Initialize mDNS service
     * @return true on success
     */
    bool init() {
        if (initialized_) {
            return true;
        }

        // Generate hostname from MAC address
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(hostname_, sizeof(hostname_), "fermenter-%02X%02X%02X",
                 mac[3], mac[4], mac[5]);

        // Initialize mDNS
        esp_err_t err = mdns_init();
        if (err != ESP_OK) {
            ESP_LOGE("MDNS", "Failed to initialize mDNS: %s", esp_err_to_name(err));
            return false;
        }

        // Set hostname
        err = mdns_hostname_set(hostname_);
        if (err != ESP_OK) {
            ESP_LOGE("MDNS", "Failed to set hostname: %s", esp_err_to_name(err));
            mdns_free();
            return false;
        }

        // Set instance name (friendly name shown in discovery)
        char instance[64];
        snprintf(instance, sizeof(instance), "Fermenter Controller %02X%02X%02X",
                 mac[3], mac[4], mac[5]);
        mdns_instance_name_set(instance);

        // Advertise HTTP service
        err = mdns_service_add(instance, "_http", "_tcp", 80, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW("MDNS", "Failed to add HTTP service: %s", esp_err_to_name(err));
        }

        // Advertise HTTPS service
        err = mdns_service_add(instance, "_https", "_tcp", 443, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGW("MDNS", "Failed to add HTTPS service: %s", esp_err_to_name(err));
        }

        initialized_ = true;
        ESP_LOGI("MDNS", "mDNS initialized: %s.local", hostname_);
        return true;
    }

    /**
     * Stop mDNS service
     */
    void stop() {
        if (initialized_) {
            mdns_free();
            initialized_ = false;
            ESP_LOGI("MDNS", "mDNS stopped");
        }
    }

    /**
     * Get the device hostname (without .local suffix)
     * @return hostname string (e.g., "fermenter-230778")
     */
    const char* get_hostname() const {
        return hostname_;
    }

    /**
     * Get the full FQDN
     * @param buffer Output buffer
     * @param size Buffer size
     * @return Length written
     */
    size_t get_fqdn(char* buffer, size_t size) const {
        if (!buffer || size < 32) return 0;
        return snprintf(buffer, size, "%s.local", hostname_);
    }

    /**
     * Check if mDNS is initialized
     */
    bool is_initialized() const {
        return initialized_;
    }

    /**
     * Generate hostname from MAC (static utility for cert generation)
     * @param buffer Output buffer (min 20 bytes)
     * @param size Buffer size
     * @return Length written
     */
    static size_t generate_hostname(char* buffer, size_t size) {
        if (!buffer || size < 20) return 0;

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        return snprintf(buffer, size, "fermenter-%02X%02X%02X",
                        mac[3], mac[4], mac[5]);
    }

private:
    bool initialized_;
    char hostname_[32];
};

} // namespace modules

#else
// Simulator stub

namespace modules {

class MdnsService {
public:
    bool init() { return true; }
    void stop() {}
    const char* get_hostname() const { return "fermenter-SIM"; }
    size_t get_fqdn(char* buffer, size_t size) const {
        if (!buffer || size < 20) return 0;
        return snprintf(buffer, size, "fermenter-SIM.local");
    }
    bool is_initialized() const { return true; }
    static size_t generate_hostname(char* buffer, size_t size) {
        if (!buffer || size < 20) return 0;
        return snprintf(buffer, size, "fermenter-SIM");
    }
};

} // namespace modules

#endif
