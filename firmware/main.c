#include "pico/stdlib.h"
#include "hardware/pwm.h"

int main() {
    // Pin 25 is the onboard LED on the original Raspberry Pi Pico
    const uint LED_PIN = 25;

    // Tell GPIO 25 that it is allocated to the PWM
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);

    // Find out which PWM slice is connected to GPIO 25
    uint slice_num = pwm_gpio_to_slice_num(LED_PIN);

    // Set period of 256 cycles (0 to 255 inclusive)
    pwm_set_wrap(slice_num, 255);
    
    // Set the PWM running
    pwm_set_enabled(slice_num, true);

    int fade = 0;
    bool going_up = true;

    while (true) {
        // Square the fade value to make it look more linear to the human eye,
        // (gamma correction approximation)
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
        sleep_ms(5); // Adjust for fade speed
    }
}
