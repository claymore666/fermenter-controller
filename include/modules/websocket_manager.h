#pragma once

#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/types.h"
#include <cstring>
#include <cstdio>
#include <cmath>

#ifdef ESP32_BUILD
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

namespace modules {

/**
 * WebSocket Manager for real-time push updates
 * Supports event-driven updates (only sends on value changes)
 * Falls back gracefully when WebSocket is unavailable
 */
class WebSocketManager {
public:
    static constexpr int MAX_WS_CLIENTS = 4;
    static constexpr uint32_t PING_INTERVAL_MS = 30000;
    static constexpr uint32_t CLIENT_TIMEOUT_MS = 60000;
    static constexpr float SENSOR_CHANGE_THRESHOLD = 0.01f;
    static constexpr size_t MAX_TX_BUFFER = 512;
    static constexpr int MAX_SENSORS = 32;
    static constexpr int MAX_RELAYS = 24;

    struct WsClient {
        int fd;
#ifdef ESP32_BUILD
        httpd_handle_t handle;
#else
        void* handle;
#endif
        char session_token[65];
        uint32_t last_activity;
        uint32_t last_ping;
        bool authenticated;
        bool active;

        WsClient() : fd(-1), handle(nullptr), session_token{},
                     last_activity(0), last_ping(0),
                     authenticated(false), active(false) {}
    };

    using SessionValidator = bool(*)(const char* token, void* user_ctx);

    WebSocketManager()
        : state_(nullptr)
        , events_(nullptr)
        , session_validator_(nullptr)
        , validator_ctx_(nullptr)
        , sensor_sub_id_(-1)
        , relay_sub_id_(-1)
        , alarm_sub_id_(-1)
        , initialized_(false)
        , client_count_(0) {
        memset(last_sensor_values_, 0, sizeof(last_sensor_values_));
        memset(last_relay_states_, 0, sizeof(last_relay_states_));
#ifdef ESP32_BUILD
        mutex_ = xSemaphoreCreateMutex();
#endif
    }

    ~WebSocketManager() {
        stop();
#ifdef ESP32_BUILD
        if (mutex_) {
            vSemaphoreDelete(mutex_);
            mutex_ = nullptr;
        }
#endif
    }

#ifdef ESP32_BUILD
    /**
     * Initialize WebSocket manager
     * @param server HTTP server handle to register WebSocket URI on
     * @param state StateManager for accessing current state
     * @param events EventBus for subscribing to updates
     * @return true on success
     */
    bool initialize(httpd_handle_t server, core::StateManager* state, core::EventBus* events) {
        if (initialized_) return true;
        if (!server || !state || !events) return false;

        state_ = state;
        events_ = events;
        server_ = server;

        // Register WebSocket URI handler
        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler,
            .user_ctx = this,
            .is_websocket = true,
            .handle_ws_control_frames = true,
            .supported_subprotocol = nullptr
        };

        esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
        if (ret != ESP_OK) {
            ESP_LOGE("WS", "Failed to register WebSocket handler: %s", esp_err_to_name(ret));
            return false;
        }

        // Subscribe to EventBus for real-time updates
        sensor_sub_id_ = events_->subscribe(core::EventType::SENSOR_UPDATE,
            [this](const core::Event& e) { on_sensor_update(e); });

        relay_sub_id_ = events_->subscribe(core::EventType::RELAY_CHANGE,
            [this](const core::Event& e) { on_relay_change(e); });

        alarm_sub_id_ = events_->subscribe(core::EventType::ALARM,
            [this](const core::Event& e) { on_alarm(e); });

        initialized_ = true;
        ESP_LOGI("WS", "WebSocket manager initialized (max %d clients)", MAX_WS_CLIENTS);
        return true;
    }

    /**
     * Set session validator callback
     * Used to validate authentication tokens against HTTP server sessions
     */
    void set_session_validator(SessionValidator validator, void* ctx) {
        session_validator_ = validator;
        validator_ctx_ = ctx;
    }

