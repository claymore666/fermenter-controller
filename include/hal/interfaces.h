#pragma once

#include <cstdint>
#include <cstddef>

namespace hal {

/**
 * MODBUS RTU communication interface
 * Supports reading/writing registers to MODBUS slave devices
 */
class IModbusInterface {
public:
    virtual ~IModbusInterface() = default;

    /**
     * Read holding registers from a MODBUS slave
     * @param slave_addr Slave device address (1-247)
     * @param start_reg Starting register address
     * @param count Number of registers to read
     * @param data Output buffer for register values
     * @return true on success, false on error
     */
    virtual bool read_holding_registers(
        uint8_t slave_addr,
        uint16_t start_reg,
        uint16_t count,
        uint16_t* data
    ) = 0;

    /**
     * Write a single register to a MODBUS slave
     * @param slave_addr Slave device address
     * @param reg Register address
     * @param value Value to write
     * @return true on success, false on error
     */
    virtual bool write_register(
        uint8_t slave_addr,
        uint16_t reg,
        uint16_t value
    ) = 0;

    /**
     * Write multiple registers to a MODBUS slave
     * @param slave_addr Slave device address
     * @param start_reg Starting register address
     * @param count Number of registers to write
     * @param data Register values to write
     * @return true on success, false on error
     */
    virtual bool write_multiple_registers(
        uint8_t slave_addr,
        uint16_t start_reg,
        uint16_t count,
        const uint16_t* data
    ) = 0;

    /**
     * Get total number of successful transactions
     */
    virtual uint32_t get_transaction_count() const = 0;

    /**
     * Get total number of failed transactions
     */
    virtual uint32_t get_error_count() const = 0;
};

/**
 * GPIO interface for relay control and digital I/O
 */
class IGPIOInterface {
public:
    virtual ~IGPIOInterface() = default;

    /**
     * Set relay state
     * @param relay_id Relay identifier (from config)
     * @param state true = ON, false = OFF
     */
    virtual void set_relay(uint8_t relay_id, bool state) = 0;

    /**
     * Get current relay state
     * @param relay_id Relay identifier
     * @return Current state (true = ON)
     */
    virtual bool get_relay_state(uint8_t relay_id) const = 0;

    /**
     * Read digital input pin
     * @param pin Pin number
     * @return Pin state (true = HIGH)
     */
    virtual bool get_digital_input(uint8_t pin) const = 0;
};

/**
 * Non-volatile storage interface
 * Provides key-value storage that persists across reboots
 */
class IStorageInterface {
public:
    virtual ~IStorageInterface() = default;

    /**
     * Write binary data to storage
     * @param key Storage key (max 15 chars for NVS)
     * @param data Data to write
     * @param len Data length in bytes
     * @return true on success
     */
    virtual bool write_blob(const char* key, const void* data, size_t len) = 0;

    /**
     * Read binary data from storage
     * @param key Storage key
     * @param data Output buffer
     * @param len In: buffer size, Out: actual data size
     * @return true on success, false if key not found or buffer too small
     */
    virtual bool read_blob(const char* key, void* data, size_t* len) = 0;

    /**
     * Write string to storage
     * @param key Storage key
     * @param value Null-terminated string
     * @return true on success
     */
    virtual bool write_string(const char* key, const char* value) = 0;

    /**
     * Read string from storage
     * @param key Storage key
     * @param value Output buffer
     * @param max_len Maximum buffer size
     * @return true on success
     */
    virtual bool read_string(const char* key, char* value, size_t max_len) = 0;

    /**
     * Write integer to storage
     */
    virtual bool write_int(const char* key, int32_t value) = 0;

    /**
     * Read integer from storage
     */
    virtual bool read_int(const char* key, int32_t* value) = 0;

    /**
     * Erase a key from storage
     */
    virtual bool erase_key(const char* key) = 0;

    /**
     * Commit pending writes (for implementations that buffer)
     */
    virtual bool commit() = 0;
};

/**
 * Network interface for WiFi connectivity
 */
class INetworkInterface {
public:
    virtual ~INetworkInterface() = default;

    /**
     * Connect to WiFi network
     * @param ssid Network SSID
     * @param password Network password
     * @return true if connection initiated successfully
     */
    virtual bool connect(const char* ssid, const char* password) = 0;

    /**
     * Check if connected to network
     */
    virtual bool is_connected() const = 0;

    /**
     * Disconnect from network
     */
    virtual bool disconnect() = 0;

    /**
     * Get current IP address
     * @return IP address string or nullptr if not connected
     */
    virtual const char* get_ip_address() const = 0;

    /**
     * Get WiFi signal strength
     * @return RSSI in dBm (negative value)
     */
    virtual int get_rssi() const = 0;
};

/**
 * Time interface for system time and delays
 */
class ITimeInterface {
public:
    virtual ~ITimeInterface() = default;

    /**
     * Get current time in milliseconds since boot
     */
    virtual uint32_t millis() const = 0;

    /**
     * Get current Unix timestamp (seconds since epoch)
     * @return 0 if NTP not synced
     */
    virtual uint32_t get_unix_time() const = 0;

    /**
     * Delay execution
     * @param ms Milliseconds to delay
     */
    virtual void delay_ms(uint32_t ms) = 0;
};

/**
 * CAN bus message structure
 */
struct CANMessage {
    uint32_t id;           // CAN ID (11-bit standard or 29-bit extended)
    uint8_t data[8];       // Message data
    uint8_t len;           // Data length (0-8)
    bool extended;         // true for 29-bit extended ID
    bool rtr;              // Remote transmission request
};

/**
 * CAN bus interface using ESP32 TWAI
 */
class ICANInterface {
public:
    virtual ~ICANInterface() = default;

    /**
     * Initialize CAN bus
     * @param bitrate CAN bitrate in bps (125000, 250000, 500000, 1000000)
     * @return true on success
     */
    virtual bool initialize(uint32_t bitrate = 500000) = 0;

    /**
     * Send CAN message
     * @param msg Message to send
     * @param timeout_ms Timeout in milliseconds (0 = no wait)
     * @return true if message was queued/sent successfully
     */
    virtual bool send(const CANMessage& msg, uint32_t timeout_ms = 100) = 0;

    /**
     * Receive CAN message
     * @param msg Output message buffer
     * @param timeout_ms Timeout in milliseconds (0 = no wait)
     * @return true if message was received
     */
    virtual bool receive(CANMessage& msg, uint32_t timeout_ms = 0) = 0;

    /**
     * Check if messages are available to receive
     */
    virtual bool available() const = 0;

    /**
     * Get number of messages sent
     */
    virtual uint32_t get_tx_count() const = 0;

    /**
     * Get number of messages received
     */
    virtual uint32_t get_rx_count() const = 0;

    /**
     * Get number of errors
     */
    virtual uint32_t get_error_count() const = 0;

    /**
     * Stop CAN bus
     */
    virtual void stop() = 0;
};

} // namespace hal
