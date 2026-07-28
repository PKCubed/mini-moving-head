cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(rp2040_pwm_led C CXX ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(rp2040_pwm_led
    main.c
)

# pull in common dependencies
target_link_libraries(rp2040_pwm_led pico_stdlib hardware_pwm)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(rp2040_pwm_led)
