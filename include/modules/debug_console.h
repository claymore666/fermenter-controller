#pragma once

#include "hal/serial_interface.h"
#include "hal/interfaces.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config.h"
#include "modules/safety_controller.h"
#include "modules/fermentation_plan.h"
#include "modules/wifi_provisioning.h"
#include "modules/status_led.h"
#ifdef CAN_ENABLED
#include "modules/can_module.h"
#endif
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include "version.h"

#ifdef ESP32_BUILD
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "hal/esp32/esp32_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#ifdef ETHERNET_ENABLED
#include "hal/esp32/esp32_ethernet.h"
#endif
#ifdef WEBSOCKET_ENABLED
#include "modules/http_server.h"
#endif
#endif

// Version info
#define FIRMWARE_NAME "Fermenter Controller"

namespace modules {

/**
 * Debug console for USB serial monitoring
 * Provides commands to query and control system state
 */
class DebugConsole {
public:
    static constexpr size_t MAX_CMD_LENGTH = 128;
    static constexpr size_t MAX_ARGS = 8;

    DebugConsole(hal::ISerialInterface* serial,
                 hal::ITimeInterface* time,
                 core::StateManager* state,
                 core::EventBus* events,
                 core::SystemConfig* config,
                 SafetyController* safety,
                 FermentationPlanManager* plans,
                 hal::IModbusInterface* modbus,
                 WifiProvisioning* wifi_prov = nullptr,
                 hal::IStorageInterface* storage = nullptr,
                 hal::IGPIOInterface* gpio = nullptr,
                 void* can_module = nullptr,
                 StatusLed* status_led = nullptr,
                 void* ethernet = nullptr)
        : serial_(serial)
        , time_(time)
        , state_(state)
        , events_(events)
        , config_(config)
        , safety_(safety)
        , plans_(plans)
        , modbus_(modbus)
        , wifi_prov_(wifi_prov)
        , storage_(storage)
        , gpio_(gpio)
        , can_module_(can_module)
        , status_led_(status_led)
        , ethernet_(ethernet)
        , http_server_(nullptr)
        , cmd_index_(0)
        , echo_enabled_(true)
        , log_events_(false)
        , log_errors_(true)
        , tls_debug_enabled_(false)
        , last_activity_ms_(0) {
        memset(cmd_buffer_, 0, sizeof(cmd_buffer_));
    }

    /**
     * Initialize the debug console
     * @param baud Baud rate for serial port
     */
    void initialize(uint32_t baud = 115200) {
        serial_->begin(baud);
        print_welcome();
    }

    /**
     * Set HTTP server reference for WebSocket commands
     */
    void set_http_server(void* http_server) {
        http_server_ = http_server;
    }

    /**
     * Process incoming serial data
     * Call this regularly from main loop (non-blocking)
     */
    void process() {
        while (serial_->available() > 0) {
            int c = serial_->read();
            if (c < 0) break;

            // Detect reconnection: print welcome after 30+ seconds of inactivity
            uint32_t now = time_->millis();
            if (last_activity_ms_ > 0 && (now - last_activity_ms_) > 30000) {
                cmd_index_ = 0;  // Clear any partial command
                print_welcome();
            }
            last_activity_ms_ = now;

            if (c == '\r' || c == '\n') {
                if (cmd_index_ > 0) {
                    cmd_buffer_[cmd_index_] = '\0';
                    serial_->println("");
                    execute_command(cmd_buffer_);
                    print_prompt();
                    cmd_index_ = 0;
                }
            } else if (c == '\b' || c == 127) {
                // Backspace
                if (cmd_index_ > 0) {
                    cmd_index_--;
                    if (echo_enabled_) {
                        serial_->print("\b \b");
                    }
                }
            } else if (cmd_index_ < MAX_CMD_LENGTH - 1) {
                cmd_buffer_[cmd_index_++] = c;
                if (echo_enabled_) {
                    char buf[2] = {(char)c, '\0'};
                    serial_->print(buf);
                }
            }
        }
    }

    /**
     * Enable/disable echo
     */
    void set_echo(bool enabled) { echo_enabled_ = enabled; }

private:
    hal::ISerialInterface* serial_;
    hal::ITimeInterface* time_;
    core::StateManager* state_;
    core::EventBus* events_;
    core::SystemConfig* config_;
    SafetyController* safety_;
    FermentationPlanManager* plans_;
    hal::IModbusInterface* modbus_;
    WifiProvisioning* wifi_prov_;
    hal::IStorageInterface* storage_;
    hal::IGPIOInterface* gpio_;
    void* can_module_;  // CANModule* when CAN_ENABLED
    StatusLed* status_led_;
    void* ethernet_;    // ESP32Ethernet* when ETHERNET_ENABLED
    void* http_server_; // HttpServer* when HTTP_ENABLED

    char cmd_buffer_[MAX_CMD_LENGTH];
    size_t cmd_index_;
    bool echo_enabled_;
    bool log_events_;
    bool log_errors_;
    bool tls_debug_enabled_;
    uint32_t last_activity_ms_;

    void print_welcome() {
        serial_->println("");
        serial_->println("================================");
        serial_->print(FIRMWARE_NAME " v" VERSION_STRING "+");
        serial_->println(get_build_hash());
        serial_->println("Debug Console");
        serial_->println("Type 'help' for commands");
        serial_->println("================================");
        print_prompt();
    }

    // Generate 6-char build hash from compile date/time
    static const char* get_build_hash() {
        static char hash[7] = {0};
        if (hash[0] == 0) {
            // Combine date and time strings
            const char* date = __DATE__;  // "Nov 24 2024"
            const char* time = __TIME__;  // "01:55:30"

            // Simple hash using djb2 algorithm
            uint32_t h = 5381;
            for (const char* p = date; *p; p++) {
                h = ((h << 5) + h) ^ *p;
            }
            for (const char* p = time; *p; p++) {
                h = ((h << 5) + h) ^ *p;
            }

            // Convert to base36 (0-9, a-z)
            const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
            for (int i = 5; i >= 0; i--) {
                hash[i] = chars[h % 36];
                h /= 36;
            }
            hash[6] = '\0';
        }
        return hash;
    }

    void print_prompt() {
        serial_->print("> ");
    }

    void printf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        serial_->print(buf);
    }

    void execute_command(const char* cmd) {
        // Parse command and arguments
        char* args[MAX_ARGS];
        int argc = 0;

        // Make a copy for tokenization
        char cmd_copy[MAX_CMD_LENGTH];
        strncpy(cmd_copy, cmd, MAX_CMD_LENGTH - 1);
        cmd_copy[MAX_CMD_LENGTH - 1] = '\0';

        char* token = strtok(cmd_copy, " ");
        while (token && argc < (int)MAX_ARGS) {
            args[argc++] = token;
            token = strtok(nullptr, " ");
        }

        if (argc == 0) return;

        // Route to command handlers
        if (strcmp(args[0], "help") == 0) {
            cmd_help();
        } else if (strcmp(args[0], "status") == 0) {
            cmd_status();
        } else if (strcmp(args[0], "sensors") == 0) {
            cmd_sensors();
        } else if (strcmp(args[0], "sensor") == 0) {
            cmd_sensor(argc, args);
        } else if (strcmp(args[0], "relays") == 0) {
            cmd_relays();
        } else if (strcmp(args[0], "relay") == 0) {
            cmd_relay(argc, args);
        } else if (strcmp(args[0], "fermenters") == 0) {
            cmd_fermenters();
        } else if (strcmp(args[0], "fermenter") == 0) {
            cmd_fermenter(argc, args);
        } else if (strcmp(args[0], "pid") == 0) {
            cmd_pid(argc, args);
        } else if (strcmp(args[0], "alarms") == 0) {
            cmd_alarms();
        } else if (strcmp(args[0], "modbus") == 0) {
            cmd_modbus(argc, args);
        } else if (strcmp(args[0], "heap") == 0) {
            cmd_heap();
        } else if (strcmp(args[0], "uptime") == 0) {
            cmd_uptime();
        } else if (strcmp(args[0], "cpu") == 0) {
            cmd_cpu();
        } else if (strcmp(args[0], "reboot") == 0) {
            cmd_reboot();
        } else if (strcmp(args[0], "shutdown") == 0) {
            cmd_shutdown();
        } else if (strcmp(args[0], "modules") == 0) {
            cmd_modules();
        } else if (strcmp(args[0], "wifi") == 0) {
            cmd_wifi(argc, args);
        } else if (strcmp(args[0], "eth") == 0) {
            cmd_eth(argc, args);
        } else if (strcmp(args[0], "can") == 0) {
            cmd_can(argc, args);
        } else if (strcmp(args[0], "factory") == 0) {
            if (argc >= 2 && strcmp(args[1], "confirm") == 0) {
                confirm_factory_reset();
            } else {
                cmd_factory_reset();
            }
        } else if (strcmp(args[0], "nvs") == 0) {
            cmd_nvs(argc, args);
        } else if (strcmp(args[0], "inputs") == 0) {
            cmd_inputs();
        } else if (strcmp(args[0], "output") == 0) {
            cmd_output(argc, args);
        } else if (strcmp(args[0], "config") == 0) {
            cmd_config();
        } else if (strcmp(args[0], "watch") == 0) {
            cmd_watch(argc, args);
        } else if (strcmp(args[0], "log") == 0) {
            cmd_log(argc, args);
        } else if (strcmp(args[0], "ssl") == 0) {
            cmd_ssl(argc, args);
        } else if (strcmp(args[0], "ws") == 0) {
            cmd_ws(argc, args);
        } else {
            printf("Unknown command: %s\r\n", args[0]);
            serial_->println("Type 'help' for available commands");
        }
    }

