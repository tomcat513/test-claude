#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

enum LedState {
    LED_OFF = 0,
    LED_SOLID,           // All normal
    LED_BLINK_SLOW,      // Internet outage (1Hz)
    LED_BLINK_FAST       // Connecting to WiFi (4Hz)
};

void led_init();
void led_set_state(LedState state);
void led_loop();  // Call from main loop to handle blinking

#endif // LED_STATUS_H
