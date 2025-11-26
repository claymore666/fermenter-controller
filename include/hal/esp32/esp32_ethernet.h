#pragma once

#ifdef ESP32_BUILD
#ifdef ETHERNET_ENABLED

#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace hal {
namespace esp32 {

// Event bits for Ethernet connection state
#define ETH_CONNECTED_BIT BIT0
#define ETH_FAIL_BIT      BIT1

/**
 * Network interface information
 */
struct NetworkInfo {
    char ip_address[16];
    char netmask[16];
    char gateway[16];
    uint8_t mac[6];
    bool connected;
    int link_speed_mbps;
};

/**
 * ESP32 Ethernet interface for W5500 SPI chip
 * Used on Waveshare ESP32-S3-POE-ETH-8DI-8DO board
 *
 * Pin assignments:
 *   GPIO12: ETH_INT (interrupt)
 *   GPIO13: ETH_MOSI
 *   GPIO14: ETH_MISO
 *   GPIO15: ETH_SCLK
 *   GPIO16: ETH_CS
 *   GPIO39: ETH_RST
 */
class ESP32Ethernet {
public:
    static constexpr const char* TAG = "ESP32ETH";

    // W5500 SPI pins for Waveshare ESP32-S3-POE-ETH-8DI-8DO
    static constexpr gpio_num_t PIN_INT  = GPIO_NUM_12;
    static constexpr gpio_num_t PIN_MOSI = GPIO_NUM_13;
    static constexpr gpio_num_t PIN_MISO = GPIO_NUM_14;
    static constexpr gpio_num_t PIN_SCLK = GPIO_NUM_15;
    static constexpr gpio_num_t PIN_CS   = GPIO_NUM_16;
    static constexpr gpio_num_t PIN_RST  = GPIO_NUM_39;

    // SPI configuration
    static constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
    static constexpr int SPI_CLOCK_MHZ = 20;

    ESP32Ethernet() : initialized_(false), connected_(false), eth_handle_(nullptr), eth_netif_(nullptr) {
        memset(&info_, 0, sizeof(info_));
        eth_event_group_ = xEventGroupCreate();
    }

    ~ESP32Ethernet() {
        if (eth_handle_) {
            esp_eth_stop(eth_handle_);
            esp_eth_driver_uninstall(eth_handle_);
        }
        if (eth_netif_) {
            esp_netif_destroy(eth_netif_);
        }
        if (eth_event_group_) {
            vEventGroupDelete(eth_event_group_);
        }
    }

    /**
     * Initialize Ethernet subsystem
     * @return true on success
     */
    bool init() {
        if (initialized_) return true;

        ESP_LOGI(TAG, "Initializing W5500 Ethernet...");

        // Reset W5500
        gpio_config_t rst_conf = {};
        rst_conf.pin_bit_mask = (1ULL << PIN_RST);
        rst_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&rst_conf);
        gpio_set_level(PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        // Initialize SPI bus
        spi_bus_config_t buscfg = {};
        buscfg.miso_io_num = PIN_MISO;
        buscfg.mosi_io_num = PIN_MOSI;
        buscfg.sclk_io_num = PIN_SCLK;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;

        esp_err_t ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return false;
        }

        // Configure SPI device for W5500
        spi_device_interface_config_t devcfg = {};
        devcfg.command_bits = 16;  // W5500 uses 16-bit address
        devcfg.address_bits = 8;   // 8-bit control byte
        devcfg.mode = 0;
        devcfg.clock_speed_hz = SPI_CLOCK_MHZ * 1000 * 1000;
        devcfg.spics_io_num = PIN_CS;
        devcfg.queue_size = 20;

        // Create Ethernet MAC for W5500
        eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI_HOST, &devcfg);
        w5500_config.int_gpio_num = PIN_INT;

        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
        if (!mac) {
            ESP_LOGE(TAG, "Failed to create W5500 MAC");
            return false;
        }