    // Command handlers
    void cmd_help() {
        serial_->println("Available commands:");
        serial_->println("");
        serial_->println("  status              - System status overview");
        serial_->println("  uptime              - System uptime");
        serial_->println("  heap                - Memory usage");
        serial_->println("");
        serial_->println("  sensors             - List all sensors");
        serial_->println("  sensor <name>       - Sensor details");
        serial_->println("");
        serial_->println("  relays              - List all relays");
        serial_->println("  relay <name> [on|off] - Relay control");
        serial_->println("");
        serial_->println("  fermenters          - List fermenters");
        serial_->println("  fermenter <id>      - Fermenter details");
        serial_->println("  fermenter <id> setpoint <temp>");
        serial_->println("  fermenter <id> mode <off|manual|plan>");
        serial_->println("");
        serial_->println("  pid <id>            - PID parameters");
        serial_->println("  pid <id> tune <kp> <ki> <kd>");
        serial_->println("");
        serial_->println("  alarms              - Active alarms");
        serial_->println("  modbus stats        - MODBUS statistics");
        serial_->println("  modbus read <addr> <reg> [count]");
        serial_->println("  modbus scan [start] [end] - Scan for devices");
        serial_->println("  modbus autodetect <addr> - Detect PT1000 sensors");
        serial_->println("");
        serial_->println("  wifi                - WiFi status");
        serial_->println("  wifi connect        - Connect to stored network");
        serial_->println("  wifi disconnect     - Disconnect (persistent)");
        serial_->println("  wifi set <ssid> <pass> - Set credentials");
        serial_->println("  wifi clear          - Clear credentials & provision");
        serial_->println("  wifi scan           - Scan for available networks");
        serial_->println("");
        serial_->println("  eth                 - Ethernet status");
        serial_->println("  eth connect         - Start Ethernet interface");
        serial_->println("  eth disconnect      - Stop Ethernet interface");
        serial_->println("");
        serial_->println("  can                 - CAN bus status");
        serial_->println("  can send <id> <data...> - Send CAN message");
        serial_->println("");
        serial_->println("  nvs                 - NVS usage info (use 'nvs' for details)");
        serial_->println("  nvs list            - List NVS namespaces");
        serial_->println("  nvs get <ns>:<key>  - Get value (e.g. wifi:ssid)");
        serial_->println("  nvs set <ns>:<key> <val>");
        serial_->println("  nvs erase <ns>:<key>");
        serial_->println("");
        serial_->println("  ssl status          - SSL certificate status");
        serial_->println("  ssl clear           - Delete cert (regenerates on reboot)");
        serial_->println("  ssl debug [on|off]  - Toggle TLS handshake messages");
        serial_->println("");
        serial_->println("  ws                  - WebSocket status");
        serial_->println("  ws clients          - List connected clients");
        serial_->println("  ws broadcast <msg>  - Send message to all clients");
        serial_->println("");
        serial_->println("  cpu                 - CPU usage percentage");
        serial_->println("  inputs              - Show digital inputs DI1-8");
        serial_->println("  output <1-8> [on|off] - Control digital output");
        serial_->println("  config              - Show loaded configuration");
        serial_->println("  watch <target> [ms] - Continuous monitoring");
        serial_->println("  log [events|errors] [on|off] - Logging settings");
        serial_->println("");
        serial_->println("  modules             - Show compiled modules");
        serial_->println("  factory             - Factory reset (erases all)");
        serial_->println("  reboot              - Restart system");
        serial_->println("  shutdown            - Enter deep sleep (reset to wake)");
    }

