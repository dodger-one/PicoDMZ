add_subdirectory(ext/FatFs_SPI build)

set(DISPLAY_DRIVER "ST7789" CACHE STRING "LCD controller for RP2040_GB (ILI9225 or ST7789)")
set_property(CACHE DISPLAY_DRIVER PROPERTY STRINGS ILI9225 ST7789)
string(TOUPPER "${DISPLAY_DRIVER}" DISPLAY_DRIVER_UPPER)

option(RP2040_GB_ENABLE_LCD "Enable LCD output" ON)
option(RP2040_GB_ENABLE_SOUND "Enable I2S sound" ON)
option(RP2040_GB_ENABLE_SDCARD "Enable SD card features (ROM selector + saves)" ON)
option(RP2040_GB_ENABLE_NFC "Enable RC522 NFC boot tag support" OFF)
option(RP2040_GB_USE_DMG_BUTTON_MATRIX "Read buttons through DMG P10..P15 matrix lines" OFF)
option(RP2040_GB_DEFAULT_INTERLACE "Enable PeanutGB interlace mode by default at game boot" ON)
set(RP2040_GB_NFC_CS_GPIO "10" CACHE STRING "RC522 chip-select GPIO")
set(RP2040_GB_NFC_RST_GPIO "11" CACHE STRING "RC522 reset GPIO")
set(RP2040_GB_NFC_SPI_BAUD "4000000" CACHE STRING "RC522 SPI baud rate")
set(RP2040_GB_NFC_MAP_FILE "nfc_games.csv" CACHE STRING "SD-card NFC UID to ROM mapping filename")
set(RP2040_GB_SYS_CLOCK_VCO "1596000000" CACHE STRING "System PLL VCO frequency in Hz")
set(RP2040_GB_SYS_CLOCK_DIV1 "6" CACHE STRING "System PLL post divider 1")
set(RP2040_GB_SYS_CLOCK_DIV2 "1" CACHE STRING "System PLL post divider 2")
set(RP2040_GB_ST7789_MADCTL "0x60" CACHE STRING "ST7789 MADCTL value (example: 0xA0, 0xA8, 0x60, 0x68)")
set(RP2040_GB_ST7789_COLMOD "0x55" CACHE STRING "ST7789 COLMOD value (0x55 for RGB565, some clones may need 0x05)")
option(RP2040_GB_ST7789_INVERT "Enable ST7789 inversion at init (INVON)" OFF)
option(RP2040_GB_ST7789_SWAP_RGB565_BYTES "Swap RGB565 byte order when sending to ST7789" OFF)
set(RP2040_GB_ST7789_RGB_ORDER "RGB" CACHE STRING "Software RGB565 channel order remap: RGB, RBG, GRB, GBR, BRG, BGR")
set_property(CACHE RP2040_GB_ST7789_RGB_ORDER PROPERTY STRINGS RGB RBG GRB GBR BRG BGR)
option(RP2040_GB_ST7789_INVERT_RGB565 "Invert RGB565 channel bits in software before SPI write" OFF)
set(RP2040_GB_BUILTIN_ROM "" CACHE FILEPATH "Path to a .gb ROM file embedded into RP2040_GB UF2")

