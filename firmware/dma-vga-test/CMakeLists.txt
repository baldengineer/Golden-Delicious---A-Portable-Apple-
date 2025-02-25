# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "C:/Users/balde/Dropbox/Pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(dma-vga-test C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(dma-vga-test dma-vga-test.c )

pico_set_program_name(dma-vga-test "dma-vga-test")
pico_set_program_version(dma-vga-test "0.1")

pico_enable_stdio_uart(dma-vga-test 1)
pico_enable_stdio_usb(dma-vga-test 1)

pico_generate_pio_header(dma-vga-test ${CMAKE_CURRENT_LIST_DIR}/src/TEST_CAP.pio)

target_include_directories(dma-vga-test PRIVATE src)

# Add the standard library to the build
target_link_libraries(dma-vga-test pico_stdlib)

# Add any user requested libraries
target_link_libraries(dma-vga-test
        hardware_dma
        hardware_pio
        )

pico_add_extra_outputs(dma-vga-test)

