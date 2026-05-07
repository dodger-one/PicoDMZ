---
layout: default
title: PicoDMZ Devlog
---

# PicoDMZ Devlog

This site is split into two parts:

- the blog below, which documents the build and implementation process over time
- a separate [technical documentation section](tech/index.md) for stable reference material

## DEMO

<iframe
  title="PicoDMZ v1.1.1 Demo"
  width="560"
  height="560"
  src="https://gnulinux.tube/videos/embed/pkWBJfUDeuTtHrFSf7csNG"
  frameborder="0"
  allowfullscreen>
</iframe>

## Technical Documentation

- [Documentation index](tech/index.md)
- [Hardware notes](tech/hardware.md)
- [Build guide](tech/build.md)
- [DMG-LCD-06 Tech Note](tech/dmg-lcd-06.md)
- [ST7789 display notes](tech/display-st7789.md)
- [NFC workflow](tech/nfc.md)

## Episodes

{% for post in site.posts %}

- [{{ post.title }}]({{ post.url | relative_url }})
  {% endfor %}
