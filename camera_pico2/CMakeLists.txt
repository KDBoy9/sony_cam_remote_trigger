# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(camera_pico2 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(camera_pico2
    main.c
    camera_control.c
    ptp_host.c
    sony_ptp.c
    applog.c
    usb_descriptors.c
    sx127x.c
    lora_proto.c
    lora_link.c
    lora_setup.c
)

pico_set_program_name(camera_pico2 "camera_pico2")
pico_set_program_version(camera_pico2 "1.0")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(camera_pico2 0)
pico_enable_stdio_usb(camera_pico2 0)

# Add the standard library to the build
target_link_libraries(camera_pico2
        pico_stdlib pico_multicore pico_util pico_unique_id hardware_clocks
        hardware_spi hardware_gpio tinyusb_device tinyusb_host tinyusb_pico_pio_usb)
target_compile_definitions(camera_pico2 PRIVATE CAMERA_CDC_LOG=1 LOG_LEVEL=2 PICO_STACK_SIZE=0x1000)

# Add the standard include files to the build
target_include_directories(camera_pico2 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

if (NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "Set PICO_PIO_USB_PATH to an installed Pico-PIO-USB checkout")
endif()

pico_add_extra_outputs(camera_pico2)

