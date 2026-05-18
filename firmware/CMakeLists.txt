cmake_minimum_required(VERSION 3.18)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/pico-sdk" CACHE PATH "Path to pico-sdk submodule")
set(WIZNET_LIBRARY_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/ioLibrary_Driver" CACHE PATH "Path to WIZnet ioLibrary submodule")

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR
        "Pico SDK submodule not found at ${PICO_SDK_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${WIZNET_LIBRARY_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "WIZnet ioLibrary submodule not found at ${WIZNET_LIBRARY_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(vscp_demo_w55rp20_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory("${WIZNET_LIBRARY_PATH}" "${CMAKE_BINARY_DIR}/wiznet_iolib")

add_executable(vscp_demo_w55rp20
    src/main.c
)

target_link_libraries(vscp_demo_w55rp20
    pico_stdlib
    hardware_spi
    hardware_gpio
)

if(TARGET ioLibrary_Driver)
    target_link_libraries(vscp_demo_w55rp20 ioLibrary_Driver)
elseif(TARGET wiznet_ioLibrary_Driver)
    target_link_libraries(vscp_demo_w55rp20 wiznet_ioLibrary_Driver)
elseif(TARGET wizchip)
    target_link_libraries(vscp_demo_w55rp20 wizchip)
else()
    message(WARNING "No known WIZnet ioLibrary target found to link against.")
endif()

pico_enable_stdio_usb(vscp_demo_w55rp20 1)
pico_enable_stdio_uart(vscp_demo_w55rp20 0)
pico_add_extra_outputs(vscp_demo_w55rp20)
