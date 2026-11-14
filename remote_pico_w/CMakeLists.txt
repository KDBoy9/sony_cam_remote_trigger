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
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(remote_pico_w C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(remote_pico_w
    main.c
    web_server.c
    sx127x.c
    lora_proto.c
    lora_link.c
    lora_setup.c
    dhcpserver.c
)

pico_set_program_name(remote_pico_w "remote_pico_w")
pico_set_program_version(remote_pico_w "1.0")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(remote_pico_w 0)
pico_enable_stdio_usb(remote_pico_w 1)

# Add the standard library to the build
target_link_libraries(remote_pico_w
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(remote_pico_w PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(remote_pico_w 
    pico_stdlib hardware_spi hardware_gpio
    pico_cyw43_arch_lwip_poll )

target_compile_definitions(remote_pico_w PRIVATE LOG_LEVEL=2 PICO_STACK_SIZE=0x1000)

pico_add_extra_outputs(remote_pico_w)

