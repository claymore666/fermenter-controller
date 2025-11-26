/**
 * Fermentation Controller - Main Application
 * ESP32-S3 based controller for brewery fermentation
 */

#include "hal/interfaces.h"
#include "core/types.h"
#include "core/config.h"
#include "core/config_loader.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "modules/filters.h"
#include "modules/pid_controller.h"
#include "modules/modbus_module.h"
#include "modules/fermentation_plan.h"
#include "modules/rest_api.h"
#include "modules/safety_controller.h"

#ifdef DEBUG_CONSOLE_ENABLED
#include "modules/debug_console.h"
#endif

#ifdef WIFI_NTP_ENABLED
#include "modules/wifi_module.h"
#include "modules/ntp_module.h"
#include "modules/wifi_provisioning.h"
#include "modules/status_led.h"
#include "modules/mdns_service.h"
#endif

#ifdef CAN_ENABLED
#include "modules/can_module.h"
#ifdef ESP32_BUILD
#include "hal/esp32/esp32_can.h"
#endif
#endif

#ifdef ETHERNET_ENABLED
#ifdef ESP32_BUILD
#include "hal/esp32/esp32_ethernet.h"
#endif
#endif

#ifdef HTTP_ENABLED
#include "modules/http_server.h"
#endif

#ifdef ESP32_BUILD
// ESP32 specific includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_clk_tree.h"
#include "hal/esp32/esp32_serial.h"
#include "hal/esp32/esp32_time.h"
#include "hal/esp32/esp32_gpio.h"
#include "hal/esp32/esp32_modbus.h"
#include "hal/esp32/esp32_storage.h"
#else
// Native/Simulator build
#include "hal/simulator/hal_simulator.h"
#include "hal/simulator/serial_modbus.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#endif

using namespace core;
using namespace modules;

// Global instances
static StateManager g_state;
static EventBus g_events;
static SystemConfig g_config;

#ifdef ESP32_BUILD
// ESP32 HAL implementations
static hal::esp32::ESP32Modbus g_esp32_modbus(UART_NUM_1, 17, 16, -1);  // TX=17, RX=16
static hal::IModbusInterface* g_modbus = &g_esp32_modbus;
static hal::esp32::ESP32GPIO g_gpio;
static hal::esp32::ESP32Storage g_storage;
static hal::esp32::ESP32Time g_time;
// TODO: ESP32 network implementation
#ifdef DEBUG_CONSOLE_ENABLED
static hal::esp32::ESP32Serial g_serial;
#endif
#else
// Simulator implementations for native testing
static hal::simulator::SimulatorModbus g_sim_modbus;
static hal::simulator::SerialModbus g_serial_modbus;
static hal::IModbusInterface* g_modbus = &g_sim_modbus;  // Default to simulator
static hal::simulator::SimulatorGPIO g_gpio;
static hal::simulator::SimulatorStorage g_storage;
static hal::simulator::SimulatorTime g_time;
static hal::simulator::SimulatorNetwork g_network;
#ifndef UNIT_TEST
static bool g_use_serial_modbus = false;
#endif
#ifdef DEBUG_CONSOLE_ENABLED
static hal::simulator::SimulatorSerial g_serial;
#endif
#endif

// Modules
static ModbusModule* g_modbus_module = nullptr;
static FermentationPlanManager* g_plan_manager = nullptr;
static SafetyController* g_safety = nullptr;
static RestApiHandler* g_api = nullptr;
static PIDController g_pid_controllers[MAX_FERMENTERS];
#ifdef DEBUG_CONSOLE_ENABLED
static DebugConsole* g_debug_console = nullptr;
#endif
#ifdef WIFI_NTP_ENABLED
static WifiProvisioning* g_wifi_prov = nullptr;
static NtpModule* g_ntp = nullptr;
static StatusLed* g_status_led = nullptr;
static modules::MdnsService* g_mdns = nullptr;
#endif

#ifdef CAN_ENABLED
#ifdef ESP32_BUILD
static hal::esp32::ESP32CAN g_esp32_can;
static hal::ICANInterface* g_can = &g_esp32_can;
#endif
static CANModule* g_can_module = nullptr;
#endif

#ifdef ETHERNET_ENABLED
#ifdef ESP32_BUILD
static hal::esp32::ESP32Ethernet* g_ethernet = nullptr;
#endif
#endif

