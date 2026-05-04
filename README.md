# PicoDMZ – Game Boy Emulator on RP2040 / RP2350

> Fork based on peanutGB with significant modifications and hardware adaptations

<!-- vim-markdown-toc GFM -->

* [What makes this project different](#what-makes-this-project-different)
* [Demo](#demo)
* [Technical notes](#technical-notes)
* [Current status](#current-status)
* [Credits](#credits)
* [Original project](#original-project)

<!-- vim-markdown-toc -->

## What makes this project different

- RP2040 and RP2350 support
- SPI display optimizations
- Dual-core rendering experiments
- Audio implementation (I2S / PWM)
- ~59.75 FPS current performance (real-time)

## Demo

To be done

## Technical notes

Detailed documentation and devlog available in the [`docs/`](docs/) folder.

## Current status

This is a working embedded Game Boy emulator running close to full speed, with ongoing work on:

- Achieving stable 60 FPS
- Boot sequence / startup behavior

## Credits

This project is based on:

- peanutGB (original emulator)

Major changes and additions:

- Port to RP2040 / RP2350 platform
- Display driver integration (ST7789 / others)
- Audio output adaptation for embedded hardware
- Performance tuning and timing adjustments
- Hardware integration (buttons, display, audio)

## Original project

The original README has been preserved here:

[`docs/original_README.md`](docs/original_README.md)
