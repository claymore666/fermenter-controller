#pragma once

#include <cstdint>
#include <algorithm>
#include "core/utils.h"

namespace modules {

/**
 * Base filter interface
 */
class IFilter {
public:
    virtual ~IFilter() = default;
    virtual float update(float value) = 0;
    virtual float get_value() const = 0;
    virtual void reset() = 0;
    virtual bool is_ready() const = 0;
};

/**
 * Exponential Moving Average filter
 * Filtered = alpha * new_value + (1 - alpha) * old_filtered
 */
class EMAFilter : public IFilter {
public:
    explicit EMAFilter(float alpha = 0.3f)
        : alpha_(alpha), value_(0), initialized_(false) {}

    float update(float value) override {
        if (!initialized_) {
            value_ = value;
            initialized_ = true;
        } else {
            value_ = alpha_ * value + (1.0f - alpha_) * value_;
        }
        return value_;
    }

    float get_value() const override { return value_; }

    void reset() override {
        value_ = 0;
        initialized_ = false;
    }

    bool is_ready() const override { return initialized_; }

    void set_alpha(float alpha) { alpha_ = alpha; }
    float get_alpha() const { return alpha_; }

private:
    float alpha_;
    float value_;
    bool initialized_;
};

/**
 * Moving Average filter
 * Simple average of last N samples
 */
class MovingAverageFilter : public IFilter {
public:
    static constexpr uint8_t MAX_WINDOW = 16;

    explicit MovingAverageFilter(uint8_t window_size = 5)
        : window_size_(window_size > MAX_WINDOW ? MAX_WINDOW : window_size)
        , index_(0)
        , count_(0)
        , sum_(0) {
        for (uint8_t i = 0; i < MAX_WINDOW; i++) {
            buffer_[i] = 0;
        }
    }

    float update(float value) override {
        // Remove oldest value from sum
        if (count_ == window_size_) {
            sum_ -= buffer_[index_];
        }

        // Add new value
        buffer_[index_] = value;
        sum_ += value;

        // Update index
        index_ = (index_ + 1) % window_size_;

        // Update count
        if (count_ < window_size_) {
            count_++;
        }

        return get_value();
    }

    float get_value() const override {
        if (count_ == 0) return 0;
        return sum_ / count_;
    }

    void reset() override {
        index_ = 0;
        count_ = 0;
        sum_ = 0;
        for (uint8_t i = 0; i < MAX_WINDOW; i++) {
            buffer_[i] = 0;
        }
    }

    bool is_ready() const override {
        return count_ == window_size_;
    }

    uint8_t get_window_size() const { return window_size_; }
    uint8_t get_sample_count() const { return count_; }

private:
    float buffer_[MAX_WINDOW];
    uint8_t window_size_;
    uint8_t index_;
    uint8_t count_;
    float sum_;
};

/**
 * Median filter
 * Returns median of last N samples (excellent for outlier rejection)
 */
class MedianFilter : public IFilter {
public:
    static constexpr uint8_t MAX_WINDOW = 9;

    explicit MedianFilter(uint8_t window_size = 5)
        : window_size_(window_size > MAX_WINDOW ? MAX_WINDOW : window_size)
        , index_(0)
        , count_(0) {
        // Window size should be odd for proper median
        if (window_size_ % 2 == 0) {
            window_size_--;
        }
        if (window_size_ < 3) {
            window_size_ = 3;
        }
        for (uint8_t i = 0; i < MAX_WINDOW; i++) {
            buffer_[i] = 0;
        }
    }

    float update(float value) override {
        buffer_[index_] = value;
        index_ = (index_ + 1) % window_size_;

        if (count_ < window_size_) {
            count_++;
        }

        return get_value();
    }

    float get_value() const override {
        if (count_ == 0) return 0;

        // Copy buffer for sorting
        float sorted[MAX_WINDOW];
        for (uint8_t i = 0; i < count_; i++) {
            sorted[i] = buffer_[i];
        }

        core::bubble_sort(sorted, count_);

        return sorted[count_ / 2];
    }

    void reset() override {
        index_ = 0;
        count_ = 0;
        for (uint8_t i = 0; i < MAX_WINDOW; i++) {
            buffer_[i] = 0;
        }
    }

    bool is_ready() const override {
        return count_ == window_size_;
    }

private:
    float buffer_[MAX_WINDOW];
    uint8_t window_size_;
    uint8_t index_;
    uint8_t count_;
};

/**
 * Dual-rate filter
 * Combines base samples (quality) with extra samples (speed)
 */
class DualRateFilter : public IFilter {
public:
    DualRateFilter(float base_alpha = 0.3f, float extra_alpha = 0.7f, float blend_ratio = 0.7f)
        : base_filter_(base_alpha)
        , extra_filter_(extra_alpha)
        , blend_ratio_(blend_ratio) {}

    /**
     * Update with base sample (higher quality, lower rate)
     */
    float update_base(float value) {
        base_filter_.update(value);
        return get_value();
    }

    /**
     * Update with extra sample (lower quality, higher rate)
     */
    float update_extra(float value) {
        extra_filter_.update(value);
        return get_value();
    }

    // Default update goes to extra samples
    float update(float value) override {
        return update_extra(value);
    }

    float get_value() const override {
        if (!base_filter_.is_ready()) {
            return extra_filter_.get_value();
        }
        if (!extra_filter_.is_ready()) {
            return base_filter_.get_value();
        }
        // Blend both filters
        return blend_ratio_ * extra_filter_.get_value() +
               (1.0f - blend_ratio_) * base_filter_.get_value();
    }

    void reset() override {
        base_filter_.reset();
        extra_filter_.reset();
    }

    bool is_ready() const override {
        return base_filter_.is_ready() || extra_filter_.is_ready();
    }

    float get_base_value() const { return base_filter_.get_value(); }
    float get_extra_value() const { return extra_filter_.get_value(); }

    void set_blend_ratio(float ratio) { blend_ratio_ = ratio; }

private:
    EMAFilter base_filter_;
    EMAFilter extra_filter_;
    float blend_ratio_;
};

/**
 * No-op filter (passthrough)
 */
class NoFilter : public IFilter {
public:
    float update(float value) override {
        value_ = value;
        return value_;
    }

    float get_value() const override { return value_; }
    void reset() override { value_ = 0; }
    bool is_ready() const override { return true; }

private:
    float value_ = 0;
};

} // namespace modules
