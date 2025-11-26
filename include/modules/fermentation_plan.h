#pragma once

#include "hal/interfaces.h"
#include "core/types.h"
#include "core/state_manager.h"
#include "core/event_bus.h"
#include "core/utils.h"
#include <cstring>
#include <cstdio>

namespace modules {

/**
 * Fermentation Plan Manager
 * Manages fermentation plans for all fermenters
 */
class FermentationPlanManager {
public:
    FermentationPlanManager(hal::ITimeInterface* time,
                            hal::IStorageInterface* storage,
                            core::StateManager* state,
                            core::EventBus* events)
        : time_(time)
        , storage_(storage)
        , state_(state)
        , events_(events) {
        // Initialize all plans as inactive
        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            plans_[i].active = false;
        }
    }

    /**
     * Load all plans from storage
     */
    void load_from_storage() {
        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            char key[32];
            snprintf(key, sizeof(key), "plan_%d", i + 1);

            size_t len = sizeof(core::FermentationPlan);
            if (storage_->read_blob(key, &plans_[i], &len)) {
                if (len == sizeof(core::FermentationPlan) && plans_[i].active) {
                    // Plan loaded successfully, validate
                    if (plans_[i].step_count == 0) {
                        plans_[i].active = false;
                    }
                }
            }
        }
    }

    /**
     * Save plan to storage
     */
    bool save_plan(uint8_t fermenter_id) {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return false;
        }

        char key[32];
        snprintf(key, sizeof(key), "plan_%d", fermenter_id);

        return storage_->write_blob(key, &plans_[core::fermenter_id_to_index(fermenter_id)],
                                    sizeof(core::FermentationPlan)) &&
               storage_->commit();
    }

    /**
     * Start a new fermentation plan
     */
    bool start_plan(uint8_t fermenter_id, const core::PlanStep* steps, uint8_t step_count) {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return false;
        }
        if (step_count == 0 || step_count > core::MAX_PLAN_STEPS) {
            return false;
        }

        core::FermentationPlan& plan = plans_[core::fermenter_id_to_index(fermenter_id)];

        plan.fermenter_id = fermenter_id;
        plan.start_time = time_->get_unix_time();
        plan.current_step = 0;
        plan.step_count = step_count;
        plan.active = true;

        for (uint8_t i = 0; i < step_count; i++) {
            plan.steps[i] = steps[i];
        }

        // Save to storage
        save_plan(fermenter_id);

        // Update fermenter state
        auto* ferm = state_->get_fermenter(fermenter_id);
        if (ferm) {
            state_->set_fermenter_mode(fermenter_id, core::FermenterMode::PLAN);
            apply_step_setpoints(fermenter_id, 0);
        }

        // Publish event
        if (events_) {
            events_->publish_plan_step_change(fermenter_id, 0, time_->millis());
        }

        return true;
    }

    /**
     * Stop/cancel a fermentation plan
     */
    void stop_plan(uint8_t fermenter_id) {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return;
        }

        plans_[core::fermenter_id_to_index(fermenter_id)].active = false;
        save_plan(fermenter_id);

        auto* ferm = state_->get_fermenter(fermenter_id);
        if (ferm) {
            state_->set_fermenter_mode(fermenter_id, core::FermenterMode::OFF);
        }
    }

    /**
     * Update all active plans
     * Call this periodically (e.g., every minute)
     */
    void update() {
        uint32_t current_time = time_->get_unix_time();

        for (uint8_t i = 0; i < core::MAX_FERMENTERS; i++) {
            if (!plans_[i].active) continue;

            uint8_t fermenter_id = core::index_to_fermenter_id(i);
            core::FermentationPlan& plan = plans_[i];

            // Calculate elapsed time
            uint32_t elapsed_seconds = current_time - plan.start_time;
            float elapsed_hours = elapsed_seconds / 3600.0f;

            // Find current step based on elapsed time
            float cumulative_hours = 0;
            uint8_t new_step = 0;
            float hours_remaining = 0;

            for (uint8_t s = 0; s < plan.step_count; s++) {
                cumulative_hours += plan.steps[s].duration_hours;

                if (elapsed_hours < cumulative_hours) {
                    new_step = s;
                    hours_remaining = cumulative_hours - elapsed_hours;
                    break;
                }

                // Check if plan is complete
                if (s == plan.step_count - 1) {
                    // Plan complete
                    plan.active = false;
                    save_plan(fermenter_id);

                    auto* ferm = state_->get_fermenter(fermenter_id);
                    if (ferm) {
                        state_->set_fermenter_mode(fermenter_id, core::FermenterMode::MANUAL);
                    }

                    if (events_) {
                        core::Event event;
                        event.type = core::EventType::PLAN_COMPLETE;
                        event.source_id = fermenter_id;
                        event.timestamp = time_->millis();
                        events_->publish(event);
                    }
                    return;
                }
            }

            // Check for step change
            if (new_step != plan.current_step) {
                plan.current_step = new_step;
                save_plan(fermenter_id);

                // Apply new setpoints
                apply_step_setpoints(fermenter_id, new_step);

                // Publish event
                if (events_) {
                    events_->publish_plan_step_change(fermenter_id, new_step, time_->millis());
                }
            }

            // Update fermenter state with progress
            state_->update_fermenter_plan_progress(fermenter_id, new_step, hours_remaining);
        }
    }

    /**
     * Get plan for a fermenter
     */
    const core::FermentationPlan* get_plan(uint8_t fermenter_id) const {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return nullptr;
        }
        return &plans_[core::fermenter_id_to_index(fermenter_id)];
    }

    /**
     * Check if a plan is active
     */
    bool is_plan_active(uint8_t fermenter_id) const {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return false;
        }
        return plans_[core::fermenter_id_to_index(fermenter_id)].active;
    }

    /**
     * Get current step for a fermenter
     */
    uint8_t get_current_step(uint8_t fermenter_id) const {
        if (!core::is_valid_fermenter_id(fermenter_id)) {
            return 0;
        }
        return plans_[core::fermenter_id_to_index(fermenter_id)].current_step;
    }

    /**
     * Get current target temperature for a fermenter
     */
    float get_target_temp(uint8_t fermenter_id) const {
        const auto* plan = get_plan(fermenter_id);
        if (!plan || !plan->active) return 0;
        return plan->steps[plan->current_step].target_temp;
    }

    /**
     * Get current target pressure for a fermenter
     */
    float get_target_pressure(uint8_t fermenter_id) const {
        const auto* plan = get_plan(fermenter_id);
        if (!plan || !plan->active) return 0;
        return plan->steps[plan->current_step].target_pressure;
    }

private:
    hal::ITimeInterface* time_;
    hal::IStorageInterface* storage_;
    core::StateManager* state_;
    core::EventBus* events_;

    core::FermentationPlan plans_[core::MAX_FERMENTERS];

    void apply_step_setpoints(uint8_t fermenter_id, uint8_t step) {
        const auto& plan = plans_[core::fermenter_id_to_index(fermenter_id)];
        if (step >= plan.step_count) return;

        float target_temp = plan.steps[step].target_temp;
        float target_pressure = plan.steps[step].target_pressure;

        auto* ferm = state_->get_fermenter(fermenter_id);
        if (ferm) {
            state_->update_fermenter_temps(fermenter_id, ferm->current_temp, target_temp);
            state_->update_fermenter_pressure(fermenter_id, ferm->current_pressure, target_pressure);
        }
    }
};

} // namespace modules