    /**
     * Stop WebSocket manager and disconnect all clients
     */
    void stop() {
        if (!initialized_) return;

        // Unsubscribe from events
        if (events_) {
            if (sensor_sub_id_ >= 0) events_->unsubscribe(sensor_sub_id_);
            if (relay_sub_id_ >= 0) events_->unsubscribe(relay_sub_id_);
            if (alarm_sub_id_ >= 0) events_->unsubscribe(alarm_sub_id_);
        }

        // Close all client connections
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active) {
                close_client(i);
            }
        }
        unlock();

        initialized_ = false;
        ESP_LOGI("WS", "WebSocket manager stopped");
    }

    /**
     * Process periodic tasks (ping, timeout cleanup)
     * Call from main loop every ~1 second
     */
    void process() {
        if (!initialized_) return;

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (!clients_[i].active) continue;

            // Check for timeout
            if (now - clients_[i].last_activity > CLIENT_TIMEOUT_MS) {
                ESP_LOGW("WS", "Client %d timed out", clients_[i].fd);
                close_client(i);
                continue;
            }

            // Send periodic ping
            if (now - clients_[i].last_ping > PING_INTERVAL_MS) {
                send_ping(i);
                clients_[i].last_ping = now;
            }
        }
        unlock();
    }

    /**
     * Get number of connected (authenticated) clients
     */
    int get_client_count() const {
        int count = 0;
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].authenticated) count++;
        }
        return count;
    }

    /**
     * Get maximum number of clients supported
     */
    int get_max_clients() const {
        return MAX_WS_CLIENTS;
    }

    /**
     * Check if WebSocket manager is initialized
     */
    bool is_initialized() const {
        return initialized_;
    }

    /**
     * Print connected clients info via callback
     */
    template<typename F>
    void print_clients(F callback) const {
        char buf[128];
#ifdef ESP32_BUILD
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
#else
        uint32_t now = 0;
#endif
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].authenticated) {
                uint32_t age_ms = now - clients_[i].last_activity;
                snprintf(buf, sizeof(buf), "[%d] fd=%d session=%.8s... last=%lums",
                         i, clients_[i].fd, clients_[i].session_token, (unsigned long)age_ms);
                callback(buf);
            }
        }
    }

    /**
     * Broadcast a text message to all authenticated clients
     */
    void broadcast_text(const char* message) {
        if (!initialized_ || !message) return;
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].authenticated) {
                send_text(clients_[i].fd, clients_[i].handle, message);
            }
        }
        unlock();
    }

    /**
     * WebSocket handler - called by HTTP server for /ws requests
     */
    static esp_err_t ws_handler(httpd_req_t* req) {
        WebSocketManager* self = static_cast<WebSocketManager*>(req->user_ctx);
        if (!self) return ESP_FAIL;

        if (req->method == HTTP_GET) {
            // WebSocket handshake
            ESP_LOGI("WS", "WebSocket handshake from fd %d", httpd_req_to_sockfd(req));
            return ESP_OK;
        }

        // Handle WebSocket frame
        httpd_ws_frame_t ws_frame;
        memset(&ws_frame, 0, sizeof(ws_frame));

        // First call with len=0 to get frame info
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_frame, 0);
        if (ret != ESP_OK) {
            ESP_LOGE("WS", "Failed to get frame info: %s", esp_err_to_name(ret));
            return ret;
        }

        if (ws_frame.len > 0) {
            // Allocate buffer for payload
            ws_frame.payload = (uint8_t*)malloc(ws_frame.len + 1);
            if (!ws_frame.payload) {
                ESP_LOGE("WS", "Failed to allocate frame buffer");
                return ESP_ERR_NO_MEM;
            }

            ret = httpd_ws_recv_frame(req, &ws_frame, ws_frame.len);
            if (ret != ESP_OK) {
                ESP_LOGE("WS", "Failed to receive frame: %s", esp_err_to_name(ret));
                free(ws_frame.payload);
                return ret;
            }

            ws_frame.payload[ws_frame.len] = '\0';
            self->handle_message(req, &ws_frame);
            free(ws_frame.payload);
        }

        // Handle control frames
        if (ws_frame.type == HTTPD_WS_TYPE_CLOSE) {
            int fd = httpd_req_to_sockfd(req);
            self->remove_client(fd);
        } else if (ws_frame.type == HTTPD_WS_TYPE_PONG) {
            // Update last activity on pong
            int fd = httpd_req_to_sockfd(req);
            self->update_client_activity(fd);
        }

        return ESP_OK;
    }

