cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(pico_gb_mobile_adapter)
#set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()

# Extra stuff from TinyUSB, that is not part of tinyusb_device library
set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
set(LIBMOBILE_PATH libmobile)

add_executable(${PROJECT_NAME} src/pico_gb_printer.c src/usb_descriptors.c src/linkcable.c src/pico_mobile_adapter.c src/socket_impl.c src/gbridge.c src/io_buffer.c src/flash_eeprom.c)
add_subdirectory(${LIBMOBILE_PATH})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/linkcable.pio)
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/linkcable_sm.pio)

target_include_directories(${PROJECT_NAME} PRIVATE include ${PICO_TINYUSB_PATH}/src ${PICO_TINYUSB_PATH}/lib/networking ${LIBMOBILE_PATH})
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_pio tinyusb_device tinyusb_board libmobile_static hardware_flash)
pico_add_extra_outputs(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_ENTER_USB_BOOT_ON_EXIT=1)
