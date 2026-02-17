#include "led_status.h"

#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // ESP32 DevKitC onboard LED
#endif

static LedState current_state = LED_OFF;
static unsigned long last_toggle = 0;
static bool led_on = false;

void led_init() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
}

void led_set_state(LedState state) {
    current_state = state;
    if (state == LED_SOLID) {
        digitalWrite(LED_BUILTIN, HIGH);
        led_on = true;
    } else if (state == LED_OFF) {
        digitalWrite(LED_BUILTIN, LOW);
        led_on = false;
    }
}

void led_loop() {
    unsigned long now = millis();
    uint32_t interval = 0;

    switch (current_state) {
        case LED_BLINK_SLOW:
            interval = 500;  // 1Hz = toggle every 500ms
            break;
        case LED_BLINK_FAST:
            interval = 125;  // 4Hz = toggle every 125ms
            break;
        default:
            return;  // SOLID and OFF are handled in led_set_state
    }

    if (now - last_toggle >= interval) {
        last_toggle = now;
        led_on = !led_on;
        digitalWrite(LED_BUILTIN, led_on ? HIGH : LOW);
    }
}
