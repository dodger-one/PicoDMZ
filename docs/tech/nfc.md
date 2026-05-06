---
layout: default
title: NFC Workflow
---

# NFC Tag Workflow

This document explains how to inspect and prepare NFC tags for the PicoDMZ emulator NFC boot feature.

## Hardware Compatibility

Your tags:

- Type: `NTAG213`
- RF/protocol: `ISO14443A`
- Frequency: `13.56 MHz`
- Family in Proxmark3: `MIFARE Ultralight / NTAG`, usually handled by `hf mfu` commands

This is compatible with Proxmark3. The RC522 reader used by the emulator can also read the ISO14443A UID from these tags.

## Emulator Mapping Behavior

The emulator uses the tag UID as the game key. It does not need to write anything to the NTAG213.

Current behavior:

- At boot, the RP2040 asks the RC522 for the tag UID.
- NTAG213 tags normally return a 7-byte UID.
- The firmware converts the UID to uppercase hex without spaces.
- The firmware opens `nfc_games.csv` from the SD card root.
- If the UID is listed, the mapped ROM is loaded automatically.
- If no tag or no mapping is found, the normal ROM selector is shown.

The emulator still does not read NDEF records or NTAG213 user memory. That is intentional for now: UID mapping is simpler, avoids authentication/NDEF parsing, and lets you change mappings by editing a file on the SD card instead of rewriting tags.

Example `nfc_games.csv` on the SD card root:

```text
# UID;ROM filename
04AABBCCDD1122;tetris.gb
04FFEEDDCC9988;zelda.gb
```

Accepted UID formatting in the file:

```text
04AABBCCDD1122;tetris.gb
04:AA:BB:CC:DD:11:22;tetris.gb
04-AA-BB-CC-DD-11-22;tetris.gb
04 AA BB CC DD 11 22;tetris.gb
```

Use semicolon as the normal separator. Comma is also accepted by the firmware, but semicolon is recommended because ROM filenames are human data and semicolon is less ambiguous.

## Install / Run Proxmark3 Client

### Option A: If your Linux distro has a package

Some distros provide a package:

```bash
sudo apt update
sudo apt install proxmark3
```

Then connect the Proxmark3 and check the device:

```bash
ls /dev/ttyACM*
proxmark3 /dev/ttyACM0
```

If your package uses the RRG/Iceman client wrapper, this may also work:

```bash
pm3
pm3 --list
```

### Option B: Build RRG/Iceman Proxmark3 from source

Use this if the distro package is missing, old, or does not support your Proxmark3 hardware.

```bash
sudo apt update
sudo apt install -y \
  git build-essential pkg-config libreadline-dev libusb-0.1-4-dev \
  libusb-1.0-0-dev qtbase5-dev libbz2-dev liblz4-dev \
  libssl-dev python3 python3-pip

git clone https://github.com/RfidResearchGroup/proxmark3.git
cd proxmark3
make clean
make -j"$(nproc)"
```

For a generic Proxmark3 Easy, you may need a platform-specific build flag. Check the official RRG build instructions for your exact hardware before flashing firmware.

Do not blindly flash firmware unless you know your Proxmark3 variant. Client and firmware versions should generally match.

## Connect To The Proxmark3

List available serial devices:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Start the client:

```bash
proxmark3 /dev/ttyACM0
```

Or, with RRG/Iceman builds:

```bash
./pm3 /dev/ttyACM0
```

Inside the client, verify hardware:

```text
hw version
hw status
```

Check HF antenna behavior:

```text
hf tune
```

A visible change when placing/removing the tag near the antenna is a good sign.

## Read Basic Tag Information

Place one NTAG213 on the Proxmark3 antenna.

Search for any HF tag:

```text
hf search
```

Read ISO14443A information:

```text
hf 14a info
```

Read MIFARE Ultralight / NTAG information:

```text
hf mfu info
```

Expected useful fields:

