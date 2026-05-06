---
layout: default
title: Hardware Notes
---

# Hardware Notes

This page collects the hardware assumptions and wiring notes for the current PicoDMZ build.

## Main Components

- RP2040 or RP2350-based Pico board
- SPI TFT display
- microSD reader
- optional MAX98357A audio output
- optional RC522 NFC reader
- optional original DMG-LCD-06 board for buttons

## Display Compatibility

The firmware currently supports these SPI LCD controllers selected at build time:

- `ILI9225`
- `ST7789`

Display type is not auto-detected. If your module uses another controller such as `SSD1306`, `SH1106`, or `ST7735`, it needs a dedicated driver path.

## Pinout: Standard Buttons + MAX98357A

- UP = `GP2`
- DOWN = `GP3`
- LEFT = `GP4`
- RIGHT = `GP5`
- BUTTON A = `GP6`
- BUTTON B = `GP7`
- SELECT = `GP8`
- START = `GP9`
- SD MISO = `GP12`
- SD CS = `GP13`
- SD SCK = `GP14`
- SD MOSI = `GP15`
- LCD CS = `GP17`
- LCD CLK = `GP18`
- LCD SDI = `GP19`
- LCD RS = `GP20`
- LCD RST = `GP21`
- LCD LED = `GP22`
- MAX98357A DIN = `GP26`
- MAX98357A BCLK = `GP27`
- MAX98357A LRC = `GP28`

## Computed Pinout

| GPIO   | Used by    | Signal              |
| ------ | ---------- | ------------------- |
| `GP2`  | DMG matrix | `P11`               |
| `GP3`  | DMG matrix | `P14`               |
| `GP4`  | DMG matrix | `P13`               |
| `GP5`  | DMG matrix | `P12`               |
| `GP6`  | DMG matrix | `P10`               |
| `GP7`  | DMG matrix | `P15`               |
| `GP10` | RC522      | `CS` / `SDA` / `SS` |
| `GP11` | RC522      | `RST`               |
| `GP12` | SD + RC522 | `MISO`              |
| `GP13` | SD         | `CS`                |
| `GP14` | SD + RC522 | `SCK`               |
| `GP15` | SD + RC522 | `MOSI`              |
| `GP16` | RC522      | optional `IRQ`      |
| `GP17` | ST7789     | `CS`                |
| `GP18` | ST7789     | `SCK`               |
| `GP19` | ST7789     | `MOSI`              |
| `GP20` | ST7789     | `DC` / `RS`         |
| `GP21` | ST7789     | `RST`               |
| `GP22` | ST7789     | `BL` / `LED`        |
| `GP26` | MAX98357A  | `DIN`               |
| `GP27` | MAX98357A  | `BCLK`              |
| `GP28` | MAX98357A  | `LRC`               |

## DMG-LCD-06 Button Matrix Wiring

If you are reusing the original DMG-LCD-06 board only for buttons, this branch uses a sequential GPIO mapping by ribbon pin number:

- `ribbon pin 4` (`P11`) -> `GP2`
- `ribbon pin 5` (`P14`) -> `GP3`
- `ribbon pin 6` (`P13`) -> `GP4`
- `ribbon pin 7` (`P12`) -> `GP5`
- `ribbon pin 8` (`P10`) -> `GP6`
- `ribbon pin 9` (`P15`) -> `GP7`

Enable this mode at build time with:

- `-DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON`

The firmware scans the DMG matrix with active-low logic, using `P14/P15` as select lines and `P10..P13` as read lines.
