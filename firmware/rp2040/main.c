#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "bsp/board.h"
#include "tusb.h"
#include "msc_disk.h"

#include <math.h>

// Pin definitions
const uint LED_PIN = 25;
const uint SERVO_1_PIN = 2; // Pan (default)
const uint SERVO_2_PIN = 3; // Tilt (default)
const uint LED2_PIN = 7;    // High power LED
const uint DMX_RX_PIN = 1;  // DMX RX

// DMX State
volatile int dmx_channel = -1;
volatile uint8_t dmx_data[513];
volatile uint32_t last_dmx_time = 0;

void on_uart_rx() {
    while (uart_is_readable(uart0)) {
        // Read the Data Register and the Receive Status Register
        uint32_t dr = uart_get_hw(uart0)->dr;
        uint8_t data = dr & 0xFF;
        
        // Check for Framing Error (which indicates a BREAK condition in DMX)
        if (dr & UART_UARTDR_FE_BITS) {
            // Break detected, reset channel counter
            dmx_channel = 0;
            continue; // Skip processing this byte as data
        }

        if (dmx_channel >= 0 && dmx_channel <= 512) {
            dmx_data[dmx_channel] = data;
            if (dmx_channel > 0) {
                last_dmx_time = time_us_32();
            }
            dmx_channel++;
        }
    }
}

int main() {
    // Board init for TinyUSB
    board_init();
    tusb_init();
    msc_disk_init();

    // Setup LED PWM (GPIO 25)
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);
    uint led_slice = pwm_gpio_to_slice_num(LED_PIN);
    pwm_set_clkdiv(led_slice, 125.0f);
    pwm_set_wrap(led_slice, 1999);
    pwm_set_enabled(led_slice, true);

    // Setup LED2 PWM (GPIO 7)
    gpio_set_function(LED2_PIN, GPIO_FUNC_PWM);
    uint led2_slice = pwm_gpio_to_slice_num(LED2_PIN);
    pwm_set_clkdiv(led2_slice, 125.0f);
    pwm_set_wrap(led2_slice, 1999);
    pwm_set_enabled(led2_slice, true);

    // Setup Servo PWM (GPIO 2 and 3 share slice 1)
    gpio_set_function(SERVO_1_PIN, GPIO_FUNC_PWM);
    gpio_set_function(SERVO_2_PIN, GPIO_FUNC_PWM);
    uint servo_slice = pwm_gpio_to_slice_num(SERVO_1_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 125.0f); // 1 MHz clock
    pwm_config_set_wrap(&config, 19999);    // 50 Hz
    pwm_init(servo_slice, &config, true);

    // DMX UART Setup
    uart_init(uart0, 250000);
    uart_set_format(uart0, 8, 2, UART_PARITY_NONE);
    gpio_set_function(DMX_RX_PIN, GPIO_FUNC_UART);
    
    // Disable UART FIFO to get accurate error flags per byte
    uart_set_fifo_enabled(uart0, false);
    
    // Set up UART IRQ
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);

    uint32_t last_led_toggle = 0;
    bool led_state = false;

    while (true) {
        // USB tasks
        tud_task();
        msc_disk_task();

        uint32_t now = time_us_32();
        
        // Status LED logic (blink slowly if DMX is active)
        bool dmx_active = (now - last_dmx_time < 1000000);
        if (dmx_active) { // 1 second timeout
            if (now - last_led_toggle > 500000) { // 1 Hz blink (500ms toggle)
                led_state = !led_state;
                pwm_set_gpio_level(LED_PIN, led_state ? 1999 : 0);
                last_led_toggle = now;
            }
        } else {
            pwm_set_gpio_level(LED_PIN, 0); // Off if no DMX
        }

        if (cfg_test_mode) {
            // Test mode: slow circle to test boundaries
            float t = (now / 1000000.0f) * 0.5f; // 0.5 radians per sec
            int16_t pan_offset = (int16_t)((cfg_pan_range / 2.0f) * sinf(t));
            int16_t tilt_offset = (int16_t)((cfg_tilt_range / 2.0f) * cosf(t));
            
            uint16_t pan_pwm = cfg_pan_center + pan_offset;
            uint16_t tilt_pwm = cfg_tilt_center + tilt_offset;
            
            // Slowly fade LED too
            uint32_t dim_pwm = (uint32_t)(999.0f + 999.0f * sinf(t * 2.0f));

            if (cfg_swap_pan_tilt) {
                pwm_set_gpio_level(SERVO_1_PIN, tilt_pwm);
                pwm_set_gpio_level(SERVO_2_PIN, pan_pwm);
            } else {
                pwm_set_gpio_level(SERVO_1_PIN, pan_pwm);
                pwm_set_gpio_level(SERVO_2_PIN, tilt_pwm);
            }
            
            pwm_set_gpio_level(LED2_PIN, dim_pwm);
        } else if (dmx_active && dmx_data[0] == 0) {
            uint16_t addr = cfg_dmx_start_address;
            if (addr >= 1 && addr <= 510) {
                uint8_t dmx_pan = dmx_data[addr];
                uint8_t dmx_tilt = dmx_data[addr + 1];
                uint8_t dmx_dim = dmx_data[addr + 2];

                // Calculate Pan/Tilt PWM values based on config
                // 0-255 maps to (center - range/2) to (center + range/2)
                int16_t pan_offset = ((dmx_pan - 127) * (int32_t)cfg_pan_range) / 255;
                int16_t tilt_offset = ((dmx_tilt - 127) * (int32_t)cfg_tilt_range) / 255;
                
                uint16_t pan_pwm = cfg_pan_center + pan_offset;
                uint16_t tilt_pwm = cfg_tilt_center + tilt_offset;
                
                // Dimmer mapping: 0-255 to 0-1999 with simple quadratic curve for better dimming
                uint32_t dim_pwm = (dmx_dim * dmx_dim * 1999) / (255 * 255);
                
                // Apply
                if (cfg_swap_pan_tilt) {
                    pwm_set_gpio_level(SERVO_1_PIN, tilt_pwm);
                    pwm_set_gpio_level(SERVO_2_PIN, pan_pwm);
                } else {
                    pwm_set_gpio_level(SERVO_1_PIN, pan_pwm);
                    pwm_set_gpio_level(SERVO_2_PIN, tilt_pwm);
                }
                
                pwm_set_gpio_level(LED2_PIN, dim_pwm);
            }
        } else if (!dmx_active) {
            // No DMX data: center servos and turn off LED
            if (cfg_swap_pan_tilt) {
                pwm_set_gpio_level(SERVO_1_PIN, cfg_tilt_center);
                pwm_set_gpio_level(SERVO_2_PIN, cfg_pan_center);
            } else {
                pwm_set_gpio_level(SERVO_1_PIN, cfg_pan_center);
                pwm_set_gpio_level(SERVO_2_PIN, cfg_tilt_center);
            }
            pwm_set_gpio_level(LED2_PIN, 0);
        }
    }
}
