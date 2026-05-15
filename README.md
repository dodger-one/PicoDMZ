# PicoDMZ

> A Game Boy rebuilt with modern hardware — but still powered by AA batteries.

This project is a fork of the [RP2040-GB for Pico-GB by YouMakeTech](https://github.com/YouMakeTech/Pico-GB), ultimately based on the Peanut-GB emulator.

PicoDMZ aims to recreate the original Nintendo Game Boy (DMG) experience as faithfully as possible, using modern hardware while preserving the original look and feel.

The goal is not just emulation, but reconstruction:

- Original DMG shell
- Original DMG-LCD-06 board (buttons and input)
- Real AA batteries for power

Despite running on RP2040 / RP2350 microcontrollers, the device behaves and feels like an authentic Game Boy.

As a twist, the project also introduces an NFC-based system to "load" games: original cartridges can be scanned, and the emulator loads the corresponding ROM from microSD — blending physical interaction with modern storage.

## What This Fork Focuses On

- RP2350 and RP2040 support
- SPI display integration and tuning
- Original DMG hardware adaptation
- Audio output on embedded hardware
- NFC tag to ROM mapping
- Performance work toward full-speed gameplay

## Current Status

The emulator is running on real hardware and is close to full-speed performance.

The current focus is on:

- pushing toward stable 59.75 FPS (met 99% of the time)
- refining hardware behavior
- documenting the system properly

In short: it works — now it's being pushed to its limits.

### DEMO

<div align="center">
  <img src="docs/media/demo.gif" width="400" alt="Demo">
</div>

See the v1.1.1 demo on the [devlog](https://dodger-one.github.io/PicoDMZ/) and more videos on [peertube](https://gnulinux.tube/c/picodmz/videos).

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

- Raspberry Pi Pico (RP2040) or Pico 2 (RP2350)
- 2.8" SPI TFT display (e.g. ST7789)
- microSD storage for ROMs
- MAX98357A for audio
- Original DMG-LCD-06 button board reuse
- Original Shell or clone
- "Optional" RC522 NFC reader

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

## Author

Built by [dodger](https://cv.ciberterminal.net)

Long-time systems engineer exploring what happens when you try to squeeze a 1989 console into modern microcontrollers.

Mostly interested in performance limits, weird bugs, and making things work when they probably shouldn’t.

## 🍺 Beer-powered development since 2026

If you enjoy weird embedded projects, retro hardware reconstruction, or just want to help fund more experiments, consider supporting PicoDMZ:

- [Liberapay](https://liberapay.com/dodger-one/)

Every beer helps fund the next bad hardware decision 🍺😄🍺