    void cmd_status() {
        auto& sys = state_->get_system_state();

        // Header with version and build
        // Build number from date: extract YYMMDD
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

        printf("%s v%s+%02d%02d%02d\r\n",
               FIRMWARE_NAME, VERSION_STRING, year, month, day);
        printf("Built: %s %s\r\n", __DATE__, __TIME__);
        serial_->println("");

        // System info
        serial_->println("System:");
        uint32_t uptime_h = sys.uptime_seconds / 3600;
        uint32_t uptime_m = (sys.uptime_seconds % 3600) / 60;
        uint32_t uptime_s = sys.uptime_seconds % 60;

#ifdef ESP32_BUILD
        // Calculate heap percentage (ESP32-S3 has ~320KB total heap)
        uint32_t total_heap_kb = 320;
        uint32_t free_heap_kb = sys.free_heap / 1024;
        uint32_t heap_percent = (free_heap_kb * 100) / total_heap_kb;

#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
        // Calculate CPU usage from FreeRTOS runtime stats (requires trace facility)
        uint32_t cpu_percent = 0;
        TaskStatus_t* task_array;
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        uint32_t total_runtime;

        task_array = (TaskStatus_t*)pvPortMalloc(task_count * sizeof(TaskStatus_t));
        if (task_array) {
            task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
            if (total_runtime > 0) {
                uint32_t idle_runtime = 0;
                for (UBaseType_t i = 0; i < task_count; i++) {
                    if (strcmp(task_array[i].pcTaskName, "IDLE") == 0 ||
                        strcmp(task_array[i].pcTaskName, "IDLE0") == 0 ||
                        strcmp(task_array[i].pcTaskName, "IDLE1") == 0) {
                        idle_runtime += task_array[i].ulRunTimeCounter;
                    }
                }
                // Calculate CPU % using 64-bit to prevent overflow
                uint64_t idle_pct = (uint64_t)idle_runtime * 100ULL / total_runtime;
                if (idle_pct <= 100) {
                    cpu_percent = 100 - (uint32_t)idle_pct;
                } else {
                    cpu_percent = 0;  // Edge case
                }
            }
            vPortFree(task_array);
        }

        printf("  Uptime: %luh %lum %lus | Heap: %luKB (%lu%%) | CPU: %lu%%\r\n",
               (unsigned long)uptime_h, (unsigned long)uptime_m, (unsigned long)uptime_s,
               (unsigned long)free_heap_kb, (unsigned long)heap_percent, (unsigned long)cpu_percent);
#else
        printf("  Uptime: %luh %lum %lus | Heap: %luKB (%lu%%)\r\n",
               (unsigned long)uptime_h, (unsigned long)uptime_m, (unsigned long)uptime_s,
               (unsigned long)free_heap_kb, (unsigned long)heap_percent);
#endif

        // Date/time if NTP synced
        if (sys.ntp_synced) {
            time_t now;
            time(&now);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
            printf("  Time: %s CET\r\n", time_str);
        }
#else
        printf("  Uptime: %luh %lum %lus | Heap: %luKB\r\n",
               (unsigned long)uptime_h, (unsigned long)uptime_m, (unsigned long)uptime_s,
               (unsigned long)(sys.free_heap / 1024));
#endif

        // Connectivity section
        serial_->println("");
        serial_->println("Connectivity:");

#ifdef ESP32_BUILD
        // I2C status
        if (gpio_) {
            auto* esp_gpio = static_cast<hal::esp32::ESP32GPIO*>(gpio_);
            printf("  I2C (TCA9554) ..... %s\r\n", esp_gpio->check_i2c() ? "OK" : "FAIL");
        }
#endif

        // WiFi
#ifdef WIFI_NTP_ENABLED
        if (sys.wifi_rssi != 0) {
            printf("  WiFi .............. OK (%d dBm)\r\n", sys.wifi_rssi);
            // Show WiFi IP
            if (wifi_prov_) {
                const char* ip = wifi_prov_->get_ip_address();
                if (ip) {
                    printf("    IP: %s\r\n", ip);
                }
            }
        } else if (wifi_prov_ && wifi_prov_->is_standby()) {
            serial_->println("  WiFi .............. HOT STANDBY");
            // Show WiFi IP even in standby (IP is retained)
            const char* ip = wifi_prov_->get_ip_address();
            if (ip) {
                printf("    IP: %s\r\n", ip);
            }
        } else {
            serial_->println("  WiFi .............. DISCONNECTED");
        }

        // NTP
        printf("  NTP ............... %s\r\n", sys.ntp_synced ? "SYNCED" : "NOT SYNCED");
#else
        serial_->println("  WiFi .............. [not compiled]");
#endif

        // Ethernet
#if defined(ESP32_BUILD) && defined(ETHERNET_ENABLED)
        if (ethernet_) {
            auto* eth = static_cast<hal::esp32::ESP32Ethernet*>(ethernet_);
            if (eth->is_connected()) {
                printf("  Ethernet .......... OK (%d Mbps)\r\n", eth->get_link_speed());
                const char* ip = eth->get_ip_address();
                if (ip) {
                    printf("    IP: %s\r\n", ip);
                }
            } else {
                serial_->println("  Ethernet .......... DISCONNECTED");
            }
        }
#endif

        // MODBUS devices
        if (config_) {
            uint16_t dummy;
            for (size_t i = 0; i < config_->hardware.modbus_device_count; i++) {
                const auto& dev = config_->hardware.modbus_devices[i];
                bool online = modbus_->read_holding_registers(dev.address, 0, 1, &dummy);
                printf("  MODBUS @%d ......... %s (%s)\r\n",
                       dev.address, online ? "OK" : "FAIL", dev.name);
            }
        }

#ifdef CAN_ENABLED
        if (can_module_) {
            auto* can = static_cast<CANModule*>(can_module_);
            auto stats = can->get_stats();
            printf("  CAN ............... %s (TX:%lu RX:%lu)\r\n",
                   stats.bus_ok ? "OK" : "ERROR",
                   (unsigned long)stats.tx_count, (unsigned long)stats.rx_count);
        }
#endif

        // Summary
        serial_->println("");
        printf("Sensors: %d | Fermenters: %d\r\n",
               state_->get_sensor_count(), state_->get_fermenter_count());

        if (safety_ && safety_->has_active_alarms()) {
            serial_->println("Alarms: ACTIVE!");
        } else {
            serial_->println("Alarms: none");
        }
    }

    void cmd_sensors() {
        serial_->println("Sensors:");
        uint8_t count = state_->get_sensor_count();

        for (uint8_t i = 0; i < count; i++) {
            auto* sensor = state_->get_sensor_by_id(i);
            if (sensor) {
                const char* quality = "?";
                switch (sensor->quality) {
                    case core::SensorQuality::GOOD: quality = "GOOD"; break;
                    case core::SensorQuality::BAD: quality = "BAD"; break;
                    case core::SensorQuality::SUSPECT: quality = "SUSPECT"; break;
                    case core::SensorQuality::WARMING_UP: quality = "WARMUP"; break;
                    case core::SensorQuality::UNKNOWN: quality = "UNKNOWN"; break;
                }
                printf("  %s: %.3f %s [%s]\r\n",
                       sensor->name, sensor->filtered_value, sensor->unit, quality);
            }
        }
    }

    void cmd_sensor(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage: sensor <name>");
            return;
        }

        auto* sensor = state_->get_sensor(args[1]);
        if (!sensor) {
            printf("Sensor not found: %s\r\n", args[1]);
            return;
        }

