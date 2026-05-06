---
layout: default
title: PicoDMZ Devlog
---

# PicoDMZ Devlog

This site is split into two parts:

- the blog below, which documents the build and implementation process over time
- a separate [technical documentation section](tech/index.md) for stable reference material

## Technical Documentation

- [Documentation index](tech/index.md)
- [Hardware notes](tech/hardware.md)
- [Build guide](tech/build.md)
- [ST7789 display notes](tech/display-st7789.md)
- [NFC workflow](tech/nfc.md)

## DEMO

<p align="center">
  <video src="{{ site.baseurl }}/media/demo.mp4" width="400" autoplay loop muted playsinline></video>
</p>

## Episodes

{% for post in site.posts %}

- [{{ post.title }}]({{ post.url | relative_url }})
  {% endfor %}
