cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD wiznet_w5100s_evb_pico CACHE STRING "Target board")
endif()

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/pico-sdk" CACHE PATH "Path to pico-sdk submodule")
set(WIZNET_LIBRARY_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/ioLibrary_Driver" CACHE PATH "Path to WIZnet ioLibrary submodule")
set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/FreeRTOS-Kernel" CACHE PATH "Path to FreeRTOS-Kernel submodule")

if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR
        "Pico SDK submodule not found at ${PICO_SDK_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${WIZNET_LIBRARY_PATH}/Ethernet/wizchip_conf.c")
    message(FATAL_ERROR
        "WIZnet ioLibrary submodule not found at ${WIZNET_LIBRARY_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

if(NOT EXISTS "${FREERTOS_KERNEL_PATH}/tasks.c")
    message(FATAL_ERROR
        "FreeRTOS-Kernel submodule not found at ${FREERTOS_KERNEL_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(vscp_demo_w55rp20_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_library(ioLibrary_Driver STATIC
    "${WIZNET_LIBRARY_PATH}/Ethernet/wizchip_conf.c"
    "${WIZNET_LIBRARY_PATH}/Ethernet/socket.c"
    "${WIZNET_LIBRARY_PATH}/Ethernet/W5500/w5500.c"
    "${WIZNET_LIBRARY_PATH}/Internet/DHCP/dhcp.c"
)

target_compile_definitions(ioLibrary_Driver PUBLIC
    _WIZCHIP_=W5500
)

target_include_directories(ioLibrary_Driver PUBLIC
    "${WIZNET_LIBRARY_PATH}/Ethernet"
    "${WIZNET_LIBRARY_PATH}/Ethernet/W5500"
    "${WIZNET_LIBRARY_PATH}/Internet/DHCP"
)

add_library(freertos_kernel STATIC
    "${FREERTOS_KERNEL_PATH}/croutine.c"
    "${FREERTOS_KERNEL_PATH}/event_groups.c"
    "${FREERTOS_KERNEL_PATH}/list.c"
    "${FREERTOS_KERNEL_PATH}/queue.c"
    "${FREERTOS_KERNEL_PATH}/stream_buffer.c"
    "${FREERTOS_KERNEL_PATH}/tasks.c"
    "${FREERTOS_KERNEL_PATH}/timers.c"
    "${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM0/port.c"
    "${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM0/portasm.c"
    "${FREERTOS_KERNEL_PATH}/portable/MemMang/heap_4.c"
)

target_include_directories(freertos_kernel PUBLIC
    "${FREERTOS_KERNEL_PATH}/include"
    "${FREERTOS_KERNEL_PATH}/portable/GCC/ARM_CM0"
    "${CMAKE_CURRENT_LIST_DIR}/src"
)

target_link_libraries(freertos_kernel PUBLIC pico_stdlib)

add_executable(vscp_demo_w55rp20
    src/main.c
)

target_link_libraries(vscp_demo_w55rp20
    pico_stdlib
    hardware_spi
    hardware_gpio
    ioLibrary_Driver
    freertos_kernel
)

pico_enable_stdio_usb(vscp_demo_w55rp20 1)
pico_enable_stdio_uart(vscp_demo_w55rp20 1)
pico_add_extra_outputs(vscp_demo_w55rp20)