        printf("Sensor: %s\r\n", sensor->name);
        printf("  Raw value: %.3f %s\r\n", sensor->raw_value, sensor->unit);
        printf("  Filtered: %.3f %s\r\n", sensor->filtered_value, sensor->unit);
        printf("  Display: %.3f %s\r\n", sensor->display_value, sensor->unit);
        printf("  Quality: %d\r\n", (int)sensor->quality);
        printf("  Filter type: %d\r\n", (int)sensor->filter_type);
        printf("  Alpha: %.2f\r\n", sensor->alpha);
        printf("  Scale: %.6f\r\n", sensor->scale);
        printf("  Last update: %lu ms\r\n", (unsigned long)sensor->timestamp);
    }

    void cmd_relays() {
        serial_->println("Relays:");
        uint8_t count = state_->get_relay_count();

        for (uint8_t i = 0; i < count; i++) {
            auto* relay = state_->get_relay_by_id(i);
            if (relay) {
                printf("  %s: %s\r\n", relay->name, relay->state ? "ON" : "OFF");
            }
        }
    }

    void cmd_relay(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage: relay <name> [on|off]");
            return;
        }

        // Find relay by name
        uint8_t count = state_->get_relay_count();
        core::RelayState* relay = nullptr;
        uint8_t relay_id = 0;

        for (uint8_t i = 0; i < count; i++) {
            auto* r = state_->get_relay_by_id(i);
            if (r && strcmp(r->name, args[1]) == 0) {
                relay = r;
                relay_id = i;
                break;
            }
        }

        if (!relay) {
            printf("Relay not found: %s\r\n", args[1]);
            return;
        }

        if (argc == 2) {
            // Show relay status
            printf("Relay: %s\r\n", relay->name);
            printf("  State: %s\r\n", relay->state ? "ON" : "OFF");
            printf("  Last change: %lu ms\r\n", (unsigned long)relay->last_change);
        } else {
            // Set relay state
            bool new_state = (strcmp(args[2], "on") == 0 || strcmp(args[2], "1") == 0);
            state_->set_relay_state(relay_id, new_state, time_->millis());
            printf("Relay %s set to %s\r\n", relay->name, new_state ? "ON" : "OFF");
        }
    }

    void cmd_fermenters() {
        serial_->println("Fermenters:");

        for (uint8_t i = 1; i <= core::MAX_FERMENTERS; i++) {
            auto* ferm = state_->get_fermenter(i);
            if (ferm && ferm->id != 0) {
                const char* mode = "?";
                switch (ferm->mode) {
                    case core::FermenterMode::OFF: mode = "OFF"; break;
                    case core::FermenterMode::MANUAL: mode = "MANUAL"; break;
                    case core::FermenterMode::PLAN: mode = "PLAN"; break;
                    case core::FermenterMode::AUTOTUNE: mode = "AUTOTUNE"; break;
                }
                printf("  %s: %.1fC -> %.1fC [%s] PID=%.0f%%\r\n",
                       ferm->name, ferm->current_temp, ferm->target_temp, mode, ferm->pid_output);
            }
        }
    }

    void cmd_fermenter(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage: fermenter <id> [setpoint <temp>|mode <off|manual|plan>]");
            return;
        }

        uint8_t id = atoi(args[1]);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            printf("Fermenter not found: %d\r\n", id);
            return;
        }

        if (argc == 2) {
            // Show fermenter details
            printf("Fermenter %d: %s\r\n", id, ferm->name);
            printf("  Temperature: %.2f C (target: %.2f C)\r\n", ferm->current_temp, ferm->target_temp);
            printf("  Pressure: %.3f bar (target: %.3f bar)\r\n", ferm->current_pressure, ferm->target_pressure);
            printf("  Mode: %d\r\n", (int)ferm->mode);
            printf("  PID output: %.1f%%\r\n", ferm->pid_output);
            printf("  Plan active: %s\r\n", ferm->plan_active ? "yes" : "no");
            if (ferm->plan_active) {
                printf("  Current step: %d\r\n", ferm->current_step);
                printf("  Hours remaining: %.1f\r\n", ferm->hours_remaining);
            }
        } else if (argc >= 4 && strcmp(args[2], "setpoint") == 0) {
            float temp = atof(args[3]);
            ferm->target_temp = temp;
            printf("Fermenter %d setpoint set to %.1f C\r\n", id, temp);
        } else if (argc >= 4 && strcmp(args[2], "mode") == 0) {
            if (strcmp(args[3], "off") == 0) {
                ferm->mode = core::FermenterMode::OFF;
            } else if (strcmp(args[3], "manual") == 0) {
                ferm->mode = core::FermenterMode::MANUAL;
            } else if (strcmp(args[3], "plan") == 0) {
                ferm->mode = core::FermenterMode::PLAN;
            }
            printf("Fermenter %d mode set to %s\r\n", id, args[3]);
        }
    }

    void cmd_pid(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage: pid <fermenter_id> [tune <kp> <ki> <kd>]");
            return;
        }

        uint8_t id = atoi(args[1]);
        auto* ferm = state_->get_fermenter(id);

        if (!ferm || ferm->id == 0) {
            printf("Fermenter not found: %d\r\n", id);
            return;
        }

        if (argc == 2) {
            // Show PID parameters
            printf("PID for fermenter %d:\r\n", id);
            printf("  Kp: %.3f\r\n", ferm->pid_params.kp);
            printf("  Ki: %.3f\r\n", ferm->pid_params.ki);
            printf("  Kd: %.3f\r\n", ferm->pid_params.kd);
            printf("  Output: %.1f%% (min: %.0f, max: %.0f)\r\n",
                   ferm->pid_output, ferm->pid_params.output_min, ferm->pid_params.output_max);
            printf("  Integral: %.3f\r\n", ferm->pid_integral);
            printf("  Last error: %.3f\r\n", ferm->pid_last_error);
        } else if (argc >= 5 && strcmp(args[2], "tune") == 0) {
            ferm->pid_params.kp = atof(args[3]);
            ferm->pid_params.ki = atof(args[4]);
            ferm->pid_params.kd = argc > 5 ? atof(args[5]) : ferm->pid_params.kd;
            printf("PID tuned: Kp=%.3f Ki=%.3f Kd=%.3f\r\n",
                   ferm->pid_params.kp, ferm->pid_params.ki, ferm->pid_params.kd);
        }
    }

    void cmd_alarms() {
        if (!safety_) {
            serial_->println("Safety controller not available");
            return;
        }

        bool any_alarms = false;
        serial_->println("Alarms:");

        for (uint8_t i = 1; i <= core::MAX_FERMENTERS; i++) {
            auto* alarm = safety_->get_alarm_state(i);
            if (alarm) {
                if (alarm->temp_high_alarm || alarm->temp_low_alarm ||
                    alarm->pressure_high_alarm || alarm->sensor_failure_alarm) {
                    any_alarms = true;
                    printf("  Fermenter %d:\r\n", i);
                    if (alarm->temp_high_alarm) serial_->println("    - Temperature HIGH");
                    if (alarm->temp_low_alarm) serial_->println("    - Temperature LOW");
                    if (alarm->pressure_high_alarm) serial_->println("    - Pressure HIGH");
                    if (alarm->sensor_failure_alarm) serial_->println("    - Sensor FAILURE");
                }
            }
        }

        if (!any_alarms) {
            serial_->println("  No active alarms");
        }
    }

    void cmd_modbus(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage:");
            serial_->println("  modbus stats");
            serial_->println("  modbus read <addr> <reg> [count]");
            serial_->println("  modbus scan [start] [end]");
            serial_->println("  modbus autodetect <addr>");
            return;
        }

        if (strcmp(args[1], "stats") == 0) {
            auto& sys = state_->get_system_state();
            printf("MODBUS Statistics:\r\n");
            printf("  Transactions: %lu\r\n", (unsigned long)sys.modbus_transactions);
            printf("  Errors: %lu\r\n", (unsigned long)sys.modbus_errors);
            if (sys.modbus_transactions > 0) {
                float error_rate = 100.0f * sys.modbus_errors / sys.modbus_transactions;
                printf("  Error rate: %.2f%%\r\n", error_rate);
            }
        } else if (strcmp(args[1], "read") == 0 && argc >= 4) {
            uint8_t addr = atoi(args[2]);
            uint16_t reg = atoi(args[3]);
            uint16_t count = argc > 4 ? atoi(args[4]) : 1;

            uint16_t values[16];
            if (count > 16) count = 16;

            if (modbus_->read_holding_registers(addr, reg, count, values)) {
                printf("Read from %d reg %d:\r\n", addr, reg);
                for (uint16_t i = 0; i < count; i++) {
                    printf("  [%d]: %d (0x%04X)\r\n", reg + i, values[i], values[i]);
                }
            } else {
                serial_->println("MODBUS read failed");
            }
        } else if (strcmp(args[1], "scan") == 0) {
            // Scan for MODBUS devices
            uint8_t start_addr = argc > 2 ? atoi(args[2]) : 1;
            uint8_t end_addr = argc > 3 ? atoi(args[3]) : 10;

            if (end_addr > 247) end_addr = 247;
            if (start_addr < 1) start_addr = 1;

            printf("Scanning MODBUS addresses %d to %d...\r\n", start_addr, end_addr);

            int found = 0;
            uint16_t dummy;
            for (uint8_t addr = start_addr; addr <= end_addr; addr++) {
                if (modbus_->read_holding_registers(addr, 0, 1, &dummy)) {
                    printf("  Found device at address %d\r\n", addr);
                    found++;
                }
            }

            if (found == 0) {
                serial_->println("  No devices found");
            } else {
                printf("Found %d device(s)\r\n", found);
            }
        } else if (strcmp(args[1], "autodetect") == 0 && argc >= 3) {
            // Autodetect sensors on PT1000 module
            uint8_t addr = atoi(args[2]);

            printf("Autodetecting sensors on address %d...\r\n", addr);

            // Read all 8 channels
            uint16_t values[8];
            if (!modbus_->read_holding_registers(addr, 0, 8, values)) {
                serial_->println("Failed to read from device");
                return;
            }

            // PT1000 thresholds (5-95% of -1800 to +6500 range)
            const int16_t PT1000_LOW = -1385;   // -138.5°C
            const int16_t PT1000_HIGH = 6085;   // +608.5°C

            printf("PT1000 Sensor Detection (valid: %.1f to %.1f C):\r\n",
                   PT1000_LOW * 0.1f, PT1000_HIGH * 0.1f);

            int valid_count = 0;
            for (int i = 0; i < 8; i++) {
                int16_t raw = (int16_t)values[i];
                float temp = raw * 0.1f;

                bool valid = (raw >= PT1000_LOW && raw <= PT1000_HIGH);

                if (valid) {
                    printf("  CH%d: %.1f C [VALID]\r\n", i + 1, temp);
                    valid_count++;
                } else if (raw > PT1000_HIGH) {
                    printf("  CH%d: %.1f C [OPEN - no sensor]\r\n", i + 1, temp);
                } else {
                    printf("  CH%d: %.1f C [SHORT - fault]\r\n", i + 1, temp);
                }
            }

            printf("Detected %d valid sensor(s)\r\n", valid_count);
        }
    }

    void cmd_heap() {
        auto& sys = state_->get_system_state();
        printf("Free heap: %lu bytes\r\n", (unsigned long)sys.free_heap);
    }

    void cmd_uptime() {
        auto& sys = state_->get_system_state();
        uint32_t secs = sys.uptime_seconds;
        uint32_t days = secs / 86400;
        uint32_t hours = (secs % 86400) / 3600;
        uint32_t mins = (secs % 3600) / 60;
        uint32_t s = secs % 60;

        printf("Uptime: %lud %02lu:%02lu:%02lu\r\n",
               (unsigned long)days, (unsigned long)hours,
               (unsigned long)mins, (unsigned long)s);
    }

    void cmd_cpu() {
        auto& sys = state_->get_system_state();

#ifdef ESP32_BUILD
        // Get CPU frequency
        printf("CPU Frequency: %lu MHz (max: %lu MHz)\r\n",
               (unsigned long)sys.cpu_freq_mhz,
               (unsigned long)sys.cpu_freq_max_mhz);

#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
        // Calculate CPU usage from FreeRTOS runtime stats
        uint32_t cpu_percent = 0;
        TaskStatus_t* task_array;
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        uint32_t total_runtime;

        task_array = (TaskStatus_t*)pvPortMalloc(task_count * sizeof(TaskStatus_t));
        if (task_array) {
            task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
            if (total_runtime > 0) {
                uint32_t idle_runtime = 0;
                for (UBaseType_t i = 0; i < task_count; i++) {
                    if (strcmp(task_array[i].pcTaskName, "IDLE") == 0 ||
                        strcmp(task_array[i].pcTaskName, "IDLE0") == 0 ||
                        strcmp(task_array[i].pcTaskName, "IDLE1") == 0) {
                        idle_runtime += task_array[i].ulRunTimeCounter;
                    }
                }
                uint64_t idle_pct = (uint64_t)idle_runtime * 100ULL / total_runtime;
                if (idle_pct <= 100) {
                    cpu_percent = 100 - (uint32_t)idle_pct;
                }
            }
            vPortFree(task_array);
        }

        printf("CPU Usage: %lu%%\r\n", (unsigned long)cpu_percent);
        printf("Tasks: %lu\r\n", (unsigned long)task_count);
#else
        printf("CPU Usage: %.1f%% (from state manager)\r\n", sys.cpu_usage);
#endif

#else
        // Simulator
        printf("CPU Usage: %.1f%%\r\n", sys.cpu_usage);
        printf("CPU Frequency: %lu MHz\r\n", (unsigned long)sys.cpu_freq_mhz);
#endif
    }

    void cmd_modules() {
        serial_->println("Compiled Modules:");

#ifdef WIFI_NTP_ENABLED
        serial_->println("  WiFi ............... [x]");
        serial_->println("  NTP ................ [x]");
#else
        serial_->println("  WiFi ............... [ ]");
        serial_->println("  NTP ................ [ ]");
#endif

#ifdef HTTP_ENABLED
        serial_->println("  HTTP Server ........ [x]");
#else
        serial_->println("  HTTP Server ........ [ ]");
#endif

#ifdef MQTT_ENABLED
        serial_->println("  MQTT Client ........ [x]");
#else
        serial_->println("  MQTT Client ........ [ ]");
#endif

#ifdef CAN_ENABLED
        serial_->println("  CAN Bus ............ [x]");
#else
        serial_->println("  CAN Bus ............ [ ]");
#endif

#ifdef DEBUG_CONSOLE_ENABLED
        serial_->println("  Debug Console ...... [x]");
#else
        serial_->println("  Debug Console ...... [ ]");
#endif
    }

    void cmd_wifi(int argc, char** args) {
        if (!wifi_prov_) {
            serial_->println("WiFi provisioning not available");
            return;
        }

        if (argc == 1) {
            // Show WiFi status
            serial_->println("WiFi Status:");
            printf("  State: %s\r\n", wifi_prov_->get_state_string());

            if (wifi_prov_->is_connected()) {
                printf("  IP: %s\r\n", wifi_prov_->get_ip_address());
                printf("  SSID: %s\r\n", wifi_prov_->get_ssid());
            } else if (wifi_prov_->is_provisioning()) {
                printf("  AP IP: %s\r\n", wifi_prov_->get_ap_ip_address());
                serial_->println("  SmartConfig: Listening...");
                serial_->println("  Use ESP-Touch app or connect to AP");
            }

            const char* method = "NONE";
            switch (wifi_prov_->get_provision_method()) {
                case WifiProvisioning::ProvisionMethod::STORED: method = "STORED"; break;
                case WifiProvisioning::ProvisionMethod::SMARTCONFIG: method = "SMARTCONFIG"; break;
                case WifiProvisioning::ProvisionMethod::CAPTIVE_PORTAL: method = "CAPTIVE_PORTAL"; break;
                case WifiProvisioning::ProvisionMethod::WPS: method = "WPS"; break;
                default: break;
            }
            printf("  Method: %s\r\n", method);
        } else if (strcmp(args[1], "set") == 0 && argc >= 4) {
            serial_->println("Setting WiFi credentials...");
            if (wifi_prov_->provision(args[2], args[3])) {
                printf("Connected to %s\r\n", args[2]);
            } else {
                printf("Failed to connect to %s\r\n", args[2]);
            }
        } else if (strcmp(args[1], "clear") == 0) {
            wifi_prov_->clear_credentials();
            serial_->println("WiFi credentials cleared");
            serial_->println("Starting provisioning mode...");
            wifi_prov_->start();  // Start provisioning immediately
        } else if (strcmp(args[1], "connect") == 0) {
            serial_->println("Connecting with stored credentials...");
            wifi_prov_->set_auto_connect(true);
            if (wifi_prov_->start()) {
                serial_->println("Connected!");
            } else {
                serial_->println("Connection failed");
            }
        } else if (strcmp(args[1], "disconnect") == 0) {
            wifi_prov_->disconnect();
            serial_->println("WiFi disconnected (will stay disconnected on reboot)");
        } else if (strcmp(args[1], "scan") == 0) {
#ifdef ESP32_BUILD
            serial_->println("Scanning for WiFi networks...");

            // Configure scan - passive scan is faster and doesn't require TX
            wifi_scan_config_t scan_config = {};
            scan_config.ssid = nullptr;
            scan_config.bssid = nullptr;
            scan_config.channel = 0;  // All channels
            scan_config.show_hidden = false;
            scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
            scan_config.scan_time.active.min = 100;
            scan_config.scan_time.active.max = 300;

            esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // Blocking scan
            if (err != ESP_OK) {
                printf("Scan failed: %s\r\n", esp_err_to_name(err));
                return;
            }

            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);

            if (ap_count == 0) {
                serial_->println("No networks found");
                return;
            }

            // Limit to 20 networks max
            if (ap_count > 20) ap_count = 20;

            wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(ap_count * sizeof(wifi_ap_record_t));
            if (!ap_records) {
                serial_->println("Memory allocation failed");
                return;
            }

            esp_wifi_scan_get_ap_records(&ap_count, ap_records);

            printf("Found %d networks:\r\n", ap_count);
            for (int i = 0; i < ap_count; i++) {
                const char* auth_str = "UNKNOWN";
                switch (ap_records[i].authmode) {
                    case WIFI_AUTH_OPEN: auth_str = "OPEN"; break;
                    case WIFI_AUTH_WEP: auth_str = "WEP"; break;
                    case WIFI_AUTH_WPA_PSK: auth_str = "WPA"; break;
                    case WIFI_AUTH_WPA2_PSK: auth_str = "WPA2"; break;
                    case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
                    case WIFI_AUTH_WPA3_PSK: auth_str = "WPA3"; break;
                    case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "WPA2/WPA3"; break;
                    default: break;
                }
                printf("  %s (%d dBm) %s\r\n",
                       ap_records[i].ssid,
                       ap_records[i].rssi,
                       auth_str);
            }

            free(ap_records);
#else
            serial_->println("WiFi scan only available on ESP32");
#endif
        } else {
            serial_->println("Usage: wifi [connect|disconnect|scan|set <ssid> <pass>|clear]");
        }
    }

    void cmd_eth(int argc, char** args) {
#if defined(ESP32_BUILD) && defined(ETHERNET_ENABLED)
        if (!ethernet_) {
            serial_->println("Ethernet not available");
            return;
        }

        auto* eth = static_cast<hal::esp32::ESP32Ethernet*>(ethernet_);

        if (argc >= 2) {
            if (strcmp(args[1], "connect") == 0) {
                serial_->println("Starting Ethernet...");
                if (eth->start()) {
                    serial_->println("Ethernet started. Waiting for DHCP...");
                    if (eth->wait_for_connection(10000)) {
                        printf("Connected! IP: %s\r\n", eth->get_ip_address());
                    } else {
                        serial_->println("Timeout waiting for DHCP. Check cable.");
                    }
                } else {
                    serial_->println("Failed to start Ethernet");
                }
                return;
            } else if (strcmp(args[1], "disconnect") == 0) {
                serial_->println("Stopping Ethernet...");
                eth->stop();
                serial_->println("Ethernet stopped");
                return;
            }
        }

        // Default: show status
        serial_->println("Ethernet Status:");
        printf("  Connected: %s\r\n", eth->is_connected() ? "Yes" : "No");

        if (eth->is_connected()) {
            printf("  IP: %s\r\n", eth->get_ip_address());
            printf("  Netmask: %s\r\n", eth->get_netmask());
            printf("  Gateway: %s\r\n", eth->get_gateway());
            printf("  Speed: %d Mbps\r\n", eth->get_link_speed());

            auto& info = eth->get_info();
            printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   info.mac[0], info.mac[1], info.mac[2],
                   info.mac[3], info.mac[4], info.mac[5]);
        } else {
            serial_->println("  Waiting for link...");
        }

        serial_->println("");
        serial_->println("Commands: eth connect | eth disconnect");