#ifdef HTTP_ENABLED
static HttpServer* g_http_server = nullptr;
#endif

/**
 * Cleanup allocated modules on init failure or shutdown
 * Prevents memory leaks if initialization fails partway through
 */
void cleanup_modules() {
    // Delete in reverse order of creation
#ifdef HTTP_ENABLED
    if (g_http_server) { delete g_http_server; g_http_server = nullptr; }
#endif
#ifdef DEBUG_CONSOLE_ENABLED
    if (g_debug_console) { delete g_debug_console; g_debug_console = nullptr; }
#endif
#ifdef CAN_ENABLED
    if (g_can_module) { delete g_can_module; g_can_module = nullptr; }
#endif
#ifdef ETHERNET_ENABLED
#ifdef ESP32_BUILD
    if (g_ethernet) { delete g_ethernet; g_ethernet = nullptr; }
#endif
#endif
#ifdef WIFI_NTP_ENABLED
    if (g_mdns) { g_mdns->stop(); delete g_mdns; g_mdns = nullptr; }
    if (g_ntp) { delete g_ntp; g_ntp = nullptr; }
    if (g_wifi_prov) { delete g_wifi_prov; g_wifi_prov = nullptr; }
    if (g_status_led) { delete g_status_led; g_status_led = nullptr; }
#endif
    if (g_api) { delete g_api; g_api = nullptr; }
    if (g_safety) { delete g_safety; g_safety = nullptr; }
    if (g_plan_manager) { delete g_plan_manager; g_plan_manager = nullptr; }
    if (g_modbus_module) { delete g_modbus_module; g_modbus_module = nullptr; }
}

// ============================================================================
// Network Failover Manager
// ============================================================================
#if defined(ETHERNET_ENABLED) && defined(WIFI_NTP_ENABLED) && defined(ESP32_BUILD)

/**
 * Compare two gateway addresses to determine if interfaces are on same network
 */
static bool gateways_match(const char* gw1, const char* gw2) {
    if (!gw1 || !gw2) return false;
    if (strlen(gw1) == 0 || strlen(gw2) == 0) return false;
    return strcmp(gw1, gw2) == 0;
}

/**
 * Evaluate network interfaces and manage WiFi standby
 * Called when either interface gets an IP or when Ethernet link changes
 */
static void evaluate_network_failover() {
    if (!g_wifi_prov || !g_ethernet) return;

    bool wifi_connected = g_wifi_prov->is_connected();
    bool eth_connected = g_ethernet->is_connected();
    bool wifi_standby = g_wifi_prov->is_standby();

    const char* wifi_gw = g_wifi_prov->get_gateway();
    const char* eth_gw = g_ethernet->get_gateway();

    ESP_LOGI("NetMgr", "Evaluating: WiFi=%s(%s) ETH=%s(%s) Standby=%s",
             wifi_connected ? "up" : "down", wifi_gw ? wifi_gw : "none",
             eth_connected ? "up" : "down", eth_gw ? eth_gw : "none",
             wifi_standby ? "yes" : "no");

    if (eth_connected && wifi_connected && !wifi_standby) {
        // Both connected - check if same network
        if (gateways_match(wifi_gw, eth_gw)) {
            ESP_LOGI("NetMgr", "Same gateway detected - WiFi entering standby, Ethernet primary");
            g_wifi_prov->enter_standby();
            g_state.reset_network_history();  // Reset graph when switching to Ethernet
        } else {
            ESP_LOGI("NetMgr", "Different networks - keeping both interfaces active");
        }
    } else if (!eth_connected && wifi_standby) {
        // Ethernet down, WiFi in standby - failover to WiFi
        ESP_LOGI("NetMgr", "Ethernet down - failing over to WiFi");
        g_wifi_prov->resume_from_standby();
        g_state.reset_network_history();  // Reset graph when switching to WiFi
    }
}

/**
 * Callback for Ethernet link state changes
 */
static void on_ethernet_link_change(bool link_up, void* user_data) {
    (void)user_data;
    ESP_LOGI("NetMgr", "Ethernet link %s", link_up ? "UP" : "DOWN");
    if (!link_up) {
        // Link down - trigger failover evaluation
        evaluate_network_failover();
    }
}

/**
 * Callback for Ethernet IP obtained
 */