- UID
- ATQA / SAK
- tag family, ideally NTAG213 or NTAG21x
- memory size / pages
- lock bits / configuration pages

For emulator testing, the UID is the most important value.

Example:

```text
UID: 04 A1 B2 C3 D4 E5 80
```

Emulator mapping key for this tag:

```text
04A1B2C3D4E580
```

Add that UID to `nfc_games.csv` if you want this tag to auto-start a ROM.

## Dump The Tag

Dump the complete NTAG memory:

```text
hf mfu dump
```

If your client requires explicit options, ask the client for exact syntax:

```text
hf mfu dump -h
```

Read individual 4-byte pages:

```text
hf mfu rdbl -b 4
hf mfu rdbl -b 5
hf mfu rdbl -b 6
```

Some newer clients prefer long options:

```text
hf mfu rdbl --blk 4
```

Use `hf mfu rdbl -h` to confirm the syntax for your installed client.

## Write Data To The Tag

NTAG213 memory is organized in 4-byte pages. User memory starts at page `4`.

Read before writing:

```text
hf mfu rdbl -b 4
```

Write 4 bytes to page `4`:

```text
hf mfu wrbl -b 4 -d 01020304
```

Newer clients may use:

```text
hf mfu wrbl --blk 4 --data 01020304
```

Verify after writing:

```text
hf mfu rdbl -b 4
```

Important:

- Do not write lock/configuration pages unless you know exactly what they do.
- For NTAG213, user pages are normally safe; lock bytes and configuration pages can permanently lock or password-protect the tag.
- Writing user memory will not change the UID on normal NTAG213 tags.
- The current emulator does not use this written data yet; it only reads UID.

## Optional: Read NDEF Data

If the tag contains NFC Forum NDEF data, try:

```text
hf mfu ndefread
```

For verbose output, depending on client version:

```text
hf mfu ndefread -v
```

The current emulator ignores NDEF records. This is useful only for inspecting tags or for a future firmware feature.

## Use A Tag With The Current Emulator

### 1. Read The Tag UID With Proxmark3

```text
hf 14a info
hf mfu info
```

Write down the UID.

Example UID from Proxmark3:

```text
04 A1 B2 C3 D4 E5 80
```

The emulator mapping key is the same UID without spaces, usually uppercase:

```text
04A1B2C3D4E580
```

### 2. Create `nfc_games.csv` On The SD Card

Create this file in the root of the same SD card that contains the ROMs:

```text
# UID;ROM filename
04A1B2C3D4E580;tetris.gb
```

For your original requested test list, replace the UIDs with your real tag UIDs:

```text
# UID;ROM filename
YOUR_TETRIS_TAG_UID;tetris.gb
YOUR_ZELDA_TAG_UID;zelda.gb
```

Example with two real-looking NTAG UIDs:

```text
04A1B2C3D4E580;tetris.gb
04FFEEDDCC9988;zelda.gb
```

The ROM filenames must exist in the SD card root.

### 3. Build With NFC Enabled

Example build flags:

```bash
cmake -S . -B build -G Ninja \
  -DDISPLAY_DRIVER=ST7789 \
  -DRP2040_GB_ENABLE_SDCARD=ON \
  -DRP2040_GB_ENABLE_SOUND=ON \
  -DRP2040_GB_ENABLE_NFC=ON \
  -DRP2040_GB_USE_DMG_BUTTON_MATRIX=ON \
  -DRP2040_GB_DEFAULT_INTERLACE=ON

cmake --build build -j --target RP2040_GB
```

Optional: change the mapping filename at build time:

```bash
cmake -S . -B build -G Ninja \
  -DRP2040_GB_ENABLE_NFC=ON \
  -DRP2040_GB_NFC_MAP_FILE=my_tags.csv
```

Or use the project build helper if it already enables NFC:

```bash
./build_test.sh
```

### 4. Flash And Test

Copy `build/RP2040_GB.uf2` to the RP2040.

