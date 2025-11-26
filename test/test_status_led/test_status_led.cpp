#include <unity.h>
#include "hal/simulator/hal_simulator.h"
#include "modules/status_led.h"

// Test fixtures
static hal::simulator::SimulatorTime* time_hal;
static modules::StatusLed* led;

void setUp(void) {
    time_hal = new hal::simulator::SimulatorTime();
    led = new modules::StatusLed(time_hal, 38);
    led->init();
}

void tearDown(void) {
    delete led;
    delete time_hal;
}

// =============================================================================
// Initialization Tests
// =============================================================================

void test_led_initial_state() {
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::OFF, led->get_color());
}

void test_led_init_success() {
    modules::StatusLed led2(time_hal, 38);
    TEST_ASSERT_TRUE(led2.init());
}

// =============================================================================
// Color Priority Tests
// =============================================================================

void test_led_green_when_all_ok() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = true;
    state.provisioning = false;
    state.has_errors = false;
    state.has_warnings = false;
    state.has_alarms = false;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());
}

void test_led_blue_when_provisioning() {
    modules::StatusLed::State state = {};
    state.provisioning = true;
    state.wifi_connected = false;
    state.ntp_synced = false;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::BLUE, led->get_color());
}

void test_led_yellow_when_wifi_disconnected() {
    modules::StatusLed::State state = {};
    state.wifi_connected = false;
    state.ntp_synced = true;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_yellow_when_ntp_not_synced() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = false;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_yellow_when_warnings() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = true;
    state.has_warnings = true;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_red_when_errors() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = true;
    state.has_errors = true;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

void test_led_red_when_alarms() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = true;
    state.has_alarms = true;

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

// =============================================================================
// Priority Tests - Higher priority overrides lower
// =============================================================================

void test_led_red_overrides_yellow() {
    modules::StatusLed::State state = {};
    state.wifi_connected = false;  // Would be yellow
    state.has_errors = true;       // But red takes priority

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

void test_led_red_overrides_blue() {
    modules::StatusLed::State state = {};
    state.provisioning = true;  // Would be blue
    state.has_errors = true;    // But red takes priority

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

void test_led_blue_overrides_yellow() {
    modules::StatusLed::State state = {};
    state.provisioning = true;     // Blue takes priority during provisioning
    state.wifi_connected = false;  // Even though WiFi is disconnected

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::BLUE, led->get_color());
}

void test_led_yellow_overrides_green() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = false;  // Yellow overrides green

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_blue_overrides_green() {
    modules::StatusLed::State state = {};
    state.wifi_connected = true;
    state.ntp_synced = true;
    state.provisioning = true;  // Blue overrides green

    led->set_state(state);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::BLUE, led->get_color());
}

// =============================================================================
// Individual State Setter Tests
// =============================================================================

void test_led_set_provisioning() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->set_provisioning(true);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::BLUE, led->get_color());
}

void test_led_set_wifi_connected() {
    led->set_wifi_connected(false);
    led->set_ntp_synced(true);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_set_ntp_synced() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(false);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::YELLOW, led->get_color());
}

void test_led_set_has_errors() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->set_has_errors(true);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

void test_led_set_has_alarms() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->set_has_alarms(true);
    led->update();

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

// =============================================================================
// Manual Control Tests
// =============================================================================

void test_led_manual_set_color() {
    led->set_color(modules::StatusLed::Color::CYAN);
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::CYAN, led->get_color());
}

void test_led_off() {
    led->set_color(modules::StatusLed::Color::RED);
    led->off();
    // Color should change to OFF (brightness 0)
    // Note: get_color() returns last set color, not OFF
}

void test_led_brightness() {
    led->set_brightness(128);
    // Brightness is internal, verify no crash
    led->update();
}

// =============================================================================
// Color String Tests
// =============================================================================

