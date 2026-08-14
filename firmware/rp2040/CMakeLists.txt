cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(rp2040_pwm_led C CXX ASM)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(rp2040_pwm_led
    main.c
    msc_disk.c
    usb_descriptors.c
)

target_include_directories(rp2040_pwm_led PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# pull in common dependencies
target_link_libraries(rp2040_pwm_led pico_stdlib hardware_pwm hardware_flash hardware_sync tinyusb_device tinyusb_board)

# increase crystal startup delay for reliable boot on some boards
target_compile_definitions(rp2040_pwm_led PRIVATE PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(rp2040_pwm_led)