Open USB CDC console:

```bash
minicom -D /dev/ttyACM0 -b 115200
```

Boot with the tag near the RC522.

Expected successful flow:

```text
NFC: init RC522 on spi1 CS=GP10 RST=GP11
NFC: RC522 VersionReg=0x92
NFC: waiting for boot tag
NFC: UID=04A1B2C3D4E580
NFC: looking up UID 04A1B2C3D4E580 in nfc_games.csv
NFC: matched UID 04A1B2C3D4E580 -> tetris.gb
NFC: auto-starting tetris.gb
```

Expected fallback flow:

```text
NFC: no tag found, showing ROM selector
```

If the mapping file is missing:

```text
NFC: mapping file nfc_games.csv not available: ...
```

If the tag UID is not listed:

```text
NFC: no ROM mapping for UID 04A1B2C3D4E580
```

If the RC522 is not detected:

```text
NFC: RC522 VersionReg=0x00
NFC: RC522 not detected, skipping NFC boot
```

or:

```text
NFC: RC522 VersionReg=0xFF
NFC: RC522 not detected, skipping NFC boot
```

## RC522 Wiring For Emulator

The RC522 shares the SD-card SPI bus.

| RC522 signal        | RP2040 GPIO | Notes                   |
| ------------------- | ----------: | ----------------------- |
| `3.3V`              |       `3V3` | Do not use 5 V.         |
| `GND`               |       `GND` | Common ground.          |
| `MISO`              |      `GP12` | Shared with SD `MISO`.  |
| `MOSI`              |      `GP15` | Shared with SD `MOSI`.  |
| `SCK`               |      `GP14` | Shared with SD `SCK`.   |
| `SDA` / `SS` / `CS` |      `GP10` | Dedicated RC522 CS.     |
| `RST`               |      `GP11` | Dedicated RC522 reset.  |
| `IRQ`               | unconnected | Current firmware polls. |

SPI sharing rule:

- SD uses `CS=GP13`.
- RC522 uses `CS=GP10`.
- `MISO`, `MOSI`, and `SCK` are shared.
- Only one chip select should be active at a time.

## Troubleshooting

### Proxmark3 Does Not See The Tag

Try:

```text
hw status
hf tune
hf search
hf 14a info
```

Checks:

- Tag is centered on the HF antenna.
- Only one tag is on the antenna.
- Client and firmware versions match.
- You are using HF commands, not LF commands.

### Emulator Says RC522 Version Is 0x00 Or 0xFF

Likely wiring or power issue.

Check:

- RC522 powered from `3.3V`, not `5V`.
- Common ground with RP2040.
- `MISO=GP12`, `MOSI=GP15`, `SCK=GP14`.
- `CS=GP10`, `RST=GP11`.
- SD and RC522 are not both driving MISO due to incorrect CS wiring.

### Tag Is Detected But Game Does Not Start

Check CDC output:

```text
NFC: UID=...
NFC: no ROM mapping for UID ...
```

Then add that UID to `nfc_games.csv` on the SD card.

Also check:

- ROM filename exists on SD card root.
- Filename case matches exactly enough for the filesystem/config used.
- SD card initializes successfully.
- `nfc_games.csv` is in the SD card root.

### Writing Data Did Not Change The Game

Expected with current firmware. The emulator currently reads UID only, not NTAG user memory or NDEF records. Edit `nfc_games.csv` to change which ROM a tag launches.

## References

- Proxmark3 command list: https://github.com/Proxmark/proxmark3/wiki/commands
- RRG/Iceman Proxmark3 repository: https://github.com/RfidResearchGroup/proxmark3
- Proxmark3 tag support notes, including NTAG213/215/216 under `hf mfu`: https://github.com/Proxmark/proxmark3/wiki/Information-on-the-main-RFID-tags-supported-by-ProxmarkIII
- Kali proxmark3 package notes: https://www.kali.org/tools/proxmark3/
