#pragma once

#include <cstdint>

namespace modules {

/**
 * PID Controller with back-calculation anti-windup
 * Standard PID algorithm with improved anti-windup using back-calculation
 * method to prevent integral accumulation when output saturates.
 */
class PIDController {
public:
    PIDController(float kp = 2.0f, float ki = 0.1f, float kd = 1.0f)
        : kp_(kp), ki_(ki), kd_(kd)
        , output_min_(0.0f), output_max_(100.0f)
        , integral_(0.0f), last_error_(0.0f)
        , last_input_(0.0f), last_output_(0.0f)
        , prev_saturation_error_(0.0f)
        , tracking_gain_(1.0f)  // Back-calculation tracking gain (Kt = 1/Ti typically)
        , first_run_(true) {}

    /**
     * Compute PID output
     * @param setpoint Target value
     * @param input Current measured value
     * @return Control output (clamped to output_min..output_max)
     */
    float compute(float setpoint, float input) {
        float error = setpoint - input;

        // Proportional term
        float p_term = kp_ * error;

        // Integral term - accumulate BEFORE computing output
        integral_ += ki_ * error;

        // Apply back-calculation correction from PREVIOUS iteration's saturation
        // This prevents windup while maintaining immediate integral response
        integral_ += prev_saturation_error_ * tracking_gain_;

        // Clamp integral to safety limits
        if (integral_ > output_max_) {
            integral_ = output_max_;
        } else if (integral_ < output_min_) {
            integral_ = output_min_;
        }

        // Derivative term (on input to avoid derivative kick on setpoint change)
        float d_term = 0;
        if (!first_run_) {
            d_term = -kd_ * (input - last_input_);
        }

        // Calculate output
        float output_unsaturated = p_term + integral_ + d_term;

        // Clamp output to limits
        float output = output_unsaturated;
        if (output > output_max_) {
            output = output_max_;
        } else if (output < output_min_) {
            output = output_min_;
        }

        // Store saturation error for next iteration's back-calculation
        prev_saturation_error_ = output - output_unsaturated;

        // Save state for next iteration
        last_error_ = error;
        last_input_ = input;
        last_output_ = output;
        first_run_ = false;

        return output;
    }

    /**
     * Set output limits
     */
    void set_output_limits(float min, float max) {
        if (min >= max) return;
        output_min_ = min;
        output_max_ = max;

        // Clamp integral to new limits
        if (integral_ > output_max_) {
            integral_ = output_max_;
        } else if (integral_ < output_min_) {
            integral_ = output_min_;
        }
    }

    /**
     * Set PID parameters
     */
    void set_tunings(float kp, float ki, float kd) {
        if (kp < 0 || ki < 0 || kd < 0) return;
        kp_ = kp;
        ki_ = ki;
        kd_ = kd;
    }

    /**
     * Set anti-windup tracking gain
     * Higher values = faster recovery from saturation
     * Typical range: 0.5 to 2.0 (default 1.0)
     */
    void set_tracking_gain(float kt) {
        if (kt >= 0) tracking_gain_ = kt;
    }

    /**
     * Reset controller state
     * Call when switching modes or restarting control
     */
    void reset() {
        integral_ = 0;
        last_error_ = 0;
        last_input_ = 0;
        last_output_ = 0;
        prev_saturation_error_ = 0;
        first_run_ = true;
    }

    /**
     * Initialize controller with current output
     * Useful for bumpless transfer when switching from manual to auto
     */
    void initialize(float output, float input) {
        integral_ = output;
        last_input_ = input;
        last_output_ = output;
        prev_saturation_error_ = 0;
        first_run_ = false;

        // Clamp integral to output limits
        if (integral_ > output_max_) {
            integral_ = output_max_;
        } else if (integral_ < output_min_) {
            integral_ = output_min_;
        }
    }

    // Getters
    float get_kp() const { return kp_; }
    float get_ki() const { return ki_; }
    float get_kd() const { return kd_; }
    float get_tracking_gain() const { return tracking_gain_; }
    float get_output_min() const { return output_min_; }
    float get_output_max() const { return output_max_; }
    float get_integral() const { return integral_; }
    float get_last_error() const { return last_error_; }
    float get_last_output() const { return last_output_; }

private:
    // Tuning parameters
    float kp_;
    float ki_;
    float kd_;