private:
    // Dependencies
    core::StateManager* state_;
    core::EventBus* events_;
    httpd_handle_t server_;
    SessionValidator session_validator_;
    void* validator_ctx_;

    // Subscriptions
    int sensor_sub_id_;
    int relay_sub_id_;
    int alarm_sub_id_;

    // State
    bool initialized_;
    WsClient clients_[MAX_WS_CLIENTS];
    int client_count_;
    SemaphoreHandle_t mutex_;

    // Change detection
    float last_sensor_values_[MAX_SENSORS];
    bool last_relay_states_[MAX_RELAYS];

    void lock() {
        if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    }

    void unlock() {
        if (mutex_) xSemaphoreGive(mutex_);
    }

    /**
     * Handle incoming WebSocket message
     */
    void handle_message(httpd_req_t* req, httpd_ws_frame_t* frame) {
        if (frame->type != HTTPD_WS_TYPE_TEXT) return;

        int fd = httpd_req_to_sockfd(req);
        const char* payload = (const char*)frame->payload;

        ESP_LOGD("WS", "Received from fd %d: %s", fd, payload);

        // Parse message type
        // Expected format: {"type":"auth","token":"..."}
        const char* type_start = strstr(payload, "\"type\"");
        if (!type_start) return;

        if (strstr(payload, "\"auth\"")) {
            // Authentication message
            const char* token_start = strstr(payload, "\"token\"");
            if (token_start) {
                token_start = strchr(token_start + 7, '"');
                if (token_start) {
                    token_start++;
                    const char* token_end = strchr(token_start, '"');
                    if (token_end && token_end - token_start < 65) {
                        char token[65];
                        size_t len = token_end - token_start;
                        memcpy(token, token_start, len);
                        token[len] = '\0';

                        if (authenticate_client(fd, req->handle, token)) {
                            send_text(fd, req->handle, "{\"type\":\"auth_ok\"}");
                            send_full_state(fd, req->handle);
                        } else {
                            send_text(fd, req->handle, "{\"type\":\"auth_failed\"}");
                        }
                    }
                }
            }
        }

        update_client_activity(fd);
    }

    /**
     * Add a new client connection
     */
    int add_client(int fd, httpd_handle_t handle) {
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (!clients_[i].active) {
                clients_[i].fd = fd;
                clients_[i].handle = handle;
                clients_[i].session_token[0] = '\0';
                clients_[i].last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;
                clients_[i].last_ping = clients_[i].last_activity;
                clients_[i].authenticated = false;
                clients_[i].active = true;
                client_count_++;
                unlock();

                ESP_LOGI("WS", "Client added: fd=%d (slot %d, total %d)", fd, i, client_count_);
                // Send auth required message
                send_text(fd, handle, "{\"type\":\"auth_required\"}");
                return i;
            }
        }
        unlock();
        ESP_LOGW("WS", "No slots available for new client");
        return -1;
    }

    /**
     * Remove client by file descriptor
     */
    void remove_client(int fd) {
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].fd == fd) {
                close_client(i);
                break;
            }
        }
        unlock();
    }

    /**
     * Close client at index (must hold lock)
     */
    void close_client(int index) {
        if (!clients_[index].active) return;

        ESP_LOGI("WS", "Client removed: fd=%d", clients_[index].fd);
        clients_[index].active = false;
        clients_[index].authenticated = false;
        clients_[index].fd = -1;
        client_count_--;
    }

    /**
     * Update client last activity timestamp
     */
    void update_client_activity(int fd) {
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].fd == fd) {
                clients_[i].last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;
                break;
            }
        }
        unlock();
    }

    /**
     * Authenticate client with session token
     */
    bool authenticate_client(int fd, httpd_handle_t handle, const char* token) {
        // Validate token using callback
        if (session_validator_ && !session_validator_(token, validator_ctx_)) {
            ESP_LOGW("WS", "Invalid session token from fd %d", fd);
            return false;
        }

        lock();
        // Find or add client
        int slot = -1;
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].fd == fd) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            // New client
            for (int i = 0; i < MAX_WS_CLIENTS; i++) {
                if (!clients_[i].active) {
                    slot = i;
                    clients_[i].fd = fd;
                    clients_[i].handle = handle;
                    clients_[i].active = true;
                    clients_[i].last_activity = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    clients_[i].last_ping = clients_[i].last_activity;
                    client_count_++;
                    break;
                }
            }
        }

        if (slot < 0) {
            unlock();
            ESP_LOGW("WS", "No slots available for authenticated client");
            return false;
        }

        strncpy(clients_[slot].session_token, token, 64);
        clients_[slot].session_token[64] = '\0';
        clients_[slot].authenticated = true;
        unlock();

        ESP_LOGI("WS", "Client authenticated: fd=%d", fd);
        return true;
    }

    /**
     * Send text message to specific client
     */
    void send_text(int fd, httpd_handle_t handle, const char* text) {
        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t*)text,
            .len = strlen(text)
        };

        esp_err_t ret = httpd_ws_send_frame_async(handle, fd, &frame);
        if (ret != ESP_OK) {
            ESP_LOGW("WS", "Failed to send to fd %d: %s", fd, esp_err_to_name(ret));
        }
    }

    /**
     * Send ping to client
     */
    void send_ping(int index) {
        if (!clients_[index].active) return;

        httpd_ws_frame_t frame = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_PING,
            .payload = nullptr,
            .len = 0
        };

        httpd_ws_send_frame_async(clients_[index].handle, clients_[index].fd, &frame);
    }

    /**
     * Broadcast message to all authenticated clients
     */
    void broadcast(const char* message) {
        lock();
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (clients_[i].active && clients_[i].authenticated) {
                send_text(clients_[i].fd, clients_[i].handle, message);
            }
        }
        unlock();
    }

    /**
     * Send full state dump to newly connected client
     */
    void send_full_state(int fd, httpd_handle_t handle) {
        if (!state_) return;

        char buffer[MAX_TX_BUFFER * 4];  // Larger buffer for full state
        int offset = 0;

        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
            "{\"type\":\"full\",\"sensors\":[");

        // Add all sensors
        auto guard = state_->scoped_lock(100);
        if (!guard.acquired()) return;

        uint8_t sensor_count = state_->get_sensor_count();
        for (uint8_t i = 0; i < sensor_count && offset < (int)sizeof(buffer) - 100; i++) {
            auto* sensor = state_->get_sensor_by_id(i);
            if (!sensor) continue;

            if (i > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ",");
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                "{\"id\":%d,\"name\":\"%s\",\"value\":%.2f,\"unit\":\"%s\",\"quality\":\"%s\"}",
                i, sensor->name, sensor->filtered_value, sensor->unit,
                quality_to_string(sensor->quality));

            // Track for change detection
            if (i < MAX_SENSORS) {
                last_sensor_values_[i] = sensor->filtered_value;
            }
        }

        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "],\"relays\":[");

        // Add all relays
        uint8_t relay_count = state_->get_relay_count();
        for (uint8_t i = 0; i < relay_count && offset < (int)sizeof(buffer) - 100; i++) {
            auto* relay = state_->get_relay_by_id(i);
            if (!relay) continue;

            if (i > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ",");
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                "{\"id\":%d,\"name\":\"%s\",\"state\":%s}",
                i, relay->name, relay->state ? "true" : "false");

            // Track for change detection
            if (i < MAX_RELAYS) {
                last_relay_states_[i] = relay->state;
            }
        }

        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "]}");

        send_text(fd, handle, buffer);
    }

    /**
     * EventBus callback: sensor update
     */
    void on_sensor_update(const core::Event& event) {
        if (!initialized_ || client_count_ == 0) return;

        uint8_t id = event.source_id;
        if (id >= MAX_SENSORS) return;

        float new_value = event.data.value;

        // Check for significant change
        if (std::abs(new_value - last_sensor_values_[id]) < SENSOR_CHANGE_THRESHOLD) {
            return;  // No significant change
        }

        last_sensor_values_[id] = new_value;

        // Get quality from state manager
        const char* quality = "UNKNOWN";
        if (state_) {
            auto guard = state_->scoped_lock(50);
            if (guard.acquired()) {
                auto* sensor = state_->get_sensor_by_id(id);
                if (sensor) {
                    quality = quality_to_string(sensor->quality);
                }
            }
        }

        char buffer[MAX_TX_BUFFER];
        snprintf(buffer, sizeof(buffer),
            "{\"type\":\"sensor\",\"id\":%d,\"value\":%.2f,\"quality\":\"%s\"}",
            id, new_value, quality);

        broadcast(buffer);
    }

    /**
     * EventBus callback: relay change
     */
    void on_relay_change(const core::Event& event) {
        if (!initialized_ || client_count_ == 0) return;

        uint8_t id = event.source_id;
        if (id >= MAX_RELAYS) return;

        bool new_state = event.data.state;

        // Check for change
        if (new_state == last_relay_states_[id]) {
            return;  // No change
        }

        last_relay_states_[id] = new_state;

        char buffer[MAX_TX_BUFFER];
        snprintf(buffer, sizeof(buffer),
            "{\"type\":\"relay\",\"id\":%d,\"state\":%s}",
            id, new_state ? "true" : "false");

        broadcast(buffer);
    }

    /**
     * EventBus callback: alarm
     */
    void on_alarm(const core::Event& event) {
        if (!initialized_ || client_count_ == 0) return;

        // Alarms always broadcast immediately
        char buffer[MAX_TX_BUFFER];
        snprintf(buffer, sizeof(buffer),
            "{\"type\":\"alarm\",\"fermenter\":%d,\"active\":%s}",
            event.source_id, event.data.state ? "true" : "false");

        broadcast(buffer);
    }

    /**
     * Convert sensor quality to string
     */
    static const char* quality_to_string(core::SensorQuality q) {
        switch (q) {
            case core::SensorQuality::GOOD: return "GOOD";
            case core::SensorQuality::BAD: return "BAD";
            case core::SensorQuality::SUSPECT: return "SUSPECT";
            case core::SensorQuality::WARMING_UP: return "WARMING_UP";
            default: return "UNKNOWN";
        }
    }

#else
    // Simulator/native build stubs
    bool initialize(void* server, core::StateManager* state, core::EventBus* events) {
        (void)server; (void)state; (void)events;
        return false;
    }
    void set_session_validator(SessionValidator validator, void* ctx) {
        (void)validator; (void)ctx;
    }
    void stop() {}
    void process() {}
    int get_client_count() const { return 0; }
#endif
};

} // namespace modules
