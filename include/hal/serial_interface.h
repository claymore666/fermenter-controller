#pragma once

#include <cstdint>
#include <cstddef>

namespace hal {

/**
 * Serial interface for debug console
 */
class ISerialInterface {
public:
    virtual ~ISerialInterface() = default;

    /**
     * Initialize serial port
     * @param baud Baud rate
     * @return true if successful
     */
    virtual bool begin(uint32_t baud) = 0;

    /**
     * Check if data is available to read
     * @return Number of bytes available
     */
    virtual int available() = 0;

    /**
     * Read a single byte
     * @return Byte read, or -1 if none available
     */
    virtual int read() = 0;

    /**
     * Read multiple bytes
     * @param buffer Buffer to read into
     * @param size Maximum bytes to read
     * @return Number of bytes actually read
     */
    virtual size_t read(uint8_t* buffer, size_t size) = 0;

    /**
     * Write data
     * @param data Data to write
     * @param len Length of data
     * @return Number of bytes written
     */
    virtual size_t write(const uint8_t* data, size_t len) = 0;

    /**
     * Write a string
     * @param str Null-terminated string
     * @return Number of bytes written
     */
    virtual size_t print(const char* str) = 0;

    /**
     * Write a string with newline
     * @param str Null-terminated string
     * @return Number of bytes written
     */
    virtual size_t println(const char* str) = 0;

    /**
     * Flush output buffer
     */
    virtual void flush() = 0;
};

} // namespace hal