static void on_ethernet_ip_obtained(const hal::esp32::NetworkInfo& info, void* user_data) {
    (void)user_data;
    ESP_LOGI("NetMgr", "Ethernet got IP: %s (gateway: %s)", info.ip_address, info.gateway);
    // Evaluate whether to put WiFi in standby
    evaluate_network_failover();
}

/**
 * Callback for WiFi IP obtained
 * Triggers failover evaluation when WiFi connects (e.g., after adding credentials)
 */
static void on_wifi_ip_obtained(void* user_data) {
    (void)user_data;
    ESP_LOGI("NetMgr", "WiFi got IP - evaluating failover");
    evaluate_network_failover();
}

#endif // ETHERNET_ENABLED && WIFI_NTP_ENABLED && ESP32_BUILD

/**
 * Initialize the system
 * @param config_loaded true if config was already loaded externally
 */
bool system_init(bool config_loaded = false) {
    printf("Fermentation Controller - Initializing...\n");

#ifdef ESP32_BUILD
    // Configure power management for energy savings
    // CPU: 80MHz max, 10MHz min (during light sleep)
    // Auto light sleep enabled when idle
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_err_t pm_ret = esp_pm_configure(&pm_config);
    if (pm_ret == ESP_OK) {
        ESP_LOGI("PM", "Power management enabled: 80MHz max, auto light sleep");
    } else {
        ESP_LOGW("PM", "Power management config failed: %s", esp_err_to_name(pm_ret));
    }
#endif

    if (!config_loaded) {
        // Load configuration
        ConfigLoader::load_defaults(g_config);

        // Try to load from storage
        if (!ConfigLoader::load_from_storage(&g_storage, g_config)) {
            printf("No saved config found, using defaults\n");
        }
    }

    // Initialize state manager with config
    g_state.initialize(g_config);

    // Initialize GPIO (TCA9554 for outputs, GPIO4-11 for inputs)
    if (!g_gpio.initialize()) {
        printf("WARNING: GPIO initialization failed\n");
    }

    // Create modules
    g_modbus_module = new ModbusModule(g_modbus, &g_time, &g_state, &g_events);
    g_plan_manager = new FermentationPlanManager(&g_time, &g_storage, &g_state, &g_events);
    g_safety = new SafetyController(&g_time, &g_gpio, &g_state, &g_events);
    g_api = new RestApiHandler(&g_state, &g_events, g_plan_manager, &g_config);

    // Initialize MODBUS module
    if (!g_modbus_module->initialize(g_config)) {
        printf("ERROR: Failed to initialize MODBUS module\n");
        cleanup_modules();
        return false;
    }

    // Load fermentation plans from storage
    g_plan_manager->load_from_storage();

    // Configure safety controller
    g_safety->configure(g_config.safety_timing);

    // Initialize PID controllers for each fermenter
    for (uint8_t i = 0; i < g_config.fermenter_count; i++) {
        // Get PID params from fermenter state (uses PIDParams defaults: kp=2.0, ki=0.1, kd=1.0)
        auto* ferm = g_state.get_fermenter(g_config.fermenters[i].id);
        if (ferm) {
            g_pid_controllers[i].set_tunings(
                ferm->pid_params.kp,
                ferm->pid_params.ki,
                ferm->pid_params.kd
            );
            g_pid_controllers[i].set_output_limits(
                ferm->pid_params.output_min,
                ferm->pid_params.output_max
            );
        } else {
            // Fallback to reasonable defaults
            g_pid_controllers[i].set_tunings(2.0f, 0.1f, 1.0f);
            g_pid_controllers[i].set_output_limits(0.0f, 100.0f);
        }
    }

    printf("Initialization complete\n");
    printf("  Sensors: %d\n", g_state.get_sensor_count());
    printf("  Relays: %d\n", g_state.get_relay_count());
    printf("  Fermenters: %d\n", g_state.get_fermenter_count());

#ifdef WIFI_NTP_ENABLED
    // Initialize Status LED
    g_status_led = new StatusLed(&g_time, 38);  // GPIO38 on Waveshare board
    if (!g_status_led) {
        printf("ERROR: Failed to allocate StatusLed\n");
        cleanup_modules();
        return false;
    }
    g_status_led->init();
    g_status_led->set_color(StatusLed::Color::BLUE);  // Blue during boot
    g_status_led->start_task();  // Start background LED task for consistent timing

    // Initialize WiFi provisioning
    g_wifi_prov = new WifiProvisioning(&g_time);
    if (!g_wifi_prov) {
        printf("ERROR: Failed to allocate WifiProvisioning\n");
        cleanup_modules();
        return false;
    }
    g_wifi_prov->init();

#ifdef ETHERNET_ENABLED
    // Register callback for WiFi IP obtained (triggers failover evaluation)
    g_wifi_prov->set_ip_callback(on_wifi_ip_obtained, nullptr);
#endif

    // Start WiFi (tries stored credentials, then provisioning)
    if (g_wifi_prov->start()) {
        printf("WiFi connected: %s\n", g_wifi_prov->get_ip_address());
        g_status_led->set_provisioning(false);
        g_status_led->set_wifi_connected(true);

        // Initialize mDNS for device discovery
        g_mdns = new modules::MdnsService();
        if (g_mdns && g_mdns->init()) {
            printf("mDNS: %s.local\n", g_mdns->get_hostname());
        } else {
            printf("WARNING: mDNS init failed\n");
        }

        // Initialize NTP
        g_ntp = new NtpModule(&g_time);
        if (!g_ntp) {
            printf("WARNING: Failed to allocate NtpModule, continuing without NTP\n");
        } else {
            modules::NtpModule::Config ntp_config;
            ntp_config.server = "pool.ntp.org";
            ntp_config.timezone = "CET-1CEST,M3.5.0,M10.5.0/3";  // Central Europe
            g_ntp->configure(ntp_config);

            if (g_ntp->init() && g_ntp->wait_for_sync(10000)) {
                printf("NTP synced\n");
                g_status_led->set_ntp_synced(true);
                g_status_led->set_color(StatusLed::Color::GREEN);  // Boot complete, all OK
            } else {
                printf("NTP sync failed\n");
                g_status_led->set_ntp_synced(false);
                g_status_led->set_color(StatusLed::Color::YELLOW);  // Boot complete but NTP failed
            }
        }
    } else {
        printf("WiFi not connected, in provisioning mode\n");
        g_status_led->set_provisioning(true);
    }

    g_status_led->update();

    // Initialize storage for HTTP server (NVS already init'd by WiFi)
    if (!g_storage.initialize()) {
        printf("WARNING: Storage init failed, password persistence disabled\n");
    }
#endif

#ifdef ETHERNET_ENABLED
#ifdef ESP32_BUILD
    // Initialize Ethernet (W5500 SPI)
    g_ethernet = new hal::esp32::ESP32Ethernet();
    if (g_ethernet && g_ethernet->init()) {
#ifdef WIFI_NTP_ENABLED
        // Set up failover callbacks before starting
        g_ethernet->set_callbacks(on_ethernet_link_change, on_ethernet_ip_obtained, nullptr);
#endif
        if (g_ethernet->start()) {
            // Wait briefly for link
            if (g_ethernet->wait_for_connection(5000)) {
                printf("Ethernet connected: %s\n", g_ethernet->get_ip_address());
#ifdef WIFI_NTP_ENABLED
                // Evaluate network failover now that Ethernet is up
                evaluate_network_failover();
#endif
            } else {
                printf("Ethernet: link up, waiting for DHCP...\n");
            }
        }
    } else {
        printf("WARNING: Ethernet init failed\n");
    }
#endif
#endif

#ifdef HTTP_ENABLED
    // Initialize HTTP server (requires network - WiFi or Ethernet)
    bool network_available = false;
#ifdef WIFI_NTP_ENABLED
    network_available = (g_wifi_prov && g_wifi_prov->is_connected());
#endif
#ifdef ETHERNET_ENABLED
    if (!network_available && g_ethernet && g_ethernet->is_connected()) {
        network_available = true;
        // If WiFi is in provisioning mode, stop captive portal to free port 80
#ifdef WIFI_NTP_ENABLED
        if (g_wifi_prov && g_wifi_prov->is_provisioning()) {
            ESP_LOGI("Main", "Ethernet available - stopping captive portal for main HTTP server");
            g_wifi_prov->stop_captive_portal();
        }
#endif
    }
#endif
    if (network_available) {
        g_http_server = new HttpServer(
            &g_time, &g_state, &g_events, &g_config,
            g_safety, g_plan_manager, g_modbus, &g_storage, &g_gpio
        );
        if (!g_http_server) {
            printf("WARNING: Failed to allocate HttpServer, continuing without HTTP\n");
        } else {
#ifdef WIFI_NTP_ENABLED
            g_http_server->set_wifi_provisioning(g_wifi_prov);
#endif
#ifdef ETHERNET_ENABLED
            g_http_server->set_ethernet(g_ethernet);
#endif
            // HttpServer loads provisioning state from NVS in constructor

            // Start HTTP server with background cert generation
            // HTTPS will be enabled once TLS issues are resolved
            if (g_http_server->start_with_background_https(80)) {
                printf("HTTP server started on port 80 (cert gen in background)\n");
            } else {
                printf("WARNING: HTTP server failed to start\n");
            }
        }
    }
#endif

#ifdef CAN_ENABLED
    // Initialize CAN bus
    g_can_module = new CANModule(g_can, &g_time, &g_state, &g_events);
    if (!g_can_module) {
        printf("WARNING: Failed to allocate CANModule, continuing without CAN\n");
    } else if (g_can_module->initialize(500000)) {  // 500kbps default
        printf("CAN bus initialized at 500kbps\n");
#ifdef HTTP_ENABLED
        if (g_http_server) {
            g_http_server->set_can_module(g_can_module);
        }
#endif
    } else {
        printf("WARNING: CAN bus initialization failed\n");
    }
#endif

#ifdef DEBUG_CONSOLE_ENABLED
    g_debug_console = new DebugConsole(
        &g_serial, &g_time, &g_state, &g_events,
        &g_config, g_safety, g_plan_manager, g_modbus
#ifdef WIFI_NTP_ENABLED
        , g_wifi_prov, &g_storage, &g_gpio
#else
        , nullptr, nullptr, nullptr
#endif
#ifdef CAN_ENABLED
        , g_can_module
#else
        , nullptr
#endif
#ifdef WIFI_NTP_ENABLED
        , g_status_led
#else
        , nullptr
#endif
#ifdef ETHERNET_ENABLED
        , g_ethernet
#else
        , nullptr
#endif
    );
    g_debug_console->initialize(115200);
#endif

    return true;
}

