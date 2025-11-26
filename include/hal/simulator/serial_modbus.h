#pragma once

#include "hal/interfaces.h"
#include <cstdint>
#include <string>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace hal {
namespace simulator {

/**
 * Serial MODBUS RTU implementation for native Linux builds.
 * Communicates with external MODBUS simulator via virtual serial port.
 */
class SerialModbus : public IModbusInterface {
public:
    SerialModbus()
        : fd_(-1)
        , timeout_ms_(1000)
        , transaction_count_(0)
        , error_count_(0) {}

    ~SerialModbus() {
        close_port();
    }

    /**
     * Open serial port for MODBUS communication
     */
    bool open_port(const char* port, int baudrate = 9600) {
        fd_ = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            printf("Failed to open serial port: %s\n", port);
            return false;
        }

        // Configure serial port
        struct termios tty;
        memset(&tty, 0, sizeof(tty));

        if (tcgetattr(fd_, &tty) != 0) {
            printf("Error getting port attributes\n");
            close_port();
            return false;
        }

        // Set baud rate
        speed_t speed;
        switch (baudrate) {
            case 4800:   speed = B4800; break;
            case 9600:   speed = B9600; break;
            case 19200:  speed = B19200; break;
            case 38400:  speed = B38400; break;
            case 57600:  speed = B57600; break;
            case 115200: speed = B115200; break;
            default:     speed = B9600; break;
        }
        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        // 8N1, no flow control
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_cflag &= ~(PARENB | PARODD);
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= (CLOCAL | CREAD);

        // Raw input
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // Raw output
        tty.c_oflag &= ~OPOST;

        // Blocking read with timeout
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 10; // 1 second timeout

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            printf("Error setting port attributes\n");
            close_port();
            return false;
        }

        // Flush buffers
        tcflush(fd_, TCIOFLUSH);

        printf("Opened serial port: %s at %d baud\n", port, baudrate);
        return true;
    }

    void close_port() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool is_open() const { return fd_ >= 0; }

    // IModbusInterface implementation

    bool read_holding_registers(uint8_t slave_id, uint16_t start_reg,
                                uint16_t count, uint16_t* values) override {
        if (!is_open() || count == 0 || count > 125) {
            return false;
        }

        transaction_count_++;

        // Build MODBUS RTU request for Read Input Registers (0x04)
        // Note: Using function code 0x04 for input registers
        uint8_t request[8];
        request[0] = slave_id;
        request[1] = 0x04;  // Function code: Read Input Registers
        request[2] = (start_reg >> 8) & 0xFF;
        request[3] = start_reg & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;

        // Calculate CRC
        uint16_t crc = calculate_crc(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        // Send request
        if (write(fd_, request, 8) != 8) {
            error_count_++;
            return false;
        }

        // Wait for response (small delay for device to process)
        usleep(10000);  // 10ms

        // Read response
        uint8_t response[256];
        size_t expected_len = 3 + (count * 2) + 2;  // slave + func + byte_count + data + crc
        size_t bytes_read = 0;
        int retries = 100;  // 1 second total timeout

        while (bytes_read < expected_len && retries > 0) {
            ssize_t n = read(fd_, response + bytes_read, expected_len - bytes_read);
            if (n > 0) {
                bytes_read += n;
            } else {
                usleep(10000);  // 10ms
                retries--;
            }
        }

        if (bytes_read < expected_len) {
            error_count_++;
            return false;
        }

        // Verify response
        if (response[0] != slave_id || response[1] != 0x04) {
            error_count_++;
            return false;
        }

        // Verify CRC
        uint16_t recv_crc = response[bytes_read - 2] | (response[bytes_read - 1] << 8);
        uint16_t calc_crc = calculate_crc(response, bytes_read - 2);
        if (recv_crc != calc_crc) {
            error_count_++;
            return false;
        }

        // Extract register values (big-endian)
        uint8_t byte_count = response[2];
        if (byte_count != count * 2) {
            error_count_++;
            return false;
        }

        for (uint16_t i = 0; i < count; i++) {
            values[i] = (response[3 + i*2] << 8) | response[4 + i*2];
        }

        return true;
    }

    bool write_register(uint8_t slave_id, uint16_t reg, uint16_t value) override {
        if (!is_open()) {
            return false;
        }

        transaction_count_++;

        // Build MODBUS RTU request for Write Single Register (0x06)
        uint8_t request[8];
        request[0] = slave_id;
        request[1] = 0x06;  // Function code
        request[2] = (reg >> 8) & 0xFF;
        request[3] = reg & 0xFF;
        request[4] = (value >> 8) & 0xFF;
        request[5] = value & 0xFF;

        uint16_t crc = calculate_crc(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        // Send request
        if (write(fd_, request, 8) != 8) {
            error_count_++;
            return false;
        }

        // Read echo response
        uint8_t response[8];
        usleep(10000);

        size_t bytes_read = 0;
        int retries = 100;
        while (bytes_read < 8 && retries > 0) {
            ssize_t n = read(fd_, response + bytes_read, 8 - bytes_read);
            if (n > 0) {
                bytes_read += n;
            } else {
                usleep(10000);
                retries--;
            }
        }

        if (bytes_read < 8) {
            error_count_++;
            return false;
        }

        // Verify echo
        return memcmp(request, response, 8) == 0;
    }

    bool write_multiple_registers(uint8_t slave_id, uint16_t start_reg,
                                   uint16_t count, const uint16_t* values) override {
        if (!is_open() || count == 0 || count > 123) {
            return false;
        }

        transaction_count_++;

        // Build MODBUS RTU request for Write Multiple Registers (0x10)
        size_t request_len = 7 + count * 2 + 2;  // header + data + crc
        uint8_t request[256];
        request[0] = slave_id;
        request[1] = 0x10;  // Function code
        request[2] = (start_reg >> 8) & 0xFF;
        request[3] = start_reg & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;
        request[6] = count * 2;  // Byte count

        for (uint16_t i = 0; i < count; i++) {
            request[7 + i*2] = (values[i] >> 8) & 0xFF;
            request[8 + i*2] = values[i] & 0xFF;
        }

        uint16_t crc = calculate_crc(request, 7 + count * 2);
        request[7 + count * 2] = crc & 0xFF;
        request[8 + count * 2] = (crc >> 8) & 0xFF;

        // Send request
        if (write(fd_, request, request_len) != (ssize_t)request_len) {
            error_count_++;
            return false;
        }

        // Read response (8 bytes: slave + func + start_reg + count + crc)
        uint8_t response[8];
        usleep(10000);

        size_t bytes_read = 0;
        int retries = 100;
        while (bytes_read < 8 && retries > 0) {
            ssize_t n = read(fd_, response + bytes_read, 8 - bytes_read);
            if (n > 0) {
                bytes_read += n;
            } else {
                usleep(10000);
                retries--;
            }
        }

        if (bytes_read < 8) {
            error_count_++;
            return false;
        }

        // Verify response
        return response[0] == slave_id && response[1] == 0x10;
    }

    uint32_t get_transaction_count() const override {
        return transaction_count_;
    }

    uint32_t get_error_count() const override {
        return error_count_;
    }

private:
    int fd_;
    uint32_t timeout_ms_;
    uint32_t transaction_count_;
    uint32_t error_count_;

    /**
     * Calculate MODBUS CRC-16
     */
    uint16_t calculate_crc(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;

        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }

        return crc;
    }
};

} // namespace simulator
} // namespace hal
