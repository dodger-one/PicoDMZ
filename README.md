# PicoDMZ

This project is a fork of the [RP2040-GB for Pico-GB by YouMakeTech](https://github.com/YouMakeTech/Pico-GB), ultimately based on the Peanut-GB emulator.

PicoDMZ aims to recreate the original Nintendo Game Boy (DMG) experience as faithfully as possible, using modern hardware while preserving the original look and feel.

The goal is not just emulation, but reconstruction:

- Original DMG shell
- Original DMG-LCD-06 board (buttons and input)
- Real AA batteries for power

Despite running on RP2040 / RP2350 microcontrollers, the device behaves and feels like an authentic Game Boy.

As a twist, the project also introduces an NFC-based system to "load" games: original cartridges can be scanned, and the emulator loads the corresponding ROM from microSD — blending physical interaction with modern storage.

## What This Fork Focuses On

- RP2040 and RP2350 support
- SPI display integration and tuning
- DMG-style hardware adaptation
- Audio output on embedded hardware
- NFC tag to ROM mapping
- Performance work toward full-speed gameplay

## Current Status

The emulator is running on target hardware and is close to real-time performance. Current work is centered on polishing hardware behavior, documenting the build process, and publishing the project in a cleaner public form.

## Quick Start

If you want to explore the project, start here:

- [Project blog and devlog](https://dodger-one.github.io/PicoDMZ/)
- [Technical documentation index](docs/tech/index.md)
- [Build guide](docs/tech/build.md)
- [Hardware notes](docs/tech/hardware.md)
- [ST7789 display notes](docs/tech/display-st7789.md)
- [NFC workflow](docs/tech/nfc.md)

## Hardware At A Glance

The current build/documentation assumes combinations of:

- Raspberry Pi Pico / RP2040 or Pico 2 / RP2350
- SPI TFT display such as ST7789
- microSD storage for ROMs
- MAX98357A for audio
- Original DMG-LCD-06 button board reuse
- optional RC522 NFC reader

See the [hardware notes](docs/tech/hardware.md) for pin mapping and wiring details.

## Documentation Model

This repository now separates documentation into two tracks:

- `README.md` is the public landing page for the repository.
- `docs/` contains the project blog plus focused technical reference material.

The blog is intended for chronological build and implementation posts. The technical pages are maintained as plain Markdown reference documents.

## Credits

This project builds on prior work from:

- [Peanut-GB](https://github.com/deltabeard/Peanut-GB)
- [Pico-GB / RP2040-GB by YouMakeTech](https://github.com/YouMakeTech/Pico-GB)

Major changes in this fork include hardware adaptation, display work, audio integration, NFC support, and performance tuning for the DMG-style build.

## Original Upstream README

The preserved historical README is available at [docs/original_README.md](docs/original_README.md).