/**
 * Main control loop iteration
 */
void control_loop() {
#ifdef ESP32_BUILD
    uint64_t loop_start_us = esp_timer_get_time();
#endif

    // 1. Poll MODBUS sensors
    g_modbus_module->poll_cycle();

    // 2. Update fermentation plans
    g_plan_manager->update();

    // 3. Run PID control for each fermenter
    for (uint8_t i = 0; i < g_config.fermenter_count; i++) {
        uint8_t ferm_id = g_config.fermenters[i].id;
        auto* ferm = g_state.get_fermenter(ferm_id);
        if (!ferm) continue;

        // Skip if not in control mode
        if (ferm->mode == FermenterMode::OFF) continue;

        // Check temperature sensor quality - don't control with bad sensor data
        auto* temp_sensor = g_state.get_sensor_by_id(ferm->temp_sensor_id);
        if (!temp_sensor || temp_sensor->quality != SensorQuality::GOOD) {
            // Sensor is bad - turn off cooling as a safe default
            uint8_t relay_id = ferm->cooling_relay_id;
            if (relay_id != 0xFF) {
                g_state.set_relay_state(relay_id, false, g_time.millis());
            }
            continue;  // Skip PID computation
        }

        // Get setpoint from plan or manual
        float setpoint = ferm->target_temp;

        // Compute PID output
        float output = g_pid_controllers[i].compute(setpoint, ferm->current_temp);

        // Update fermenter state with PID output
        ferm->pid_output = output;

        // Apply output to cooling relay (time-proportional control would go here)
        // For now, simple on/off based on 50% threshold
        bool cooling_on = output > 50.0f;
        uint8_t relay_id = ferm->cooling_relay_id;
        if (relay_id != 0xFF) {
            g_state.set_relay_state(relay_id, cooling_on, g_time.millis());
        }
    }

    // 4. Safety checks
    g_safety->check();

    // 5. Update system stats
    g_state.update_system_uptime(g_time.millis() / 1000);
    g_state.update_modbus_stats(
        g_modbus->get_transaction_count(),
        g_modbus->get_error_count()
    );
#ifdef ESP32_BUILD
    g_state.update_free_heap(esp_get_free_heap_size());

    // Calculate CPU usage based on actual work time within the 100ms cycle
    static float cpu_usage_filtered = 0.0f;
    uint64_t loop_end_us = esp_timer_get_time();
    uint64_t work_time_us = loop_end_us - loop_start_us;
    // Work time as percentage of 100ms cycle
    float load = (float)work_time_us / 1000.0f;  // Convert to percentage (100ms = 100%)
    if (load > 100) load = 100;
    // Apply EMA filter for smooth display
    cpu_usage_filtered = cpu_usage_filtered * 0.9f + load * 0.1f;
    g_state.update_cpu_usage(cpu_usage_filtered);

    // Timing for periodic updates
    uint32_t now_ms = g_time.millis();

    // Update CPU frequency (once per second is sufficient)
    static uint32_t last_freq_update_ms = 0;
    if (now_ms - last_freq_update_ms >= 1000) {
        uint32_t current_freq_hz = 0;
        esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &current_freq_hz);
        // Configured max from power management settings
        uint32_t max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        g_state.update_cpu_freq(current_freq_hz / 1000000, max_freq_mhz);
        last_freq_update_ms = now_ms;
    }

    // Add CPU history sample every 15 seconds
    static uint32_t last_cpu_sample_ms = 0;
    if (now_ms - last_cpu_sample_ms >= core::CpuHistory::SAMPLE_INTERVAL_MS) {
        g_state.add_cpu_history_sample(cpu_usage_filtered);
        last_cpu_sample_ms = now_ms;
    }

    // Add network history sample every 15 seconds
    static uint32_t last_net_sample_ms = 0;
    if (now_ms - last_net_sample_ms >= core::NetworkHistory::SAMPLE_INTERVAL_MS) {
        bool sampled = false;
#ifdef WIFI_NTP_ENABLED
        if (g_wifi_prov && g_wifi_prov->is_connected()) {
            g_state.sample_network_history(
                g_wifi_prov->get_link_speed_mbps(),
                g_wifi_prov->get_channel()
            );
            sampled = true;
        }
#endif
#ifdef ETHERNET_ENABLED
        // Sample from Ethernet if WiFi not connected (e.g., WiFi in standby)
        if (!sampled && g_ethernet && g_ethernet->is_connected()) {
            auto& info = g_ethernet->get_info();
            g_state.sample_network_history(info.link_speed_mbps, 0);  // Ethernet has no channel
        }
#endif
        last_net_sample_ms = now_ms;
    }
