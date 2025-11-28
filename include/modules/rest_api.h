#pragma once

#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/config.h"
#include "core/config_loader.h"
#include "core/utils.h"
#include "modules/fermentation_plan.h"
#include <cstring>
#include <cstdio>

namespace modules {

/**
 * HTTP Method types
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE
};

/**
 * HTTP Response
 */
struct HttpResponse {
    int status_code;
    char content_type[32];
    char body[4096];
    size_t body_length;

    HttpResponse() : status_code(200), body_length(0) {
        core::safe_strncpy(content_type, "application/json", sizeof(content_type));
        body[0] = '\0';
    }

    void set_json(const char* json) {
        core::safe_strncpy(body, json, sizeof(body));
        body_length = strlen(body);
    }

    void set_error(int code, const char* message) {
        status_code = code;
        snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
        body_length = strlen(body);
    }
};

/**
 * REST API Handler
 * Provides HTTP endpoint handlers for the fermentation controller
 */
class RestApiHandler {
public:
    RestApiHandler(core::StateManager* state,
                   core::EventBus* events,
                   FermentationPlanManager* plans,
                   core::SystemConfig* config)
        : state_(state)
        , events_(events)
        , plans_(plans)
        , config_(config) {}

    /**
     * Handle an API request
     * @param method HTTP method
     * @param path Request path (e.g., "/sensors")
     * @param body Request body (for POST/PUT)
     * @param response Output response
     */
    void handle_request(HttpMethod method, const char* path,
                       const char* body, HttpResponse& response) {
        // Sensors
        if (strcmp(path, "/sensors") == 0 && method == HttpMethod::GET) {
            handle_get_sensors(response);
            return;
        }

        if (strncmp(path, "/sensors/", 9) == 0 && method == HttpMethod::GET) {
            handle_get_sensor(path + 9, response);
            return;
        }

        // Relays
        if (strcmp(path, "/relays") == 0 && method == HttpMethod::GET) {
            handle_get_relays(response);
            return;
        }

        if (strncmp(path, "/relays/", 8) == 0) {
            const char* relay_path = path + 8;
            size_t path_len = strlen(relay_path);

            // Check for exact /on suffix (not substring)
            bool ends_with_on = path_len >= 3 &&
                strcmp(relay_path + path_len - 3, "/on") == 0;
            // Check for exact /off suffix (not substring)
            bool ends_with_off = path_len >= 4 &&
                strcmp(relay_path + path_len - 4, "/off") == 0;

            if (ends_with_on && method == HttpMethod::POST) {
                // Extract relay name (path without /on suffix)
                char name[32] = {0};
                size_t name_len = path_len - 3;
                if (name_len > 0 && name_len < sizeof(name)) {
                    memcpy(name, relay_path, name_len);
                    handle_relay_on(name, response);
                    return;
                }
            }
            if (ends_with_off && method == HttpMethod::POST) {
                // Extract relay name (path without /off suffix)
                char name[32] = {0};
                size_t name_len = path_len - 4;
                if (name_len > 0 && name_len < sizeof(name)) {
                    memcpy(name, relay_path, name_len);
                    handle_relay_off(name, response);
                    return;
                }
            }
        }

        // Fermenters
        if (strcmp(path, "/fermenters") == 0 && method == HttpMethod::GET) {
            handle_get_fermenters(response);
            return;
        }

        if (strncmp(path, "/fermenters/", 12) == 0) {
            const char* ferm_path = path + 12;

            // Extract fermenter ID
            int ferm_id = atoi(ferm_path);
            if (ferm_id < 1 || ferm_id > 8) {
                response.set_error(404, "Fermenter not found");
                return;
            }

            if (strstr(ferm_path, "/plan") && method == HttpMethod::POST) {
                handle_start_plan(ferm_id, body, response);
                return;
            }

            if (strstr(ferm_path, "/plan") && method == HttpMethod::GET) {
                handle_get_plan(ferm_id, response);
                return;
            }

            if (strstr(ferm_path, "/plan") && method == HttpMethod::DELETE) {
                handle_stop_plan(ferm_id, response);
                return;
            }

            if (strstr(ferm_path, "/setpoint") && method == HttpMethod::PUT) {
                handle_set_setpoint(ferm_id, body, response);
                return;
            }

            // Get single fermenter
            if (method == HttpMethod::GET) {
                handle_get_fermenter(ferm_id, response);
                return;
            }
        }

        // System
        if (strcmp(path, "/system/status") == 0 && method == HttpMethod::GET) {
            handle_get_system_status(response);
            return;
        }

        if (strcmp(path, "/config") == 0 && method == HttpMethod::GET) {
            handle_get_config(response);
            return;
        }

        // Not found
        response.set_error(404, "Endpoint not found");
    }

private:
    core::StateManager* state_;
    core::EventBus* events_;
    FermentationPlanManager* plans_;
    core::SystemConfig* config_;

