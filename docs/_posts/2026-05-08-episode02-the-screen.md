---
layout: post
title: "Episode 02 - The Screen"
author: dodger-one
tags:
  - rp2040
  - dmg
  - hardware
---

## Screen Fighter I

The fight was epic: Ryu vs Ken.

I started by trimming the new 2.8-inch TFT screen so it could fit inside the original DMG shell.

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260301093844.webp" width="400" alt="Episode 02">
</div>

I only had to cut the corners. Everything else fit surprisingly well and stayed firmly in place inside the shell.

Then I replaced the ribbon cable from the old 1.8-inch display with the new screen:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260301094534.webp" width="400" alt="Episode 02">
</div>

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260301094632.webp" width="400" alt="Episode 02">
</div>

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260301094803.webp" width="400" alt="Episode 02">
</div>

I placed the screen underneath the DMG-LCD-06 board, exactly as planned for the final build — although still using Dupont wires at this stage.

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260301095428.webp" width="400" alt="Episode 02">
</div>

Next step was checking if the configured resolution properly matched the original Game Boy screen frame:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260316212218.webp" width="400" alt="Episode 02">
</div>

Even if it isn't pixel-perfect due to the TFT resolution, my goal was to fully fill the original screen window — just like the real DMG.

And this is where the brutal fight with the color palette began :rofl:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260316212445.webp" width="400" alt="Episode 02">
</div>

I had to build a custom [firmware](tech/display-st7789.md) for the RP2040 in order to select the EXACT color palette I had in mind: the classic DMG green palette that we all love :inlove:

After many tests, I found that palette `074` was the closest match.

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260318121220.webp" width="400" alt="Episode 02">
</div>

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG_20260318_121247.webp" width="400" alt="Episode 02">
</div>

So we went from this:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG_20260318_121359.webp" width="400" alt="Episode 02">
</div>

To the palette that became the default for the rest of the project development:

<div align="center">
  <img src="{{ site.baseurl }}/media/episode_02/IMG20260318122729.webp" width="400" alt="Episode 02">
</div>

## Related Technical Documentation

- [Display Smoke Test Tech Note]({{ site.baseurl }}/tech/display-st7789.md)
