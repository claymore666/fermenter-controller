#pragma once

#include <cstdint>

namespace core {

/**
 * Ring buffer for network bandwidth utilization history
 * Stores 30 minutes of history at 15-second intervals
 * Calculates utilization as percentage of link speed
 */
class NetworkHistory {
public:
    static constexpr uint8_t MAX_SAMPLES = 120;       // 30 minutes at 15s intervals
    static constexpr uint16_t SAMPLE_INTERVAL_MS = 15000;  // 15 seconds

    NetworkHistory()
        : write_index_(0), count_(0),
          last_tx_(0), last_rx_(0),
          current_tx_(0), current_rx_(0),
          link_speed_mbps_(0), channel_(0) {
        for (uint8_t i = 0; i < MAX_SAMPLES; i++) {
            samples_[i] = 0;
        }
    }

    /**
     * Add bytes to the traffic counters (call from HTTP server)
     */
    void add_tx_bytes(uint32_t bytes) {
        current_tx_ += bytes;
    }

    void add_rx_bytes(uint32_t bytes) {
        current_rx_ += bytes;
    }

    /**
     * Update link info and calculate utilization sample
     * Call every SAMPLE_INTERVAL_MS
     * @param link_speed_mbps Current negotiated link speed
     * @param channel Current WiFi channel
     */
    void sample(uint32_t link_speed_mbps, uint8_t channel) {
        link_speed_mbps_ = link_speed_mbps;
        channel_ = channel;

        // Calculate bytes transferred since last sample
        uint64_t delta_tx = current_tx_ - last_tx_;
        uint64_t delta_rx = current_rx_ - last_rx_;
        uint64_t delta_total = delta_tx + delta_rx;

        // Update last values for next delta calculation
        last_tx_ = current_tx_;
        last_rx_ = current_rx_;

        // Calculate utilization percentage
        uint8_t utilization = 0;
        if (link_speed_mbps > 0 && delta_total > 0) {
            // Convert bytes to bits: delta_total * 8
            // Calculate bits per second: (delta_bits) / (interval_sec)
            // interval_sec = 15
            uint64_t bits_per_sec = (delta_total * 8) / (SAMPLE_INTERVAL_MS / 1000);

            // Link speed in bits per second
            uint64_t link_bps = (uint64_t)link_speed_mbps * 1000000ULL;

            // Calculate percentage (capped at 100%)
            if (link_bps > 0) {
                uint64_t pct = (bits_per_sec * 100) / link_bps;
                utilization = (pct > 100) ? 100 : (uint8_t)pct;
            }
        }

        // Store sample
        samples_[write_index_] = utilization;
        write_index_ = (write_index_ + 1) % MAX_SAMPLES;

        if (count_ < MAX_SAMPLES) {
            count_++;
        }
    }

    /**
     * Get all samples in chronological order (oldest to newest)
     * @param buffer Output buffer (must be at least MAX_SAMPLES bytes)
     * @return Number of samples copied
     */
    uint8_t get_samples(uint8_t* buffer) const {
        if (!buffer || count_ == 0) return 0;

        // Calculate start index (oldest sample)
        uint8_t start_idx;
        if (count_ < MAX_SAMPLES) {
            start_idx = 0;
        } else {
            start_idx = write_index_;
        }

        // Copy samples in chronological order
        for (uint8_t i = 0; i < count_; i++) {
            uint8_t idx = (start_idx + i) % MAX_SAMPLES;
            buffer[i] = samples_[idx];
        }

        return count_;
    }

    uint8_t get_sample_count() const { return count_; }
    uint32_t get_link_speed_mbps() const { return link_speed_mbps_; }
    uint8_t get_channel() const { return channel_; }
    uint64_t get_total_tx_bytes() const { return current_tx_; }
    uint64_t get_total_rx_bytes() const { return current_rx_; }

    /**
     * Get the maximum value in the history (for Y-axis scaling)
     */
    uint8_t get_max_value() const {
        uint8_t max_val = 0;
        for (uint8_t i = 0; i < count_; i++) {
            if (samples_[i] > max_val) {
                max_val = samples_[i];
            }
        }
        return max_val;
    }

private:
    uint8_t samples_[MAX_SAMPLES];  // Utilization 0-100%
    uint8_t write_index_;
    uint8_t count_;

    uint64_t last_tx_;      // TX bytes at last sample
    uint64_t last_rx_;      // RX bytes at last sample
    uint64_t current_tx_;   // Current cumulative TX bytes
    uint64_t current_rx_;   // Current cumulative RX bytes

    uint32_t link_speed_mbps_;
    uint8_t channel_;
};

} // namespace core