#else
        (void)argc;
        (void)args;
        serial_->println("Ethernet not enabled in this build");
#endif
    }

    void cmd_can(int argc, char** args) {
#ifdef CAN_ENABLED
        if (!can_module_) {
            serial_->println("CAN module not available");
            return;
        }

        auto* can = static_cast<CANModule*>(can_module_);

        if (argc == 1) {
            // Show CAN status
            serial_->println("CAN Bus Status:");
            printf("  Initialized: %s\r\n", can->is_initialized() ? "Yes" : "No");
            printf("  Bitrate: %lu bps\r\n", can->get_bitrate());

            auto stats = can->get_stats();
            printf("  TX: %lu\r\n", stats.tx_count);
            printf("  RX: %lu\r\n", stats.rx_count);
            printf("  Errors: %lu\r\n", stats.error_count);
            printf("  Bus: %s\r\n", stats.bus_ok ? "OK" : "ERROR");
        } else if (strcmp(args[1], "send") == 0 && argc >= 3) {
            // Send CAN message: can send <id> <data bytes...>
            uint32_t id = strtoul(args[2], nullptr, 0);
            uint8_t data[8] = {0};
            uint8_t len = 0;

            for (int i = 3; i < argc && len < 8; i++) {
                data[len++] = (uint8_t)strtoul(args[i], nullptr, 0);
            }

            if (can->send(id, data, len)) {
                printf("Sent CAN ID 0x%03lX [%d bytes]\r\n", id, len);
            } else {
                serial_->println("Failed to send CAN message");
            }
        } else {
            serial_->println("Usage: can [send <id> <data...>]");
        }
#else
        (void)argc;
        (void)args;
        serial_->println("CAN not enabled in this build");
#endif
    }

    void cmd_nvs(int argc, char** args) {
#ifdef ESP32_BUILD
        if (argc < 2) {
            serial_->println("Usage:");
            serial_->println("  nvs list                    - Show namespaces");
            serial_->println("  nvs list <namespace>        - Show keys in namespace");
            serial_->println("  nvs get <ns>:<key>          - Read value");
            serial_->println("  nvs set <ns>:<key> <value>  - Write value");
            serial_->println("  nvs erase <ns>:<key>        - Delete key");
            serial_->println("");
            serial_->println("Examples:");
            serial_->println("  nvs list wifi");
            serial_->println("  nvs get wifi:ssid");
            serial_->println("  nvs set config:brightness 50");
            return;
        }

        if (strcmp(args[1], "list") == 0) {
            if (argc >= 3) {
                // List keys in specific namespace: nvs list <namespace>
                const char* ns = args[2];
                printf("Keys in namespace '%s':\r\n", ns);

                nvs_iterator_t it = nullptr;
                esp_err_t err = nvs_entry_find("nvs", ns, NVS_TYPE_ANY, &it);
                if (err == ESP_ERR_NVS_NOT_FOUND || it == nullptr) {
                    printf("  (no keys found or namespace doesn't exist)\r\n");
                } else {
                    int count = 0;
                    while (err == ESP_OK && it != nullptr) {
                        nvs_entry_info_t info;
                        nvs_entry_info(it, &info);

                        const char* type_str = "?";
                        switch (info.type) {
                            case NVS_TYPE_U8:  type_str = "u8"; break;
                            case NVS_TYPE_I8:  type_str = "i8"; break;
                            case NVS_TYPE_U16: type_str = "u16"; break;
                            case NVS_TYPE_I16: type_str = "i16"; break;
                            case NVS_TYPE_U32: type_str = "u32"; break;
                            case NVS_TYPE_I32: type_str = "i32"; break;
                            case NVS_TYPE_U64: type_str = "u64"; break;
                            case NVS_TYPE_I64: type_str = "i64"; break;
                            case NVS_TYPE_STR: type_str = "string"; break;
                            case NVS_TYPE_BLOB: type_str = "blob"; break;
                            default: type_str = "unknown"; break;
                        }
                        printf("  %s (%s)\r\n", info.key, type_str);
                        count++;

                        err = nvs_entry_next(&it);
                    }
                    if (count == 0) {
                        serial_->println("  (empty)");
                    }
                }
                if (it != nullptr) {
                    nvs_release_iterator(it);
                }
            } else {
                // List namespaces (known ones)
                serial_->println("NVS namespaces:");
                serial_->println("  wifi       - WiFi credentials");
                serial_->println("  fermenter  - SSL certs, auth, plans");
                serial_->println("  nvs.net80211 - WiFi driver state");
                serial_->println("");
                serial_->println("Use 'nvs list <namespace>' to show keys");
            }
        } else if (strcmp(args[1], "get") == 0) {
            if (argc < 3) {
                serial_->println("Usage: nvs get <namespace>:<key>");
                serial_->println("Example: nvs get wifi:ssid");
                return;
            }
            // Parse namespace:key format
            char ns[16];
            char key[32];
            char* colon = strchr(args[2], ':');
            if (!colon) {
                serial_->println("Error: Use <namespace>:<key> format");
                serial_->println("Example: nvs get wifi:ssid");
                return;
            }
            size_t ns_len = colon - args[2];
            if (ns_len > sizeof(ns) - 1) ns_len = sizeof(ns) - 1;
            strncpy(ns, args[2], ns_len);
            ns[ns_len] = '\0';
            strncpy(key, colon + 1, sizeof(key) - 1);
            key[sizeof(key) - 1] = '\0';

            nvs_handle_t nvs;
            if (nvs_open(ns, NVS_READONLY, &nvs) == ESP_OK) {
                // Try string first
                char value[128];
                size_t len = sizeof(value);
                if (nvs_get_str(nvs, key, value, &len) == ESP_OK) {
                    printf("%s:%s = \"%s\"\r\n", ns, key, value);
                } else {
                    // Try integer
                    int32_t ival;
                    if (nvs_get_i32(nvs, key, &ival) == ESP_OK) {
                        printf("%s:%s = %ld\r\n", ns, key, (long)ival);
                    } else {
                        printf("Key not found: %s:%s\r\n", ns, key);
                    }
                }
                nvs_close(nvs);
            } else {
                printf("Failed to open namespace: %s\r\n", ns);
            }
        } else if (strcmp(args[1], "set") == 0) {
            if (argc < 4) {
                serial_->println("Usage: nvs set <namespace>:<key> <value>");
                serial_->println("Example: nvs set config:brightness 50");
                return;
            }
            char ns[16];
            char key[32];
            char* colon = strchr(args[2], ':');
            if (!colon) {
                serial_->println("Error: Use <namespace>:<key> format");
                serial_->println("Example: nvs set wifi:ssid MyNetwork");
                return;
            }
            size_t ns_len = colon - args[2];
            if (ns_len > sizeof(ns) - 1) ns_len = sizeof(ns) - 1;
            strncpy(ns, args[2], ns_len);
            ns[ns_len] = '\0';
            strncpy(key, colon + 1, sizeof(key) - 1);
            key[sizeof(key) - 1] = '\0';

            nvs_handle_t nvs;
            if (nvs_open(ns, NVS_READWRITE, &nvs) == ESP_OK) {
                // Check if value is numeric
                char* endptr;
                long ival = strtol(args[3], &endptr, 10);
                if (*endptr == '\0') {
                    nvs_set_i32(nvs, key, (int32_t)ival);
                    printf("Set %s:%s = %ld\r\n", ns, key, ival);
                } else {
                    nvs_set_str(nvs, key, args[3]);
                    printf("Set %s:%s = \"%s\"\r\n", ns, key, args[3]);
                }
                nvs_commit(nvs);
                nvs_close(nvs);
            } else {
                printf("Failed to open namespace: %s\r\n", ns);
            }
        } else if (strcmp(args[1], "erase") == 0) {
            if (argc < 3) {
                serial_->println("Usage: nvs erase <namespace>:<key>");
                serial_->println("Example: nvs erase wifi:password");
                return;
            }
            char ns[16];
            char key[32];
            char* colon = strchr(args[2], ':');
            if (!colon) {
                serial_->println("Error: Use <namespace>:<key> format");
                serial_->println("Example: nvs erase wifi:password");
                return;
            }
            size_t ns_len = colon - args[2];
            if (ns_len > sizeof(ns) - 1) ns_len = sizeof(ns) - 1;
            strncpy(ns, args[2], ns_len);
            ns[ns_len] = '\0';
            strncpy(key, colon + 1, sizeof(key) - 1);
            key[sizeof(key) - 1] = '\0';

            nvs_handle_t nvs;
            if (nvs_open(ns, NVS_READWRITE, &nvs) == ESP_OK) {
                if (nvs_erase_key(nvs, key) == ESP_OK) {
                    nvs_commit(nvs);
                    printf("Erased %s:%s\r\n", ns, key);
                } else {
                    printf("Key not found: %s:%s\r\n", ns, key);
                }
                nvs_close(nvs);
            }
        }
#else
        (void)argc;
        (void)args;
        serial_->println("NVS commands only available on ESP32");
#endif
    }

    void cmd_inputs() {
        serial_->println("Digital Inputs:");

        if (!gpio_) {
            serial_->println("  GPIO interface not available");
            return;
        }

        for (uint8_t i = 0; i < 8; i++) {
            bool state = gpio_->get_digital_input(i);
            printf("  DI%d: %s\r\n", i + 1, state ? "HIGH" : "LOW");
        }
    }

    void cmd_output(int argc, char** args) {
        if (!gpio_) {
            serial_->println("GPIO interface not available");
            return;
        }

        if (argc < 2) {
            // Show all outputs
            serial_->println("Digital Outputs:");
            for (uint8_t i = 0; i < 8; i++) {
                bool state = gpio_->get_relay_state(i);
                printf("  DO%d: %s\r\n", i + 1, state ? "ON" : "OFF");
            }
            serial_->println("");
            serial_->println("Usage: output <1-8> [on|off]");
            return;
        }

        uint8_t num = atoi(args[1]);
        if (num < 1 || num > 8) {
            serial_->println("Invalid output number (1-8)");
            return;
        }

        uint8_t idx = num - 1;

        if (argc < 3) {
            // Show single output state
            bool state = gpio_->get_relay_state(idx);
            printf("DO%d: %s\r\n", num, state ? "ON" : "OFF");
            return;
        }

        // Set output
        bool on = (strcmp(args[2], "on") == 0 || strcmp(args[2], "1") == 0);
        gpio_->set_relay(idx, on);
        printf("DO%d: %s\r\n", num, on ? "ON" : "OFF");
    }

    void cmd_config() {
        serial_->println("System Configuration:");

        if (!config_) {
            serial_->println("  Config not loaded");
            return;
        }

        printf("  Fermenters: %d\r\n", config_->fermenter_count);
        printf("  MODBUS devices: %d\r\n", config_->hardware.modbus_device_count);
        printf("  GPIO relays: %d\r\n", config_->hardware.gpio_relay_count);

        serial_->println("");
        serial_->println("Timing:");
        printf("  MODBUS poll: %lu ms\r\n", (unsigned long)config_->modbus_timing.poll_interval_ms);
        printf("  PID interval: %lu ms\r\n", (unsigned long)config_->pid_timing.calculation_interval_ms);
        printf("  Safety check: %lu ms\r\n", (unsigned long)config_->safety_timing.check_interval_ms);

        serial_->println("");
        serial_->println("MODBUS Devices:");
        for (uint8_t i = 0; i < config_->hardware.modbus_device_count && i < 8; i++) {
            auto& d = config_->hardware.modbus_devices[i];
            printf("  %s: addr=%d\r\n", d.name, d.address);
        }

        serial_->println("");
        serial_->println("Relays:");
        for (uint8_t i = 0; i < config_->hardware.gpio_relay_count && i < 8; i++) {
            auto& r = config_->hardware.gpio_relays[i];
            printf("  %s: pin=%d\r\n", r.name, r.pin);
        }

        serial_->println("");
        serial_->println("Fermenters:");
        for (uint8_t i = 0; i < config_->fermenter_count && i < core::MAX_FERMENTERS; i++) {
            auto& f = config_->fermenters[i];
            printf("  %s: id=%d\r\n", f.name, f.id);
        }
    }

    void cmd_watch(int argc, char** args) {
        if (argc < 2) {
            serial_->println("Usage: watch sensors|inputs|fermenter <id> [interval_ms]");
            serial_->println("Press Enter to stop");
            return;
        }

        uint32_t interval = 1000;  // Default 1 second

        if (strcmp(args[1], "sensors") == 0) {
            if (argc > 2) interval = atoi(args[2]);
            watch_sensors(interval);
        } else if (strcmp(args[1], "inputs") == 0) {
            if (argc > 2) interval = atoi(args[2]);
            watch_inputs(interval);
        } else if (strcmp(args[1], "fermenter") == 0 && argc >= 3) {
            uint8_t id = atoi(args[2]);
            if (argc > 3) interval = atoi(args[3]);
            watch_fermenter(id, interval);
        } else {
            serial_->println("Unknown watch target");
        }
    }

    void watch_sensors(uint32_t interval) {
        serial_->println("Watching sensors (Enter to stop)...");
        serial_->println("");

        while (true) {
            // Check for keypress to stop
            if (serial_->available() > 0) {
                serial_->read();  // Consume the character
                break;
            }

            // Print sensor values
            uint8_t count = state_->get_sensor_count();
            for (uint8_t i = 0; i < count; i++) {
                auto* sensor = state_->get_sensor_by_id(i);
                if (sensor) {
                    printf("%s=%.2f ", sensor->name, sensor->filtered_value);
                }
            }
            serial_->println("");

            time_->delay_ms(interval);
        }
        serial_->println("Stopped");
    }

    void watch_inputs(uint32_t interval) {
        if (!gpio_) {
            serial_->println("GPIO interface not available");
            return;
        }

        serial_->println("Watching inputs (Enter to stop)...");
        serial_->println("");

        while (true) {
            if (serial_->available() > 0) {
                serial_->read();
                break;
            }

            serial_->print("DI: ");
            for (uint8_t i = 0; i < 8; i++) {
                serial_->print(gpio_->get_digital_input(i) ? "1" : "0");
            }
            serial_->println("");

            time_->delay_ms(interval);
        }
        serial_->println("Stopped");
    }

    void watch_fermenter(uint8_t id, uint32_t interval) {
        auto* ferm = state_->get_fermenter(id);
        if (!ferm || ferm->id == 0) {
            printf("Fermenter not found: %d\r\n", id);
            return;
        }

        printf("Watching fermenter %d (Enter to stop)...\r\n", id);
        serial_->println("");

        while (true) {
            if (serial_->available() > 0) {
                serial_->read();
                break;
            }

            printf("T=%.2f->%.2f P=%.3f PID=%.1f%%\r\n",
                   ferm->current_temp, ferm->target_temp,
                   ferm->current_pressure, ferm->pid_output);

            time_->delay_ms(interval);
        }
        serial_->println("Stopped");
    }

    void cmd_log(int argc, char** args) {
        if (argc == 1) {
            // Show current log settings
            serial_->println("Log Settings:");
            printf("  Events: %s\r\n", log_events_ ? "ON" : "OFF");
            printf("  Errors: %s\r\n", log_errors_ ? "ON" : "OFF");
            return;
        }

        if (argc >= 3) {
            bool enable = (strcmp(args[2], "on") == 0 || strcmp(args[2], "1") == 0);

            if (strcmp(args[1], "events") == 0) {
                log_events_ = enable;
                printf("Event logging: %s\r\n", enable ? "ON" : "OFF");
            } else if (strcmp(args[1], "errors") == 0) {
                log_errors_ = enable;
                printf("Error logging: %s\r\n", enable ? "ON" : "OFF");
            } else {
                serial_->println("Usage: log [events|errors] [on|off]");
            }
        } else {
            serial_->println("Usage: log [events|errors] [on|off]");
        }
    }

    void cmd_ssl(int argc, char** args) {
        if (argc < 2) {
            serial_->println("SSL/TLS Commands:");
            serial_->println("  ssl status      - Show certificate status");
            serial_->println("  ssl clear       - Delete stored certificate");
            serial_->println("  ssl debug [on|off] - Toggle TLS handshake messages");
            return;
        }

        if (strcmp(args[1], "status") == 0) {
#ifdef ESP32_BUILD
            // Cert is stored in "fermenter" namespace with keys "ssl:cert" and "ssl:key"
            nvs_handle_t nvs;
            if (nvs_open("fermenter", NVS_READONLY, &nvs) == ESP_OK) {
                size_t cert_len = 0, key_len = 0;
                bool has_cert = (nvs_get_str(nvs, "ssl:cert", nullptr, &cert_len) == ESP_OK && cert_len > 100);
                bool has_key = (nvs_get_str(nvs, "ssl:key", nullptr, &key_len) == ESP_OK && key_len > 100);
                nvs_close(nvs);

                if (has_cert && has_key) {
                    printf("Certificate: stored (cert=%d bytes, key=%d bytes)\r\n", (int)cert_len, (int)key_len);
                } else {
                    serial_->println("Certificate: not stored");
                }
            } else {
                serial_->println("Certificate: not stored");
            }
            printf("TLS debug output: %s\r\n", tls_debug_enabled_ ? "enabled" : "disabled");
#else
            serial_->println("Not available in simulator");
#endif
        } else if (strcmp(args[1], "clear") == 0) {
#ifdef ESP32_BUILD
            serial_->println("Clearing SSL certificate from NVS...");
            nvs_handle_t nvs;
            if (nvs_open("fermenter", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_erase_key(nvs, "ssl:cert");
                nvs_erase_key(nvs, "ssl:key");
                nvs_commit(nvs);
                nvs_close(nvs);
                serial_->println("SSL certificate cleared");
                serial_->println("Reboot to regenerate certificate");
            } else {
                serial_->println("Failed to open NVS");
            }
#else
            serial_->println("Not available in simulator");
#endif
        } else if (strcmp(args[1], "debug") == 0) {
#ifdef ESP32_BUILD
            if (argc >= 3) {
                if (strcmp(args[2], "on") == 0) {
                    tls_debug_enabled_ = true;
                    esp_log_level_set("esp-tls", ESP_LOG_INFO);
                    esp_log_level_set("esp_tls_mbedtls", ESP_LOG_INFO);
                    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_INFO);
                    esp_log_level_set("mbedtls", ESP_LOG_INFO);
                    serial_->println("TLS debug output: enabled");
                } else if (strcmp(args[2], "off") == 0) {
                    tls_debug_enabled_ = false;
                    esp_log_level_set("esp-tls", ESP_LOG_WARN);
                    esp_log_level_set("esp_tls_mbedtls", ESP_LOG_WARN);
                    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_WARN);
                    esp_log_level_set("mbedtls", ESP_LOG_WARN);
                    serial_->println("TLS debug output: disabled");
                } else {
                    serial_->println("Usage: ssl debug [on|off]");
                }
            } else {
                printf("TLS debug output: %s\r\n", tls_debug_enabled_ ? "enabled" : "disabled");
            }
#else
            serial_->println("Not available in simulator");
#endif
        } else {
            serial_->println("Unknown ssl subcommand. Use: ssl status | ssl clear | ssl debug");
        }
    }

    void cmd_ws(int argc, char** args) {
#if defined(ESP32_BUILD) && defined(WEBSOCKET_ENABLED)
        if (!http_server_) {
            serial_->println("HTTP server not available");
            return;
        }

        auto* http = static_cast<HttpServer*>(http_server_);
        auto* ws_mgr = http->get_ws_manager();

        if (!ws_mgr) {
            serial_->println("WebSocket not enabled");
            return;
        }

        if (argc < 2) {
            // Show status
            serial_->println("WebSocket Status:");
            printf("  Enabled: Yes\r\n");
            printf("  Clients: %d/%d\r\n", ws_mgr->get_client_count(), ws_mgr->get_max_clients());
            printf("  Initialized: %s\r\n", ws_mgr->is_initialized() ? "Yes" : "No");
            return;
        }

        if (strcmp(args[1], "clients") == 0) {
            int count = ws_mgr->get_client_count();
            if (count == 0) {
                serial_->println("No connected clients");
                return;
            }
            printf("Connected clients (%d):\r\n", count);
            ws_mgr->print_clients([](const char* line) {
                ::printf("  %s\r\n", line);
            });
        } else if (strcmp(args[1], "broadcast") == 0) {
            if (argc < 3) {
                serial_->println("Usage: ws broadcast <message>");
                return;
            }
            // Join remaining args into message
            char msg[256] = {0};
            int pos = 0;
            for (int i = 2; i < argc && pos < 250; i++) {
                if (i > 2) msg[pos++] = ' ';
                int len = strlen(args[i]);
                if (pos + len < 250) {
                    strcpy(msg + pos, args[i]);
                    pos += len;
                }
            }
            ws_mgr->broadcast_text(msg);
            printf("Broadcast sent: %s\r\n", msg);
        } else {
            serial_->println("WebSocket Commands:");
            serial_->println("  ws              - Show WebSocket status");
            serial_->println("  ws clients      - List connected clients");
            serial_->println("  ws broadcast <msg> - Send message to all clients");
        }
#else
        serial_->println("WebSocket not available (requires ESP32 + WEBSOCKET_ENABLED)");
#endif
    }

    // Public logging methods for other modules to call
