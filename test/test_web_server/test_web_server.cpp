/**
 * Native HTTP server test for web admin interface
 *
 * Runs a mock fermenter controller API on localhost for UI testing.
 * Uses cpp-httplib for portable HTTP server.
 *
 * Build: g++ -std=c++17 -I../../include -pthread test_web_server.cpp -o web_server
 * Run: ./web_server
 * Open: http://localhost:8080/admin.html
 * Password: admin
 */

#include "httplib.h"

#include <iostream>
#include <chrono>
#include <random>
#include <cmath>

// Simulated state
struct SimState {
    float fermenters[8][4] = {  // temp, setpoint, pressure, pid_output
        {18.2f, 18.0f, 1.1f, 45.0f},
        {12.0f, 12.0f, 0.8f, 30.0f},
        {10.5f, 10.0f, 1.5f, 60.0f},
        {20.0f, 20.0f, 1.0f, 25.0f},
        {15.0f, 15.0f, 0.5f, 40.0f},
        {8.0f, 8.0f, 2.0f, 55.0f},
        {18.0f, 18.0f, 1.2f, 35.0f},
        {14.0f, 14.0f, 0.9f, 50.0f}
    };
    const char* modes[8] = {"MANUAL", "PLAN", "OFF", "MANUAL", "PLAN", "PLAN", "OFF", "MANUAL"};
    float pid_params[8][3] = {  // kp, ki, kd
        {2.0f, 0.1f, 1.0f},
        {2.5f, 0.15f, 1.2f},
        {1.8f, 0.08f, 0.9f},
        {2.2f, 0.12f, 1.1f},
        {2.0f, 0.1f, 1.0f},
        {2.3f, 0.11f, 1.0f},
        {2.1f, 0.1f, 1.0f},
        {2.4f, 0.13f, 1.1f}
    };
    bool relays[16] = {false};
    bool outputs[8] = {false};
    bool inputs[8] = {false, true, false, false, true, false, true, false};
    uint32_t modbus_transactions = 1250;
    uint32_t modbus_errors = 3;
    std::string session_token;
    std::chrono::steady_clock::time_point start_time;
};

SimState state;
std::mt19937 rng(42);

// Simulate sensor noise
float add_noise(float value, float range = 0.05f) {
    std::uniform_real_distribution<float> dist(-range, range);
    return value + dist(rng);
}

// Update simulated values
void update_simulation() {
    for (int i = 0; i < 8; i++) {
        // Temperature drifts toward setpoint
        float error = state.fermenters[i][1] - state.fermenters[i][0];
        state.fermenters[i][0] += error * 0.01f + add_noise(0, 0.02f);

        // Pressure changes slowly
        state.fermenters[i][2] = add_noise(state.fermenters[i][2], 0.01f);
        if (state.fermenters[i][2] < 0) state.fermenters[i][2] = 0;

        // PID output based on error
        state.fermenters[i][3] = std::max(0.0f, std::min(100.0f,
            50.0f + error * 20.0f + add_noise(0, 2.0f)));
    }

    // Randomly toggle some inputs for visual feedback
    if (rand() % 20 == 0) {
        state.inputs[rand() % 8] = !state.inputs[rand() % 8];
    }
}

// Simple JSON helpers
std::string json_bool(bool v) { return v ? "true" : "false"; }

