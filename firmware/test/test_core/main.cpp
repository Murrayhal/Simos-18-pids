#include "test_harness.h"

void run_settings_tests();
void run_can_decode_tests();
void run_pwm_meter_tests();
void run_valve_controller_tests();
void run_button_tests();
void run_signal_hunter_tests();
void run_console_tests();
void run_led_tests();

int main() {
  run_settings_tests();
  run_can_decode_tests();
  run_pwm_meter_tests();
  run_valve_controller_tests();
  run_button_tests();
  run_signal_hunter_tests();
  run_console_tests();
  run_led_tests();
  return harness::report();
}