#endif

#ifdef WIFI_NTP_ENABLED
    // Update WiFi and NTP status
    static bool was_connected = false;
    if (g_wifi_prov) {
        bool is_connected = g_wifi_prov->is_connected();
        g_state.update_wifi_rssi(g_wifi_prov->get_rssi());

        // Trigger NTP resync when WiFi reconnects
        if (is_connected && !was_connected && g_ntp) {
            ESP_LOGI("Main", "WiFi reconnected, triggering NTP resync");
            g_ntp->resync();
        }
        was_connected = is_connected;
    }
    if (g_ntp) {
        g_state.update_system_ntp_status(g_ntp->is_synced());
    }
    // 6. Update status LED state (LED task handles the actual updates)
    if (g_status_led) {
        // Check for errors/alarms from safety controller
        g_status_led->set_has_alarms(g_safety->has_active_alarms());
        g_status_led->set_has_errors(g_safety->has_active_errors());
        g_status_led->set_has_warnings(g_safety->has_active_warnings());

        // Check WiFi status
        // Note: Standby counts as "connected" for LED - network is healthy via Ethernet
        if (g_wifi_prov) {
            g_status_led->set_wifi_connected(g_wifi_prov->is_connected() || g_wifi_prov->is_standby());
            g_status_led->set_provisioning(g_wifi_prov->is_provisioning());
            g_status_led->set_ap_client_connected(g_wifi_prov->has_ap_clients());
        }

        // Check NTP status
        if (g_ntp) {
            g_status_led->set_ntp_synced(g_ntp->is_synced());
        }

        // Check SSL certificate generation status
        if (g_http_server) {
            g_status_led->set_cert_generating(g_http_server->is_cert_generating());
        }
        // Note: LED task handles update() automatically
    }