public:
    void log_event(const char* msg) {
        if (log_events_) {
            printf("[EVENT] %s\r\n", msg);
        }
    }

    void log_error(const char* msg) {
        if (log_errors_) {
            printf("[ERROR] %s\r\n", msg);
        }
    }

private:
    void cmd_factory_reset() {
        serial_->println("");
        serial_->println("!!! FACTORY RESET !!!");
        serial_->println("This will erase ALL stored data:");
        serial_->println("  - WiFi credentials");
        serial_->println("  - Fermentation plans");
        serial_->println("  - PID parameters");
        serial_->println("  - Calibration data");
        serial_->println("");
        serial_->println("Type 'factory confirm' to proceed");
        serial_->println("");
    }

    void confirm_factory_reset() {
#ifdef ESP32_BUILD
        serial_->println("Erasing NVS...");
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) {
            serial_->println("NVS erased successfully");
            serial_->println("Rebooting...");
            serial_->flush();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else {
            printf("NVS erase failed: %d\r\n", err);
        }
#else
        serial_->println("Factory reset not available in simulator");
#endif
    }

    void cmd_reboot() {
        serial_->println("Rebooting...");
        serial_->flush();

#ifdef ESP32_BUILD
        esp_restart();
#else
        serial_->println("(Reboot not available in simulator)");
#endif
    }

    void cmd_shutdown() {
        serial_->println("Shutting down (entering deep sleep)...");
        serial_->println("Press RESET button to wake up");
        serial_->flush();

#ifdef ESP32_BUILD
        // Turn off all outputs before sleep
        if (gpio_) {
            for (uint8_t i = 0; i < 8; i++) {
                gpio_->set_relay(i, false);
            }
        }

        // Turn off status LED
        if (status_led_) {
            status_led_->off();
            vTaskDelay(pdMS_TO_TICKS(50));  // Let LED data transmit
        }

        // Small delay to ensure message is sent
        vTaskDelay(pdMS_TO_TICKS(100));

        // Enter deep sleep with no wake-up source (requires reset)
        esp_deep_sleep_start();
#else
        serial_->println("(Shutdown not available in simulator)");
#endif
    }
};

} // namespace modules