    // Output limits
    float output_min_;
    float output_max_;

    // State
    float integral_;
    float last_error_;
    float last_input_;
    float last_output_;
    float prev_saturation_error_;  // For back-calculation anti-windup
    float tracking_gain_;  // Back-calculation anti-windup gain
    bool first_run_;
};

/**
 * PID Autotuner using relay method
 * Determines optimal PID parameters by inducing oscillation
 */
class PIDAutotuner {
public:
    enum class State {
        IDLE,
        RUNNING,
        COMPLETE,
        FAILED
    };

    PIDAutotuner(float output_low = 0.0f, float output_high = 100.0f, float hysteresis = 0.5f)
        : output_low_(output_low)
        , output_high_(output_high)
        , hysteresis_(hysteresis)
        , state_(State::IDLE)
        , setpoint_(0)
        , output_(output_low)
        , peak_count_(0)
        , last_peak_time_(0)
        , last_peak_value_(0)
        , peak_high_(0)
        , peak_low_(0)
        , period_sum_(0)
        , amplitude_sum_(0) {}

    /**
     * Start autotuning
     * @param setpoint Target temperature
     */
    void start(float setpoint) {
        state_ = State::RUNNING;
        setpoint_ = setpoint;
        output_ = output_high_;  // Start with high output
        peak_count_ = 0;
        peak_high_ = setpoint;
        peak_low_ = setpoint;
        period_sum_ = 0;
        amplitude_sum_ = 0;
    }

    /**
     * Update autotuner with current input
     * @param input Current temperature
     * @param timestamp Current time in ms
     * @return Output to apply (0 or 100%)
     */
    float update(float input, uint32_t timestamp) {
        if (state_ != State::RUNNING) {
            return output_low_;
        }

        // Relay control with hysteresis
        if (output_ == output_high_ && input > setpoint_ + hysteresis_) {
            // Switch to low
            output_ = output_low_;

            // Record peak
            if (input > peak_high_) {
                peak_high_ = input;
            }

            // Calculate period if we have previous peak
            if (peak_count_ > 0 && last_peak_time_ > 0) {
                uint32_t period = timestamp - last_peak_time_;
                period_sum_ += period;
            }

            last_peak_time_ = timestamp;
            last_peak_value_ = input;
            peak_count_++;

        } else if (output_ == output_low_ && input < setpoint_ - hysteresis_) {
            // Switch to high
            output_ = output_high_;

            // Record trough
            if (input < peak_low_) {
                peak_low_ = input;
            }
        }

        // Check if we have enough cycles (need at least 4 peaks)
        if (peak_count_ >= 5) {
            state_ = State::COMPLETE;
        }

        return output_;
    }

    /**
     * Get calculated PID parameters (Ziegler-Nichols)
     * Call after state is COMPLETE
     */
    void get_pid_params(float& kp, float& ki, float& kd) const {
        if (state_ != State::COMPLETE || peak_count_ < 2) {
            kp = ki = kd = 0;
            return;
        }

        // Calculate ultimate gain and period
        float amplitude = (peak_high_ - peak_low_) / 2.0f;
        float period = period_sum_ / (peak_count_ - 1);  // Average period in ms

        // Ultimate gain: Ku = 4d / (π * a)
        // where d = relay amplitude, a = oscillation amplitude
        float d = (output_high_ - output_low_) / 2.0f;
        float ku = (4.0f * d) / (3.14159f * amplitude);

        // Convert period to seconds
        float pu = period / 1000.0f;

        // Ziegler-Nichols PID tuning
        kp = 0.6f * ku;
        ki = 1.2f * ku / pu;
        kd = 0.075f * ku * pu;
    }

    State get_state() const { return state_; }
    uint8_t get_peak_count() const { return peak_count_; }

    void cancel() { state_ = State::IDLE; }

private:
    float output_low_;
    float output_high_;
    float hysteresis_;

    State state_;
    float setpoint_;
    float output_;

    uint8_t peak_count_;
    uint32_t last_peak_time_;
    float last_peak_value_;
    float peak_high_;
    float peak_low_;
    uint32_t period_sum_;
    float amplitude_sum_;
};

} // namespace modules
