#pragma once

#include <cstdint>

namespace core {

/**
 * Ring buffer for CPU usage history
 * Stores 30 minutes of history at 15-second intervals
 * Uses uint8_t (0-100) for memory efficiency
 */
class CpuHistory {
public:
    static constexpr uint8_t MAX_SAMPLES = 120;       // 30 minutes at 15s intervals
    static constexpr uint16_t SAMPLE_INTERVAL_MS = 15000;  // 15 seconds

    CpuHistory() : write_index_(0), count_(0) {
        // Initialize all samples to 0
        for (uint8_t i = 0; i < MAX_SAMPLES; i++) {
            samples_[i] = 0;
        }
    }

    /**
     * Add a new CPU usage sample
     * @param cpu_percent CPU usage 0-100%
     */
    void add_sample(float cpu_percent) {
        // Clamp to valid range and convert to uint8_t
        if (cpu_percent < 0) cpu_percent = 0;
        if (cpu_percent > 100) cpu_percent = 100;

        samples_[write_index_] = static_cast<uint8_t>(cpu_percent + 0.5f);  // Round
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
            // Buffer not full yet, start from 0
            start_idx = 0;
        } else {
            // Buffer full, oldest is at write_index_
            start_idx = write_index_;
        }

        // Copy samples in chronological order
        for (uint8_t i = 0; i < count_; i++) {
            uint8_t idx = (start_idx + i) % MAX_SAMPLES;
            buffer[i] = samples_[idx];
        }

        return count_;
    }

    /**
     * Get the number of samples currently stored
     */
    uint8_t get_sample_count() const {
        return count_;
    }

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
    uint8_t samples_[MAX_SAMPLES];  // Ring buffer storage
    uint8_t write_index_;           // Next write position
    uint8_t count_;                 // Number of samples stored (0 to MAX_SAMPLES)
};

} // namespace core
