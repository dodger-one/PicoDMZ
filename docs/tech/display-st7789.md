---
layout: default
title: ST7789 Display Notes
---

# ST7789 Display Notes

This page covers the display smoke-test target and the configuration flags used when a panel needs orientation or color tuning.

## Display Smoke Test Firmware

A standalone test target is available to validate display wiring without SD, audio, or emulator logic:

- target name: `display_smoke_test`
- output UF2: `display_smoke_test.uf2`
- expected controller: `ST7789`
- pin usage: `CS=GP17`, `SCK=GP18`, `MOSI=GP19`, `DC=GP20`, `RST=GP21`, `BLK=GP22`

The smoke test runs an interactive color-combo cycler and prints serial logs.

It can:

- iterate through `MADCTL`, `COLMOD`, invert, byte-swap, RGB order, and RGB565 invert combinations
- draw DMG-style pseudo-green grayscale swatches plus RGB reference bars
- show the current combination index on screen

Serial keys:

- `c` next combo
- `p` previous combo
- `r` toggle auto-cycle
- `h` or `?` print help

For serial debug on Linux:

```bash
minicom -D /dev/ttyACM0 -b 115200
```

## Runtime Debug Notes

Useful serial controls in the emulator:

- `b` prints frames, elapsed time, and FPS
- `n` or `]` selects next manual palette
- `p` or `[` selects previous manual palette
- `g` forces DMG green palette
- `a` restores automatic palette selection
- `h` or `?` prints help

Automatic FPS logging includes `frameskip` and `interlace` state.

## ST7789 Mode Notes

When using `DISPLAY_DRIVER=ST7789`, the panel is driven in `320x240` logical orientation. The Game Boy frame (`160x144`) is scaled to `266x240`.

If colors or orientation look wrong on a given module, tune these build flags:

- `-DRP2040_GB_ST7789_MADCTL=0x60`
- `-DRP2040_GB_ST7789_MADCTL=0x68`
- `-DRP2040_GB_ST7789_MADCTL=0xA0`
- `-DRP2040_GB_ST7789_COLMOD=0x55`
- `-DRP2040_GB_ST7789_COLMOD=0x05`
- `-DRP2040_GB_ST7789_SWAP_RGB565_BYTES=ON|OFF`
- `-DRP2040_GB_ST7789_INVERT=ON|OFF`
- `-DRP2040_GB_ST7789_RGB_ORDER=RGB|RBG|GRB|GBR|BRG|BGR`
- `-DRP2040_GB_ST7789_INVERT_RGB565=ON|OFF`

## Palette / Tuning Recipes

### Default RGB

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ST7789_COLMOD=0x55 \
  -DRP2040_GB_ST7789_MADCTL=0x60 \
  -DRP2040_GB_ST7789_INVERT=OFF \
  -DRP2040_GB_ST7789_RGB_ORDER=RGB \
  -DRP2040_GB_ST7789_INVERT_RGB565=OFF \
  -DRP2040_GB_ST7789_SWAP_RGB565_BYTES=OFF
```

### BGR Mode

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ST7789_COLMOD=0x55 \
  -DRP2040_GB_ST7789_MADCTL=0x68 \
  -DRP2040_GB_ST7789_INVERT=OFF \
  -DRP2040_GB_ST7789_RGB_ORDER=RGB \
  -DRP2040_GB_ST7789_INVERT_RGB565=OFF \
  -DRP2040_GB_ST7789_SWAP_RGB565_BYTES=OFF
```

### Byte Swap On

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ST7789_COLMOD=0x55 \
  -DRP2040_GB_ST7789_MADCTL=0xA0 \
  -DRP2040_GB_ST7789_INVERT=OFF \
  -DRP2040_GB_ST7789_RGB_ORDER=RGB \
  -DRP2040_GB_ST7789_INVERT_RGB565=OFF \
  -DRP2040_GB_ST7789_SWAP_RGB565_BYTES=ON
```

### Clone-Friendly Fallback

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ST7789_COLMOD=0x05 \
  -DRP2040_GB_ST7789_MADCTL=0xA0 \
  -DRP2040_GB_ST7789_INVERT=OFF \
  -DRP2040_GB_ST7789_RGB_ORDER=BGR \
  -DRP2040_GB_ST7789_INVERT_RGB565=OFF \
  -DRP2040_GB_ST7789_SWAP_RGB565_BYTES=OFF
```

## Common Failure Signal

If serial output shows `No disk, or could not put SD card in to SPI idle state` or `f_mount error (3)`, the SD card path is not initializing correctly. Either fix the SD wiring or build with `-DRP2040_GB_ENABLE_SDCARD=OFF` while testing display-only configurations.
