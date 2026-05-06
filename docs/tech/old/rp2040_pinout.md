# RP2040 Pinout

This pinout is for the RP2040/Pico build using:

- ST7789 SPI TFT
- MAX98357A I2S amplifier
- DMG-LCD-06 button matrix
- MicroSD card reader
- Future RC522 NFC reader

## Power

| Device    | Pin    | RP2040/Pico connection         | Notes                                                                                            |
| --------- | ------ | ------------------------------ | ------------------------------------------------------------------------------------------------ |
| TFT       | `VCC`  | `3V3` or module-required `VCC` | Use `3V3` if the TFT module supports 3.3 V logic/power.                                          |
| TFT       | `GND`  | `GND`                          | Common ground required.                                                                          |
| MAX98357A | `VIN`  | `3V3` or `VBUS`                | Depends on your amplifier board/speaker volume needs. Logic is 3.3 V.                            |
| MAX98357A | `GND`  | `GND`                          | Common ground required.                                                                          |
| MicroSD   | `VCC`  | `3V3`                          | Do not feed SD card modules with 5 V unless the module explicitly has level shifting/regulation. |
| MicroSD   | `GND`  | `GND`                          | Common ground required.                                                                          |
| RC522     | `3.3V` | `3V3`                          | RC522 is 3.3 V only. Do not use 5 V.                                                             |
| RC522     | `GND`  | `GND`                          | Common ground required.                                                                          |

## ST7789 TFT

The firmware uses `spi0` for the TFT data path.

| TFT signal | Also labeled as     |   RP2040 GPIO | Notes                                 |
| ---------- | ------------------- | ------------: | ------------------------------------- |
| `CS`       | `TFT_CS`            |        `GP17` | Chip select.                          |
| `SCK`      | `CLK`, `SCL`        |        `GP18` | `spi0` clock.                         |
| `MOSI`     | `SDI`, `SDA`, `DIN` |        `GP19` | `spi0` TX to display.                 |
| `DC`       | `RS`, `A0`          |        `GP20` | Data/command select.                  |
| `RST`      | `RESET`             |        `GP21` | Display reset.                        |
| `BL`       | `LED`, `BLK`        |        `GP22` | Backlight control.                    |
| `MISO`     | `SDO`, `DO`         | not connected | Not used by current display firmware. |

## DMG-LCD-06 Button Matrix

Enable this mode at build time:

```bash
-DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON
```

Physical wiring is kept aligned by DMG ribbon order.

| DMG ribbon pin | DMG signal | RP2040 GPIO | Direction in firmware |
| -------------: | ---------- | ----------: | --------------------- |
|              4 | `P11`      |       `GP2` | Input with pull-up.   |
|              5 | `P14`      |       `GP3` | Matrix select output. |
|              6 | `P13`      |       `GP4` | Input with pull-up.   |
|              7 | `P12`      |       `GP5` | Input with pull-up.   |
|              8 | `P10`      |       `GP6` | Input with pull-up.   |
|              9 | `P15`      |       `GP7` | Matrix select output. |

Matrix scan behavior:

| Select line | Active row | Button mapping                                |
| ----------- | ---------- | --------------------------------------------- |
| `P14` low   | D-pad row  | `P10=Right`, `P11=Left`, `P12=Up`, `P13=Down` |
| `P15` low   | Button row | `P10=A`, `P11=B`, `P12=Select`, `P13=Start`   |

All matrix signals are active-low.

## MicroSD Reader

The firmware uses `spi1` for the SD card.

| MicroSD signal | RP2040 GPIO | Notes           |
| -------------- | ----------: | --------------- |
| `MISO`         |      `GP12` | `spi1` RX.      |
| `CS`           |      `GP13` | SD chip select. |
| `SCK`          |      `GP14` | `spi1` clock.   |
| `MOSI`         |      `GP15` | `spi1` TX.      |

Some SD modules label the pins as:

| Module label | Meaning |
| ------------ | ------- |
| `DO`         | `MISO`  |
| `DI`         | `MOSI`  |
| `CLK`        | `SCK`   |
| `CS`         | `CS`    |

## RC522 NFC Reader

The RC522 can share the same `spi1` bus with the SD card. It needs its own chip-select and reset pins.

Current emulator firmware can use the RC522 at boot when built with `-DRP2040_GB_ENABLE_NFC=ON`. It scans for a tag before the ROM selector and can auto-load a mapped ROM from the SD card.

| RC522 signal        | RP2040 GPIO | Notes                                   |
| ------------------- | ----------: | --------------------------------------- |
| `MISO`              |      `GP12` | Shared with SD `MISO`.                  |
| `MOSI`              |      `GP15` | Shared with SD `MOSI`.                  |
| `SCK`               |      `GP14` | Shared with SD `SCK`.                   |
| `SDA` / `SS` / `CS` |      `GP10` | Dedicated RC522 chip select.            |
| `RST`               |      `GP11` | Dedicated RC522 reset.                  |
| `IRQ`               |      `GP16` | Optional. Leave unconnected if polling. |

SPI sharing rule:

- `GP12`, `GP14`, and `GP15` are shared by SD and RC522.
- SD keeps `CS=GP13`.
- RC522 gets `CS=GP10`.
- Only one device CS should be active at a time.

## MAX98357A Amplifier

The firmware uses PIO I2S output.

| MAX98357A signal       | RP2040 GPIO | Notes                               |
| ---------------------- | ----------: | ----------------------------------- |
| `DIN`                  |      `GP26` | I2S data.                           |
| `BCLK`                 |      `GP27` | I2S bit clock.                      |
| `LRC` / `LRCLK` / `WS` |      `GP28` | I2S word select / left-right clock. |

Common optional MAX98357A pins:

| MAX98357A signal  | Suggested connection                  | Notes                                                  |
| ----------------- | ------------------------------------- | ------------------------------------------------------ |
| `GAIN`            | leave default or wire per module docs | Optional gain setting.                                 |
| `SD`              | leave default/enabled or pull high    | Shutdown/mute pin, module-dependent.                   |
| Speaker `+` / `-` | speaker terminals                     | Connect speaker to MAX output, not directly to RP2040. |

## GPIO Summary

|   GPIO | Used by    | Signal              |
| -----: | ---------- | ------------------- |
|  `GP2` | DMG matrix | `P11`               |
|  `GP3` | DMG matrix | `P14`               |
|  `GP4` | DMG matrix | `P13`               |
|  `GP5` | DMG matrix | `P12`               |
|  `GP6` | DMG matrix | `P10`               |
|  `GP7` | DMG matrix | `P15`               |
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

## Build Flags

Typical build for this wiring:

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON \
  -DRP2040_GB_ENABLE_SDCARD=ON \
  -DRP2040_GB_ENABLE_SOUND=ON
cmake --build build -j --target RP2040_GB
```

Known-good ST7789 palette/display combo from previous hardware testing:

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON \
  -DRP2040_GB_ENABLE_SDCARD=ON \
  -DRP2040_GB_ENABLE_SOUND=ON \
  -DRP2040_GB_ST7789_MADCTL=0xA0 \
  -DRP2040_GB_ST7789_COLMOD=0x05 \
  -DRP2040_GB_ST7789_INVERT=ON \
  -DRP2040_GB_ST7789_SWAP_RGB565_BYTES=OFF \
  -DRP2040_GB_ST7789_RGB_ORDER=RGB \
  -DRP2040_GB_ST7789_INVERT_RGB565=ON
cmake --build build -j --target RP2040_GB
```