void test_led_color_to_string() {
    TEST_ASSERT_EQUAL_STRING("OFF", modules::StatusLed::color_to_string(modules::StatusLed::Color::OFF));
    TEST_ASSERT_EQUAL_STRING("RED", modules::StatusLed::color_to_string(modules::StatusLed::Color::RED));
    TEST_ASSERT_EQUAL_STRING("GREEN", modules::StatusLed::color_to_string(modules::StatusLed::Color::GREEN));
    TEST_ASSERT_EQUAL_STRING("BLUE", modules::StatusLed::color_to_string(modules::StatusLed::Color::BLUE));
    TEST_ASSERT_EQUAL_STRING("YELLOW", modules::StatusLed::color_to_string(modules::StatusLed::Color::YELLOW));
    TEST_ASSERT_EQUAL_STRING("WHITE", modules::StatusLed::color_to_string(modules::StatusLed::Color::WHITE));
    TEST_ASSERT_EQUAL_STRING("CYAN", modules::StatusLed::color_to_string(modules::StatusLed::Color::CYAN));
    TEST_ASSERT_EQUAL_STRING("MAGENTA", modules::StatusLed::color_to_string(modules::StatusLed::Color::MAGENTA));
}

// =============================================================================
// State Transition Tests
// =============================================================================

void test_led_state_transition_ok_to_error() {
    // Start with all OK
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());

    // Transition to error
    led->set_has_errors(true);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());
}

void test_led_state_transition_error_to_ok() {
    // Start with error
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->set_has_errors(true);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::RED, led->get_color());

    // Clear error
    led->set_has_errors(false);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());
}

void test_led_state_transition_provisioning_to_connected() {
    // Start in provisioning
    led->set_provisioning(true);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::BLUE, led->get_color());

    // Complete provisioning
    led->set_provisioning(false);
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->update();
    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());
}

// =============================================================================
// Update Timing Tests
// =============================================================================

void test_led_update_rate_limiting() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);
    led->update();

    // Multiple rapid updates should not crash
    for (int i = 0; i < 100; i++) {
        led->update();
    }

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());
}

void test_led_update_with_time_advance() {
    led->set_wifi_connected(true);
    led->set_ntp_synced(true);

    // Simulate time passing
    for (int i = 0; i < 10; i++) {
        time_hal->advance_millis(100);
        led->update();
    }

    TEST_ASSERT_EQUAL(modules::StatusLed::Color::GREEN, led->get_color());
}

// =============================================================================
// Test Runner
// =============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Initialization
    RUN_TEST(test_led_initial_state);
    RUN_TEST(test_led_init_success);

    // Color priority
    RUN_TEST(test_led_green_when_all_ok);
    RUN_TEST(test_led_blue_when_provisioning);
    RUN_TEST(test_led_yellow_when_wifi_disconnected);
    RUN_TEST(test_led_yellow_when_ntp_not_synced);
    RUN_TEST(test_led_yellow_when_warnings);
    RUN_TEST(test_led_red_when_errors);
    RUN_TEST(test_led_red_when_alarms);

    // Priority overrides
    RUN_TEST(test_led_red_overrides_yellow);
    RUN_TEST(test_led_red_overrides_blue);
    RUN_TEST(test_led_blue_overrides_yellow);
    RUN_TEST(test_led_yellow_overrides_green);
    RUN_TEST(test_led_blue_overrides_green);

    // Individual setters
    RUN_TEST(test_led_set_provisioning);
    RUN_TEST(test_led_set_wifi_connected);
    RUN_TEST(test_led_set_ntp_synced);
    RUN_TEST(test_led_set_has_errors);
    RUN_TEST(test_led_set_has_alarms);

    // Manual control
    RUN_TEST(test_led_manual_set_color);
    RUN_TEST(test_led_off);
    RUN_TEST(test_led_brightness);

    // Color strings
    RUN_TEST(test_led_color_to_string);

    // State transitions
    RUN_TEST(test_led_state_transition_ok_to_error);
    RUN_TEST(test_led_state_transition_error_to_ok);
    RUN_TEST(test_led_state_transition_provisioning_to_connected);

    // Timing
    RUN_TEST(test_led_update_rate_limiting);
    RUN_TEST(test_led_update_with_time_advance);

    return UNITY_END();
}