        // Create Ethernet PHY for W5500
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        phy_config.autonego_timeout_ms = 0;  // W5500 doesn't need autoneg
        phy_config.reset_gpio_num = -1;      // Already reset above
        esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);
        if (!phy) {
            ESP_LOGE(TAG, "Failed to create W5500 PHY");
            return false;
        }

        // Install Ethernet driver
        esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
        ret = esp_eth_driver_install(&eth_config, &eth_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to install Ethernet driver: %s", esp_err_to_name(ret));
            return false;
        }

        // Create default netif for Ethernet
        esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netif_ = esp_netif_new(&netif_cfg);
        if (!eth_netif_) {
            ESP_LOGE(TAG, "Failed to create netif");
            return false;
        }

        // Attach Ethernet driver to netif
        ret = esp_netif_attach(eth_netif_, esp_eth_new_netif_glue(eth_handle_));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to attach Ethernet to netif: %s", esp_err_to_name(ret));
            return false;
        }

        // Register event handlers
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            ETH_EVENT, ESP_EVENT_ANY_ID,
            &eth_event_handler, this, &eth_event_instance_));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_ETH_GOT_IP,
            &ip_event_handler, this, &ip_event_instance_));

        initialized_ = true;
        ESP_LOGI(TAG, "Ethernet initialized");
        return true;
    }

    /**
     * Start Ethernet interface
     * @return true on success
     */
    bool start() {
        if (!initialized_) {
            if (!init()) return false;
        }

        esp_err_t ret = esp_eth_start(eth_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(ret));
            return false;
        }

        ESP_LOGI(TAG, "Ethernet started");
        return true;
    }

    /**
     * Stop Ethernet interface
     */
    void stop() {
        if (eth_handle_) {
            esp_eth_stop(eth_handle_);
            connected_ = false;
        }
    }

    /**
     * Check if Ethernet is connected
     */
    bool is_connected() const { return connected_; }

    /**
     * Get network information
     */
    const NetworkInfo& get_info() const { return info_; }

    /**
     * Get IP address as string
     */
    const char* get_ip_address() const { return info_.ip_address; }

    /**
     * Get netmask as string
     */
    const char* get_netmask() const { return info_.netmask; }

    /**
     * Get gateway as string
     */
    const char* get_gateway() const { return info_.gateway; }

    /**
     * Get link speed in Mbps
     */
    int get_link_speed() const { return info_.link_speed_mbps; }

    /**
     * Wait for connection with timeout
     * @param timeout_ms Timeout in milliseconds
     * @return true if connected within timeout
     */
    bool wait_for_connection(uint32_t timeout_ms = 10000) {
        EventBits_t bits = xEventGroupWaitBits(
            eth_event_group_,
            ETH_CONNECTED_BIT | ETH_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(timeout_ms));

        return (bits & ETH_CONNECTED_BIT) != 0;
    }

private:
    bool initialized_;
    bool connected_;
    esp_eth_handle_t eth_handle_;
    esp_netif_t* eth_netif_;
    EventGroupHandle_t eth_event_group_;
    esp_event_handler_instance_t eth_event_instance_;
    esp_event_handler_instance_t ip_event_instance_;
    NetworkInfo info_;

    static void eth_event_handler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data) {
        ESP32Ethernet* self = static_cast<ESP32Ethernet*>(arg);
        if (!self) return;

        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED: {
                ESP_LOGI(TAG, "Ethernet link up");
                // Get link speed
                esp_eth_handle_t eth_handle = *(esp_eth_handle_t*)event_data;
                eth_speed_t speed;
                esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &speed);
                self->info_.link_speed_mbps = (speed == ETH_SPEED_100M) ? 100 : 10;
                // Get MAC address
                esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, self->info_.mac);
                break;
            }

            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Ethernet link down");
                self->connected_ = false;
                memset(self->info_.ip_address, 0, sizeof(self->info_.ip_address));
                xEventGroupClearBits(self->eth_event_group_, ETH_CONNECTED_BIT);
                xEventGroupSetBits(self->eth_event_group_, ETH_FAIL_BIT);
                break;

            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;

            case ETHERNET_EVENT_STOP:
                ESP_LOGI(TAG, "Ethernet stopped");
                self->connected_ = false;
                break;

            default:
                break;
        }
    }

    static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                 int32_t event_id, void* event_data) {
        ESP32Ethernet* self = static_cast<ESP32Ethernet*>(arg);
        if (!self) return;

        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;

        // Store IP info
        snprintf(self->info_.ip_address, sizeof(self->info_.ip_address),
                 IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(self->info_.netmask, sizeof(self->info_.netmask),
                 IPSTR, IP2STR(&event->ip_info.netmask));
        snprintf(self->info_.gateway, sizeof(self->info_.gateway),
                 IPSTR, IP2STR(&event->ip_info.gw));

        self->connected_ = true;
        self->info_.connected = true;

        ESP_LOGI(TAG, "Ethernet got IP: %s", self->info_.ip_address);
        ESP_LOGI(TAG, "  Netmask: %s", self->info_.netmask);
        ESP_LOGI(TAG, "  Gateway: %s", self->info_.gateway);

        xEventGroupSetBits(self->eth_event_group_, ETH_CONNECTED_BIT);
        xEventGroupClearBits(self->eth_event_group_, ETH_FAIL_BIT);
    }
};

} // namespace esp32
} // namespace hal

#endif // ETHERNET_ENABLED
#endif // ESP32_BUILD
