cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(pico_gb_mobile_adapter)
#set(LIBMOBILE_ENABLE_NO32BIT ON)
#set(PICO_CXX_ENABLE_EXCEPTIONS 1)
#set(PICO_COPY_TO_RAM 1)
#set(LIBMOBILE_TIME_SENSITIVE "__attribute__((section(\".time_critical.\" #x))) x")
pico_sdk_init()

# Extra stuff from TinyUSB, that is not part of tinyusb_device library
set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
set(LIBMOBILE_PATH libmobile)

add_executable(${PROJECT_NAME} src/specific/main.c src/specific/usb_descriptors.c src/specific/linkcable.c src/specific/pico_mobile_adapter.c src/generic/socket_impl.c src/generic/gbridge.c src/generic/io_buffer.c src/specific/save_load_config.c src/generic/bridge_debug_commands.c src/generic/utils.c src/generic/sync.c src/generic/gbridge_timeout.c src/generic/upkeep_callback.c)
add_subdirectory(${LIBMOBILE_PATH})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/specific/linkcable.pio)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/specific/linkcable_sm.pio)

target_include_directories(${PROJECT_NAME} PRIVATE include/generic include/specific ${PICO_TINYUSB_PATH}/src ${PICO_TINYUSB_PATH}/lib/networking ${LIBMOBILE_PATH})
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_pio tinyusb_device tinyusb_board libmobile_static hardware_flash pico_multicore)
pico_add_extra_outputs(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)