int main() {
    httplib::Server svr;
    state.start_time = std::chrono::steady_clock::now();

    std::cout << "Starting Fermenter Web Admin Test Server..." << std::endl;

    // Serve static files
    svr.set_mount_point("/", "../../web");

    // CORS headers
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Authorization, Content-Type"}
    });

    // Handle OPTIONS for CORS
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    // Login
    svr.Post("/api/login", [](const httplib::Request& req, httplib::Response& res) {
        if (req.body.find("\"admin\"") != std::string::npos) {
            state.session_token = "test_token_12345";
            res.set_content(R"({"success":true,"token":")" + state.session_token + "\"}", "application/json");
        } else {
            res.status = 401;
            res.set_content(R"({"error":"Invalid password"})", "application/json");
        }
    });

    // Logout
    svr.Post("/api/logout", [](const httplib::Request&, httplib::Response& res) {
        state.session_token.clear();
        res.set_content(R"({"success":true})", "application/json");
    });

    // Health check
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // Auth middleware
    auto check_auth = [](const httplib::Request& req) {
        auto auth = req.get_header_value("Authorization");
        return auth.find(state.session_token) != std::string::npos;
    };

    // Status
    svr.Get("/api/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        update_simulation();
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - state.start_time).count();

        // Get current time
        auto now_time = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now_time);
        struct tm* timeinfo = localtime(&now_c);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

        char json[1024];
        snprintf(json, sizeof(json),
            R"({"version":"0.1.0","build":"251124","built":"Nov 24 2025 14:30:00","uptime":"%ldh %ldm %lds","uptime_seconds":%ld,"free_heap":245000,"wifi_rssi":-65,"ntp_synced":true,"sensor_count":18,"fermenter_count":8,"modbus_transactions":%u,"modbus_errors":%u,"system_time":"%s","timezone":"UTC","flash_used":1015808,"flash_total":4194304})",
            uptime/3600, (uptime%3600)/60, uptime%60, uptime,
            state.modbus_transactions++, state.modbus_errors,
            time_str);
        res.set_content(json, "application/json");
    });

    // Sensors
    svr.Get("/api/sensors", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string json = R"({"sensors":[)";
        json += R"({"name":"glycol_supply","modbus_addr":"1:0","value":2.1,"unit":"C","quality":"GOOD","type":"pt1000"},)";
        json += R"({"name":"glycol_return","modbus_addr":"1:1","value":8.5,"unit":"C","quality":"GOOD","type":"pt1000"},)";

        for (int i = 0; i < 8; i++) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                R"({"name":"fermenter_%d_temp","modbus_addr":"%d:0","value":%.2f,"unit":"C","quality":"GOOD","type":"pt1000"},)",
                i+1, i+2, state.fermenters[i][0]);
            json += buf;
            snprintf(buf, sizeof(buf),
                R"({"name":"fermenter_%d_pressure","modbus_addr":"%d:1","value":%.3f,"unit":"bar","quality":"GOOD","type":"pressure_0_1.6"}%s)",
                i+1, i+2, state.fermenters[i][2], i < 7 ? "," : "");
            json += buf;
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // Sensor config save
    svr.Post(R"(/api/sensor/(.+)/config)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string name = req.matches[1];
        // Parse type from body (in real implementation, save to config)

        char json[128];
        snprintf(json, sizeof(json),
            R"({"success":true,"sensor":"%s","message":"Configuration saved"})",
            name.c_str());
        res.set_content(json, "application/json");
    });

    // Individual sensor
    svr.Get(R"(/api/sensor/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string name = req.matches[1];
        char json[256];
        snprintf(json, sizeof(json),
            R"({"name":"%s","raw_value":18.15,"filtered_value":18.2,"display_value":18.2,"unit":"C","quality":"GOOD","filter_type":2,"alpha":0.30,"scale":0.1,"timestamp":5000})",
            name.c_str());
        res.set_content(json, "application/json");
    });

    // Relays
    svr.Get("/api/relays", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        const char* relay_names[] = {
            "glycol_chiller", "f1_cooling", "f1_spunding", "f2_cooling", "f2_spunding",
            "f3_cooling", "f3_spunding", "f4_cooling", "f4_spunding", "heater"
        };

        std::string json = R"({"relays":[)";
        for (int i = 0; i < 10; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                R"({"name":"%s","state":%s,"last_change":1000}%s)",
                relay_names[i], json_bool(state.relays[i]).c_str(), i < 9 ? "," : "");
            json += buf;
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // Set relay
    svr.Post(R"(/api/relay/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string name = req.matches[1];
        bool state_val = req.body.find("true") != std::string::npos;

        // Find relay index
        const char* relay_names[] = {
            "glycol_chiller", "f1_cooling", "f1_spunding", "f2_cooling", "f2_spunding",
            "f3_cooling", "f3_spunding", "f4_cooling", "f4_spunding", "heater"
        };
        for (int i = 0; i < 10; i++) {
            if (name == relay_names[i]) {
                state.relays[i] = state_val;
                break;
            }
        }

        char json[128];
        snprintf(json, sizeof(json),
            R"({"success":true,"relay":"%s","state":%s})",
            name.c_str(), json_bool(state_val).c_str());
        res.set_content(json, "application/json");
    });

    // Fermenters
    svr.Get("/api/fermenters", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string json = R"({"fermenters":[)";
        for (int i = 0; i < 8; i++) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                R"({"id":%d,"name":"F%d","temp":%.2f,"setpoint":%.2f,"pressure":%.3f,"mode":"%s","pid_output":%.1f}%s)",
                i+1, i+1, state.fermenters[i][0], state.fermenters[i][1],
                state.fermenters[i][2], state.modes[i], state.fermenters[i][3],
                i < 7 ? "," : "");
            json += buf;
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // Individual fermenter
    svr.Get(R"(/api/fermenter/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        int id = std::stoi(req.matches[1]) - 1;
        if (id < 0 || id >= 8) {
            res.status = 404;
            res.set_content(R"({"error":"Fermenter not found"})", "application/json");
            return;
        }

        char json[256];
        snprintf(json, sizeof(json),
            R"({"id":%d,"name":"F%d","temp":%.2f,"setpoint":%.2f,"pressure":%.3f,"target_pressure":1.0,"mode":"%s","pid_output":%.1f,"plan_active":true,"current_step":2,"hours_remaining":48.5})",
            id+1, id+1, state.fermenters[id][0], state.fermenters[id][1],
            state.fermenters[id][2], state.modes[id], state.fermenters[id][3]);
        res.set_content(json, "application/json");
    });

    // Set fermenter
    svr.Post(R"(/api/fermenter/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        int id = std::stoi(req.matches[1]) - 1;
        if (id < 0 || id >= 8) {
            res.status = 404;
            return;
        }

        // Parse setpoint
        auto pos = req.body.find("\"setpoint\":");
        if (pos != std::string::npos) {
            state.fermenters[id][1] = std::stof(req.body.substr(pos + 11));
        }

        // Parse mode
        if (req.body.find("\"OFF\"") != std::string::npos) state.modes[id] = "OFF";
        else if (req.body.find("\"MANUAL\"") != std::string::npos) state.modes[id] = "MANUAL";
        else if (req.body.find("\"PLAN\"") != std::string::npos) state.modes[id] = "PLAN";

        char json[128];
        snprintf(json, sizeof(json),
            R"({"success":true,"id":%d,"setpoint":%.1f,"mode":"%s"})",
            id+1, state.fermenters[id][1], state.modes[id]);
        res.set_content(json, "application/json");
    });

    // PID
    svr.Get(R"(/api/pid/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        int id = std::stoi(req.matches[1]) - 1;
        if (id < 0 || id >= 8) {
            res.status = 404;
            return;
        }

        char json[256];
        snprintf(json, sizeof(json),
            R"({"fermenter_id":%d,"kp":%.3f,"ki":%.3f,"kd":%.3f,"output":%.1f,"output_min":0,"output_max":100,"integral":12.34,"last_error":0.5})",
            id+1, state.pid_params[id][0], state.pid_params[id][1], state.pid_params[id][2],
            state.fermenters[id][3]);
        res.set_content(json, "application/json");
    });

    // Set PID
    svr.Post(R"(/api/pid/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        int id = std::stoi(req.matches[1]) - 1;
        if (id < 0 || id >= 8) {
            res.status = 404;
            return;
        }

        auto parse_param = [&](const char* name) -> float {
            auto pos = req.body.find(name);
            if (pos != std::string::npos) {
                pos = req.body.find(":", pos);
                return std::stof(req.body.substr(pos + 1));
            }
            return 0;
        };

        if (req.body.find("\"kp\"") != std::string::npos)
            state.pid_params[id][0] = parse_param("\"kp\"");
        if (req.body.find("\"ki\"") != std::string::npos)
            state.pid_params[id][1] = parse_param("\"ki\"");
        if (req.body.find("\"kd\"") != std::string::npos)
            state.pid_params[id][2] = parse_param("\"kd\"");

        char json[128];
        snprintf(json, sizeof(json),
            R"({"success":true,"id":%d,"kp":%.3f,"ki":%.3f,"kd":%.3f})",
            id+1, state.pid_params[id][0], state.pid_params[id][1], state.pid_params[id][2]);
        res.set_content(json, "application/json");
    });

    // Alarms
    svr.Get("/api/alarms", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }
        res.set_content(R"({"alarms":[]})", "application/json");
    });

    // MODBUS stats
    svr.Get("/api/modbus/stats", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        float error_rate = 100.0f * state.modbus_errors / state.modbus_transactions;
        char json[128];
        snprintf(json, sizeof(json),
            R"({"transactions":%u,"errors":%u,"error_rate":%.2f})",
            state.modbus_transactions, state.modbus_errors, error_rate);
        res.set_content(json, "application/json");
    });

    // Inputs
    svr.Get("/api/inputs", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string json = R"({"inputs":[)";
        for (int i = 0; i < 8; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                R"({"id":%d,"state":%s}%s)",
                i+1, json_bool(state.inputs[i]).c_str(), i < 7 ? "," : "");
            json += buf;
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // Outputs
    svr.Get("/api/outputs", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        std::string json = R"({"outputs":[)";
        for (int i = 0; i < 8; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                R"({"id":%d,"state":%s}%s)",
                i+1, json_bool(state.outputs[i]).c_str(), i < 7 ? "," : "");
            json += buf;
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // Set output
    svr.Post(R"(/api/output/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        int id = std::stoi(req.matches[1]) - 1;
        if (id < 0 || id >= 8) {
            res.status = 400;
            return;
        }

        state.outputs[id] = req.body.find("true") != std::string::npos;

        char json[64];
        snprintf(json, sizeof(json),
            R"({"success":true,"output":%d,"state":%s})",
            id+1, json_bool(state.outputs[id]).c_str());
        res.set_content(json, "application/json");
    });

    // Config
    svr.Get("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        res.set_content(R"({"fermenter_count":8,"modbus_device_count":4,"gpio_relay_count":8,"timing":{"modbus_poll_ms":1000,"pid_interval_ms":5000,"safety_check_ms":1000}})", "application/json");
    });

    // Modules
    svr.Get("/api/modules", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        res.set_content(R"({"modules":{"wifi":true,"ntp":true,"http":true,"mqtt":false,"can":true,"debug_console":true}})", "application/json");
    });

    // CAN status
    svr.Get("/api/can/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }

        update_simulation();
        char json[128];
        snprintf(json, sizeof(json),
            R"({"tx":15,"rx":42,"errors":0,"state":"OK","bitrate":500000})");
        res.set_content(json, "application/json");
    });

    // Reboot
    svr.Post("/api/reboot", [&](const httplib::Request& req, httplib::Response& res) {
        if (!check_auth(req)) { res.status = 401; return; }
        res.set_content(R"({"success":true,"message":"Rebooting..."})", "application/json");
    });

    std::cout << "\n========================================" << std::endl;
    std::cout << "Fermenter Controller Web Admin" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server running at: http://localhost:8080" << std::endl;
    std::cout << "Admin page: http://localhost:8080/admin.html" << std::endl;
    std::cout << "Password: admin" << std::endl;
    std::cout << "========================================\n" << std::endl;

    svr.listen("0.0.0.0", 8080);

    return 0;
}
