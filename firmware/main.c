#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main() {
    // Pin 25 is the onboard LED on the original Raspberry Pi Pico
    const uint LED_PIN = 25;
    
    // Servo pins
    const uint SERVO_1_PIN = 2; // Sine wave
    const uint SERVO_2_PIN = 3; // Cosine wave

    // Setup LED PWM
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);
    uint led_slice = pwm_gpio_to_slice_num(LED_PIN);
    pwm_set_wrap(led_slice, 255);
    pwm_set_enabled(led_slice, true);

    // Setup Servo PWM (GPIO 2 and 3 share slice 1)
    gpio_set_function(SERVO_1_PIN, GPIO_FUNC_PWM);
    gpio_set_function(SERVO_2_PIN, GPIO_FUNC_PWM);
    uint servo_slice = pwm_gpio_to_slice_num(SERVO_1_PIN);

    // System clock is 125 MHz. 125 MHz / 125.0 = 1 MHz PWM clock.
    // Wrap at 19999 gives a period of 20000 ticks (20 ms), which is 50 Hz.
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 125.0f);
    pwm_config_set_wrap(&config, 19999);
    pwm_init(servo_slice, &config, true);

    int fade = 0;
    bool going_up = true;
    
    float t = 0.0f;
    const float dt = 0.02f; // Animation speed for the sine/cosine waves

    while (true) {
        // --- LED fading logic ---
        pwm_set_gpio_level(LED_PIN, fade * fade / 255);

        if (going_up) {
            fade++;
            if (fade > 255) {
                fade = 255;
                going_up = false;
            }
        } else {
            fade--;
            if (fade < 0) {
                fade = 0;
                going_up = true;
            }
        }

        // --- Servo wave logic ---
        // Some servos support a wider pulse range than 500-2500us for extended angles.
        // We define the center and amplitude here so you can tune it to your specific servo's mechanical limits.
        #define SERVO_CENTER 1500
        #define SERVO_AMPLITUDE 1000 // 1500 +/- 1000 = 500us to 2500us.

        uint16_t servo_1_level = (uint16_t)(SERVO_CENTER + SERVO_AMPLITUDE * sin(t));
        uint16_t servo_2_level = (uint16_t)(SERVO_CENTER + SERVO_AMPLITUDE * cos(t));
        
        pwm_set_gpio_level(SERVO_1_PIN, servo_1_level);
        pwm_set_gpio_level(SERVO_2_PIN, servo_2_level);

        // Advance time
        t += dt;
        if (t >= 2.0f * M_PI) {
            t -= 2.0f * M_PI;
        }

        sleep_ms(5);
    }
}
