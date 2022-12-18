cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(usbkb C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(usbkb)
target_sources(usbkb PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/main.c
	${CMAKE_CURRENT_SOURCE_DIR}/usbkeyboard.c
	${CMAKE_CURRENT_SOURCE_DIR}/hid_virtual_keytable.c
	${CMAKE_CURRENT_SOURCE_DIR}/msc_app.c
	${CMAKE_CURRENT_SOURCE_DIR}/../lcd-lib/ili9341_spi.c
	${CMAKE_CURRENT_SOURCE_DIR}/../lcd-lib/graphlib.c
	${CMAKE_CURRENT_SOURCE_DIR}/../lcd-lib/fontdata.c
)
#target_include_directories(usbkb PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(usbkb PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
)

# Pull in basic dependencies
target_link_libraries(usbkb pico_stdlib tinyusb_host tinyusb_board hardware_spi pico_multicore)

# create map/bin/hex file etc.
pico_add_extra_outputs(usbkb)