    void handle_get_sensors(HttpResponse& response) {
        char json[4096];
        int offset = 0;

        core::safe_snprintf_append(json, sizeof(json), offset, "{\"sensors\":[");

        uint8_t count = state_->get_sensor_count();
        for (uint8_t i = 0; i < count; i++) {
            auto* sensor = state_->get_sensor_by_id(i);
            if (!sensor) continue;

            if (i > 0) core::safe_snprintf_append(json, sizeof(json), offset, ",");

            core::safe_snprintf_append(json, sizeof(json), offset,
                "{\"name\":\"%s\",\"value\":%.2f,\"unit\":\"%s\",\"quality\":%d}",
                sensor->name, sensor->filtered_value, sensor->unit,
                static_cast<int>(sensor->quality));
        }

        core::safe_snprintf_append(json, sizeof(json), offset, "]}");
        response.set_json(json);
    }

    void handle_get_sensor(const char* name, HttpResponse& response) {
        auto* sensor = state_->get_sensor(name);
        if (!sensor) {
            response.set_error(404, "Sensor not found");
            return;
        }

        char json[512];
        snprintf(json, sizeof(json),
            "{\"name\":\"%s\",\"raw\":%.2f,\"filtered\":%.2f,\"display\":%.2f,"
            "\"unit\":\"%s\",\"quality\":%d,\"timestamp\":%lu}",
            sensor->name, sensor->raw_value, sensor->filtered_value,
            sensor->display_value, sensor->unit,
            static_cast<int>(sensor->quality), (unsigned long)sensor->timestamp);

        response.set_json(json);
    }

    void handle_get_relays(HttpResponse& response) {
        char json[2048];
        int offset = 0;

        core::safe_snprintf_append(json, sizeof(json), offset, "{\"relays\":[");

        uint8_t count = state_->get_relay_count();
        for (uint8_t i = 0; i < count; i++) {
            auto* relay = state_->get_relay_by_id(i);
            if (!relay) continue;

            if (i > 0) core::safe_snprintf_append(json, sizeof(json), offset, ",");

            core::safe_snprintf_append(json, sizeof(json), offset,
                "{\"name\":\"%s\",\"state\":%s,\"duty_cycle\":%.1f}",
                relay->name, relay->state ? "true" : "false", relay->duty_cycle);
        }

        core::safe_snprintf_append(json, sizeof(json), offset, "]}");
        response.set_json(json);
    }

    void handle_relay_on(const char* name, HttpResponse& response) {
        uint8_t id = state_->get_relay_id(name);
        if (id == 0xFF) {
            response.set_error(404, "Relay not found");
            return;
        }

        state_->set_relay_state(id, true, 0);

        if (events_) {
            events_->publish_relay_change(id, true, 0);
        }

        response.set_json("{\"success\":true}");
    }

    void handle_relay_off(const char* name, HttpResponse& response) {
        uint8_t id = state_->get_relay_id(name);
        if (id == 0xFF) {
            response.set_error(404, "Relay not found");
            return;
        }

        state_->set_relay_state(id, false, 0);

        if (events_) {
            events_->publish_relay_change(id, false, 0);
        }

        response.set_json("{\"success\":true}");
    }

    void handle_get_fermenters(HttpResponse& response) {
        char json[4096];
        int offset = 0;

        core::safe_snprintf_append(json, sizeof(json), offset, "{\"fermenters\":[");

        uint8_t count = state_->get_fermenter_count();
        for (uint8_t i = 0; i < count; i++) {
            auto* ferm = state_->get_fermenter(i + 1);
            if (!ferm) continue;

            if (i > 0) core::safe_snprintf_append(json, sizeof(json), offset, ",");

            core::safe_snprintf_append(json, sizeof(json), offset,
                "{\"id\":%d,\"name\":\"%s\",\"current_temp\":%.1f,\"target_temp\":%.1f,"
                "\"current_pressure\":%.2f,\"target_pressure\":%.2f,\"mode\":%d,"
                "\"plan_active\":%s,\"current_step\":%d,\"hours_remaining\":%.1f}",
                ferm->id, ferm->name, ferm->current_temp, ferm->target_temp,
                ferm->current_pressure, ferm->target_pressure,
                static_cast<int>(ferm->mode),
                ferm->plan_active ? "true" : "false",
                ferm->current_step, ferm->hours_remaining);
        }

        core::safe_snprintf_append(json, sizeof(json), offset, "]}");
        response.set_json(json);
    }