set(RP2040_GB_BUILTIN_ROM_SOURCE "")
set(RP2040_GB_BUILTIN_ROM_DEFINES "")
if(RP2040_GB_BUILTIN_ROM)
    if(NOT EXISTS "${RP2040_GB_BUILTIN_ROM}")
        message(FATAL_ERROR "RP2040_GB_BUILTIN_ROM file not found: ${RP2040_GB_BUILTIN_ROM}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RP2040_GB_BUILTIN_ROM}")
    file(READ "${RP2040_GB_BUILTIN_ROM}" ROM_HEX HEX)
    string(LENGTH "${ROM_HEX}" ROM_HEX_LEN)
    math(EXPR ROM_SIZE_BYTES "${ROM_HEX_LEN} / 2")

    string(REGEX REPLACE "([0-9A-Fa-f]{64})" "\\1\n" ROM_HEX_LINES "${ROM_HEX}")
    string(REPLACE "\n" ";" ROM_HEX_LIST "${ROM_HEX_LINES}")

    set(ROM_C_ARRAY "")
    foreach(ROM_LINE IN LISTS ROM_HEX_LIST)
        if(NOT ROM_LINE STREQUAL "")
            string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " ROM_C_LINE "${ROM_LINE}")
            string(APPEND ROM_C_ARRAY "    ${ROM_C_LINE}\n")
        endif()
    endforeach()

    set(RP2040_GB_BUILTIN_ROM_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/rp2040_gb_builtin_rom.c")
    file(WRITE "${RP2040_GB_BUILTIN_ROM_SOURCE}"
"#include <stdint.h>

const uint8_t rp2040_gb_builtin_rom[] = {
${ROM_C_ARRAY}};
const unsigned int rp2040_gb_builtin_rom_size = sizeof(rp2040_gb_builtin_rom);
")
    set(RP2040_GB_BUILTIN_ROM_DEFINES RP2040_GB_HAS_BUILTIN_ROM=1)
    message(STATUS "RP2040_GB builtin ROM: ${RP2040_GB_BUILTIN_ROM} (${ROM_SIZE_BYTES} bytes)")
endif()

if(DISPLAY_DRIVER_UPPER STREQUAL "ILI9225")
    set(RP2040_GB_DISPLAY_SOURCE firmware/mk_ili9225.c)
    set(RP2040_GB_DISPLAY_DEFINES DISPLAY_ILI9225=1)
elseif(DISPLAY_DRIVER_UPPER STREQUAL "ST7789")
    set(RP2040_GB_DISPLAY_SOURCE firmware/mk_st7789.c)
    set(RP2040_GB_DISPLAY_DEFINES DISPLAY_ST7789=1)
else()
    message(FATAL_ERROR "Unsupported DISPLAY_DRIVER='${DISPLAY_DRIVER}'. Use ILI9225 or ST7789.")
endif()

message(STATUS "RP2040_GB display driver: ${DISPLAY_DRIVER_UPPER}")

set(RP2040_GB_NFC_SOURCE "")
if(RP2040_GB_ENABLE_NFC)
    if(NOT RP2040_GB_ENABLE_SDCARD)
        message(FATAL_ERROR "RP2040_GB_ENABLE_NFC requires RP2040_GB_ENABLE_SDCARD because mappings are loaded from SD")
    endif()
    set(RP2040_GB_NFC_SOURCE firmware/mfrc522.c)
endif()

string(TOUPPER "${RP2040_GB_ST7789_RGB_ORDER}" ST7789_RGB_ORDER_UPPER)
set(ST7789_RGB_ORDER_VALUE 0)
if(ST7789_RGB_ORDER_UPPER STREQUAL "RGB")
    set(ST7789_RGB_ORDER_VALUE 0)
elseif(ST7789_RGB_ORDER_UPPER STREQUAL "RBG")
    set(ST7789_RGB_ORDER_VALUE 1)
elseif(ST7789_RGB_ORDER_UPPER STREQUAL "GRB")
    set(ST7789_RGB_ORDER_VALUE 2)
elseif(ST7789_RGB_ORDER_UPPER STREQUAL "GBR")
    set(ST7789_RGB_ORDER_VALUE 3)
elseif(ST7789_RGB_ORDER_UPPER STREQUAL "BRG")
    set(ST7789_RGB_ORDER_VALUE 4)
elseif(ST7789_RGB_ORDER_UPPER STREQUAL "BGR")
    set(ST7789_RGB_ORDER_VALUE 5)
else()
    message(FATAL_ERROR "Unsupported RP2040_GB_ST7789_RGB_ORDER='${RP2040_GB_ST7789_RGB_ORDER}'. Use RGB, RBG, GRB, GBR, BRG, or BGR.")
endif()

add_executable(RP2040_GB
        firmware/main.c
        firmware/boot_sound_data.c
        ${RP2040_GB_DISPLAY_SOURCE}
        ${RP2040_GB_NFC_SOURCE}
        ${RP2040_GB_BUILTIN_ROM_SOURCE}
        ext/minigb_apu/minigb_apu.c
        ext/i2s/i2s.c
)

add_executable(display_smoke_test
        firmware/display_smoke_test.c
)

add_executable(nfc_smoke_test
        firmware/nfc_smoke_test.c
        firmware/mfrc522.c
)

pico_generate_pio_header(RP2040_GB ${CMAKE_CURRENT_LIST_DIR}/ext/i2s/audio_i2s.pio)

target_include_directories(RP2040_GB PRIVATE inc ext/minigb_apu ext/i2s)
target_include_directories(display_smoke_test PRIVATE inc)
target_include_directories(nfc_smoke_test PRIVATE inc)

target_link_libraries(RP2040_GB
        FatFs_SPI
        pico_stdlib pico_stdio pico_bootrom pico_multicore pico_stdio  pico_multicore
        hardware_clocks hardware_pio hardware_vreg hardware_pio
        hardware_sync hardware_pll hardware_spi hardware_irq hardware_dma
        pico_binary_info)
target_link_libraries(display_smoke_test
        pico_stdlib
        hardware_clocks
        hardware_spi)
target_link_libraries(nfc_smoke_test
        pico_stdlib
        hardware_spi)
target_compile_definitions(RP2040_GB PRIVATE
        ${RP2040_GB_DISPLAY_DEFINES}
        ${RP2040_GB_BUILTIN_ROM_DEFINES}
        ST7789_MADCTL_VALUE=${RP2040_GB_ST7789_MADCTL}
        ST7789_COLMOD_VALUE=${RP2040_GB_ST7789_COLMOD}
        ST7789_INIT_INVERT=$<BOOL:${RP2040_GB_ST7789_INVERT}>
        ST7789_SWAP_RGB565_BYTES=$<BOOL:${RP2040_GB_ST7789_SWAP_RGB565_BYTES}>
        ST7789_RGB_ORDER=${ST7789_RGB_ORDER_VALUE}
        ST7789_INVERT_RGB565=$<BOOL:${RP2040_GB_ST7789_INVERT_RGB565}>
        USE_DMG_BUTTON_MATRIX=$<BOOL:${RP2040_GB_USE_DMG_BUTTON_MATRIX}>
        DEFAULT_INTERLACE=$<IF:$<BOOL:${RP2040_GB_DEFAULT_INTERLACE}>,1,0>
        ENABLE_LCD=$<BOOL:${RP2040_GB_ENABLE_LCD}>
        ENABLE_SOUND=$<BOOL:${RP2040_GB_ENABLE_SOUND}>
        ENABLE_SDCARD=$<BOOL:${RP2040_GB_ENABLE_SDCARD}>
        ENABLE_NFC=$<BOOL:${RP2040_GB_ENABLE_NFC}>
        RP2040_GB_SYS_CLOCK_VCO=${RP2040_GB_SYS_CLOCK_VCO}
        RP2040_GB_SYS_CLOCK_DIV1=${RP2040_GB_SYS_CLOCK_DIV1}
        RP2040_GB_SYS_CLOCK_DIV2=${RP2040_GB_SYS_CLOCK_DIV2}
        MFRC522_PIN_CS=${RP2040_GB_NFC_CS_GPIO}
        MFRC522_PIN_RST=${RP2040_GB_NFC_RST_GPIO}
        MFRC522_SPI_BAUD=${RP2040_GB_NFC_SPI_BAUD}
        NFC_MAP_FILE="${RP2040_GB_NFC_MAP_FILE}"
        PARAM_ASSERTIONS_DISABLE_ALL=1
        PICO_ENTER_USB_BOOT_ON_EXIT=1
        PICO_STDIO_ENABLE_CRLF_SUPPORT=1
        PICO_STDIO_DEFAULT_CRLF=1
        PICO_PRINTF_SUPPORT_FLOAT=0
        PICO_PRINTF_SUPPORT_EXPONENTIAL=0
        PICO_PRINTF_SUPPORT_LONG_LONG=1
        PICO_PRINTF_SUPPORT_PTRDIFF_T=0)

target_compile_definitions(display_smoke_test PRIVATE
        ST7789_MADCTL_VALUE=${RP2040_GB_ST7789_MADCTL}
        ST7789_COLMOD_VALUE=${RP2040_GB_ST7789_COLMOD}
        ST7789_INIT_INVERT=$<BOOL:${RP2040_GB_ST7789_INVERT}>
        ST7789_SWAP_RGB565_BYTES=$<BOOL:${RP2040_GB_ST7789_SWAP_RGB565_BYTES}>
        ST7789_RGB_ORDER=${ST7789_RGB_ORDER_VALUE}
        ST7789_INVERT_RGB565=$<BOOL:${RP2040_GB_ST7789_INVERT_RGB565}>
        PICO_STDIO_ENABLE_CRLF_SUPPORT=1
        PICO_STDIO_DEFAULT_CRLF=1)

target_compile_definitions(nfc_smoke_test PRIVATE
        MFRC522_PIN_CS=${RP2040_GB_NFC_CS_GPIO}
        MFRC522_PIN_RST=${RP2040_GB_NFC_RST_GPIO}
        MFRC522_SPI_BAUD=${RP2040_GB_NFC_SPI_BAUD}
        PICO_STDIO_ENABLE_CRLF_SUPPORT=1
        PICO_STDIO_DEFAULT_CRLF=1)

function(pico_add_verbose_dis_output TARGET)
	add_custom_command(TARGET ${TARGET} POST_BUILD
		COMMAND ${CMAKE_OBJDUMP} -h $<TARGET_FILE:${TARGET}> >$<IF:$<BOOL:$<TARGET_PROPERTY:${TARGET},OUTPUT_NAME>>,$<TARGET_PROPERTY:${TARGET},OUTPUT_NAME>,$<TARGET_PROPERTY:${TARGET},NAME>>.dis
		COMMAND ${CMAKE_OBJDUMP} -drwCSl $<TARGET_FILE:${TARGET}> >>$<IF:$<BOOL:$<TARGET_PROPERTY:${TARGET},OUTPUT_NAME>>,$<TARGET_PROPERTY:${TARGET},OUTPUT_NAME>,$<TARGET_PROPERTY:${TARGET},NAME>>.dis
		)
endfunction()

pico_set_binary_type(RP2040_GB copy_to_ram)
#pico_set_binary_type(RP2040_GB no_flash)
pico_enable_stdio_usb(RP2040_GB 1)
pico_enable_stdio_uart(RP2040_GB 0)
pico_add_verbose_dis_output(RP2040_GB)
pico_add_bin_output(RP2040_GB)
pico_add_uf2_output(RP2040_GB)

pico_enable_stdio_usb(display_smoke_test 1)
pico_enable_stdio_uart(display_smoke_test 0)
pico_add_bin_output(display_smoke_test)
pico_add_uf2_output(display_smoke_test)

pico_enable_stdio_usb(nfc_smoke_test 1)
pico_enable_stdio_uart(nfc_smoke_test 0)
pico_add_bin_output(nfc_smoke_test)
pico_add_uf2_output(nfc_smoke_test)
