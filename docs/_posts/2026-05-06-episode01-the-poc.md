---
layout: post
title: "Episode 01 - The POC"
author: dodger-one
tags:
  - rp2040
  - dmg
  - hardware
  - poc
---

## It Started As a POC

Everything started as a simple proof of concept.

I've always been a fan of Arduino, ESP32 and the Raspberry Pi Pico ecosystem. Coming from the FOSS world, these boards felt revolutionary compared to the traditionally closed ecosystem of embedded and automation hardware.

After discovering several Game Boy emulator projects for ESP32 and RP2040/RP2350 boards, I couldn't resist trying one myself using a spare RP2040 board and a small 1.8" TFT display I already had available:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/IMG20260218114039.webp" width="400" alt="1.8 TFT">
</div>

<iframe
  title="PicoDMZ v1.1.1 Demo"
  width="560"
  height="560"
  src="https://gnulinux.tube/videos/embed/vKwMHL9pivydjr7kf215TA"
  frameborder="0"
  allowfullscreen>
</iframe>

Then I realized I had an original DMG-LCD-06 board available from a previous Game Boy restoration project using an IPS display replacement.

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/IMG_20260219_193029.webp" width="400" alt="DMG-LCD-06">
</div>

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/IMG_20260219_193048.webp" width="400" alt="DMG-LCD-06">
</div>

So I started experimenting to see whether I could reuse the DMG-LCD-06 board together with the RP2040 and keep the original PCB and buttons.

I removed the original screen (it was already broken):

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/IMG20260220092247.webp" width="400" alt="DMG-LCD-06 without screen">
</div>

Then I temporarily soldered the required wires to connect it to the RP2040:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/IMG20260220095702.webp" width="400" alt="DMG-LCD-06 wiring">
</div>

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_01/photo_2026-02-20_18-24-06.webp" width="400" alt="DMG-LCD-06 wired">
</div>

And... it worked!!!!

At that point, the goal stopped being a simple POC and became:

> "Let's build a Game Boy."

I realized I could probably fit everything required inside an original Game Boy shell while preserving the original button board and controls, keeping the look and feel as close as possible to the real hardware.

## Related Technical Documentation

- [DMG-LCD-06 Tech Note](tech/dmg-lcd-06.md)