    void handle_get_fermenter(int id, HttpResponse& response) {
        auto* ferm = state_->get_fermenter(id);
        if (!ferm) {
            response.set_error(404, "Fermenter not found");
            return;
        }

        char json[512];
        snprintf(json, sizeof(json),
            "{\"id\":%d,\"name\":\"%s\",\"current_temp\":%.1f,\"target_temp\":%.1f,"
            "\"current_pressure\":%.2f,\"target_pressure\":%.2f,\"mode\":%d,"
            "\"plan_active\":%s,\"current_step\":%d,\"hours_remaining\":%.1f,"
            "\"pid_output\":%.1f}",
            ferm->id, ferm->name, ferm->current_temp, ferm->target_temp,
            ferm->current_pressure, ferm->target_pressure,
            static_cast<int>(ferm->mode),
            ferm->plan_active ? "true" : "false",
            ferm->current_step, ferm->hours_remaining, ferm->pid_output);

        response.set_json(json);
    }

    void handle_get_plan(int ferm_id, HttpResponse& response) {
        const auto* plan = plans_->get_plan(ferm_id);
        if (!plan || !plan->active) {
            response.set_json("{\"active\":false}");
            return;
        }

        char json[2048];
        int offset = 0;

        core::safe_snprintf_append(json, sizeof(json), offset,
            "{\"active\":true,\"start_time\":%lu,\"current_step\":%d,\"steps\":[",
            (unsigned long)plan->start_time, plan->current_step);

        for (uint8_t i = 0; i < plan->step_count; i++) {
            if (i > 0) core::safe_snprintf_append(json, sizeof(json), offset, ",");

            core::safe_snprintf_append(json, sizeof(json), offset,
                "{\"name\":\"%s\",\"duration_hours\":%lu,\"target_temp\":%.1f,"
                "\"target_pressure\":%.2f}",
                plan->steps[i].name, (unsigned long)plan->steps[i].duration_hours,
                plan->steps[i].target_temp, plan->steps[i].target_pressure);
        }

        core::safe_snprintf_append(json, sizeof(json), offset, "]}");
        response.set_json(json);
    }

    void handle_start_plan(int ferm_id, const char* body, HttpResponse& response) {
        // Simple JSON parsing for plan steps
        // In production, use ArduinoJson
        core::PlanStep steps[core::MAX_PLAN_STEPS];
        uint8_t step_count = 0;

        // For now, just acknowledge - real implementation would parse body
        (void)ferm_id;
        (void)body;
        (void)steps;
        (void)step_count;

        response.set_json("{\"success\":true,\"message\":\"Plan parsing not implemented\"}");
    }

    void handle_stop_plan(int ferm_id, HttpResponse& response) {
        plans_->stop_plan(ferm_id);
        response.set_json("{\"success\":true}");
    }

    void handle_set_setpoint(int ferm_id, const char* body, HttpResponse& response) {
        // Parse temperature from body
        float temp = 0;
        if (sscanf(body, "{\"temperature\":%f}", &temp) == 1 ||
            sscanf(body, "{\"temp\":%f}", &temp) == 1) {

            auto* ferm = state_->get_fermenter(ferm_id);
            if (ferm) {
                state_->update_fermenter_temps(ferm_id, ferm->current_temp, temp);
                state_->set_fermenter_mode(ferm_id, core::FermenterMode::MANUAL);
                response.set_json("{\"success\":true}");
                return;
            }
        }

        response.set_error(400, "Invalid request body");
    }

    void handle_get_system_status(HttpResponse& response) {
        auto& sys = state_->get_system_state();

        char json[512];
        snprintf(json, sizeof(json),
            "{\"uptime\":%lu,\"ntp_synced\":%s,\"wifi_rssi\":%d,"
            "\"free_heap\":%lu,\"modbus_transactions\":%lu,\"modbus_errors\":%lu}",
            (unsigned long)sys.uptime_seconds, sys.ntp_synced ? "true" : "false",
            sys.wifi_rssi, (unsigned long)sys.free_heap,
            (unsigned long)sys.modbus_transactions, (unsigned long)sys.modbus_errors);

        response.set_json(json);
    }

    void handle_get_config(HttpResponse& response) {
        char buffer[4096];
        size_t len = core::ConfigLoader::to_json(*config_, buffer, sizeof(buffer));

        if (len > 0) {
            response.set_json(buffer);
        } else {
            response.set_error(500, "Failed to serialize config");
        }
    }
};

} // namespace modules