#endif
}

#ifdef ESP32_BUILD
/**
 * FreeRTOS task for control loop
 */
void control_task(void* pvParameters) {
    (void)pvParameters;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(g_config.scheduler.base_cycle_ms);

    while (true) {
        control_loop();
        vTaskDelayUntil(&last_wake, interval);
    }
}

/**
 * ESP32 main entry point
 */
extern "C" void app_main() {
    if (!system_init()) {
        ESP_LOGE("MAIN", "System initialization failed!");
        return;
    }

    // Create control task
    xTaskCreate(
        control_task,
        "control",
        8192,
        nullptr,
        5,  // High priority
        nullptr
    );

    // TODO: Create HTTP server task
    // TODO: Create display update task

    // Main task handles debug console
    while (true) {
#ifdef DEBUG_CONSOLE_ENABLED
        if (g_debug_console) {
            g_debug_console->process();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms for responsive console
    }
}

#else
#ifndef UNIT_TEST
/**
 * Native/Simulator main entry point
 */
int main(int argc, char** argv) {
    printf("Fermentation Controller - Simulator Mode\n");
    printf("========================================\n\n");

    // Check for MODBUS_PORT environment variable
    const char* modbus_port = getenv("MODBUS_PORT");
    if (modbus_port && strlen(modbus_port) > 0) {
        printf("Using serial MODBUS on port: %s\n", modbus_port);
        if (g_serial_modbus.open_port(modbus_port, 57600)) {
            g_modbus = &g_serial_modbus;
            g_use_serial_modbus = true;
        } else {
            printf("WARNING: Failed to open serial port, using simulator\n");
        }
    }

    // Load config from file if provided
    bool config_loaded = false;
    if (argc > 1) {
        FILE* f = fopen(argv[1], "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* json = new char[size + 1];
            fread(json, 1, size, f);
            json[size] = '\0';
            fclose(f);

            if (ConfigLoader::load_from_json(json, g_config)) {
                printf("Loaded config from: %s\n\n", argv[1]);
                config_loaded = true;
            }
            delete[] json;
        }
    }

    if (!system_init(config_loaded)) {
        printf("System initialization failed!\n");
        return 1;
    }

    // Set up simulated sensor values (only when not using serial MODBUS)
    if (!g_use_serial_modbus) {
        // PT1000 Module (address 1): temperatures in 0.1°C
        g_sim_modbus.set_register(1, 0, 120);  // glycol_supply: 12.0°C
        g_sim_modbus.set_register(1, 1, 140);  // glycol_return: 14.0°C
        g_sim_modbus.set_register(1, 2, 185);  // fermenter_1_temp: 18.5°C
        g_sim_modbus.set_register(1, 3, 190);  // fermenter_2_temp: 19.0°C

        // Analog Module (address 2): pressure in 0.001 bar
        g_sim_modbus.set_register(2, 0, 1050); // fermenter_1_pressure: 1.05 bar
        g_sim_modbus.set_register(2, 1, 980);  // fermenter_2_pressure: 0.98 bar
    } else {
        printf("Using external MODBUS simulator for sensor values\n");
    }

    printf("\nStarting control loop (press Ctrl+C to stop)...\n\n");

    // Run control loop
    int cycle = 0;
    while (cycle < 10) {  // Run 10 cycles for demo
        control_loop();

        // Print status every cycle
        printf("Cycle %d:\n", cycle + 1);

        for (uint8_t i = 0; i < g_state.get_sensor_count() && i < 4; i++) {
            auto* sensor = g_state.get_sensor_by_id(i);
            if (sensor) {
                const char* quality_str = "";
                if (sensor->quality == core::SensorQuality::BAD) {
                    quality_str = " [FAULT]";
                } else if (sensor->quality == core::SensorQuality::SUSPECT) {
                    quality_str = " [SUSPECT]";
                }
                printf("  %s: raw=%.3f filtered=%.3f %s%s\n",
                       sensor->name, sensor->raw_value, sensor->filtered_value, sensor->unit, quality_str);
            }
        }

        // Advance simulator time
        g_time.advance_millis(g_config.scheduler.base_cycle_ms);
        g_time.advance_unix_time(1);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cycle++;

#ifdef DEBUG_CONSOLE_ENABLED
        // Process debug console commands
        if (g_debug_console) {
            g_debug_console->process();
        }
#endif
    }

    printf("\nSimulation complete.\n");

    // Cleanup
    delete g_modbus_module;
    delete g_plan_manager;
    delete g_safety;
    delete g_api;
#ifdef DEBUG_CONSOLE_ENABLED
    delete g_debug_console;
#endif

    return 0;
}
#endif // UNIT_TEST
#endif // ESP32_BUILD
