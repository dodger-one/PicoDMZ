---
layout: default
title: Build Guide
---

# Build Guide

## Flashing The Firmware

- Download the desired UF2 build artifact.
- Hold `BOOTSEL` while connecting the Pico to USB.
- Release `BOOTSEL` when `RPI-RP2` appears.
- Copy the UF2 to the mounted drive.

## Preparing The SD Card

The emulator uses a FAT32 microSD card for ROMs and save data.

- Format the card as `FAT32`.
- Copy legally owned `.gb` ROMs to the SD card root.
- Insert the card into the wired SD reader.

Subfolders are not currently the default workflow for ROM discovery.

## Building From Source

The [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) is required. Make sure a Pico SDK example project builds correctly before working on this repository.

Select the display driver with `DISPLAY_DRIVER`:

- `-DDISPLAY_DRIVER=ST7789`
- `-DDISPLAY_DRIVER=ILI9225`

Example build:

```bash
cmake -S . -B build -G Ninja -DDISPLAY_DRIVER=ST7789
cmake --build build -j --target RP2040_GB
```

RP2350 / Pico 2 example:

```bash
cmake -S . -B build_pico2 -G Ninja -DPICO_BOARD=pico2 -DDISPLAY_DRIVER=ST7789
cmake --build build_pico2 -j --target RP2040_GB
```

## Feature Toggles

- `-DRP2040_GB_ENABLE_SDCARD=OFF` to run without SD card wiring
- `-DRP2040_GB_ENABLE_SOUND=OFF` to disable I2S audio
- `-DRP2040_GB_ENABLE_NFC=ON` to enable RC522 NFC boot-tag support
- `-DRP2040_GB_ENABLE_LCD=ON|OFF`
- `-DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON`
- `-DRP2040_GB_DEFAULT_INTERLACE=ON|OFF`
- `-DRP2040_GB_BUILTIN_ROM=/absolute/path/to/game.gb`

ST7789-specific flags are documented in [display-st7789.md](display-st7789.md).

## Embedded ROM Mode

Embedded ROM mode compiles a `.gb` file into the UF2 so the emulator can run without SD card wiring.

- Useful while SD hardware is not soldered yet
- Best paired with `-DRP2040_GB_ENABLE_SDCARD=OFF`

Example:

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ENABLE_SDCARD=OFF \
  -DRP2040_GB_BUILTIN_ROM=/home/user/roms/tetris.gb
```

## NFC Builds

To build with RC522 support enabled:

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ENABLE_SDCARD=ON \
  -DRP2040_GB_ENABLE_SOUND=ON \
  -DRP2040_GB_ENABLE_NFC=ON \
  -DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON \
  -DRP2040_GB_DEFAULT_INTERLACE=ON

cmake --build build -j --target RP2040_GB
```

See [nfc.md](nfc.md) for tag workflow, mapping behavior, and RC522 wiring.
