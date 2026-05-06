/**
 * Copyright (C) 2022 by Mahyar Koshkouei <mk@deltabeard.com>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

// Peanut-GB emulator settings
#ifndef ENABLE_LCD
#define ENABLE_LCD	1
#endif
#ifndef ENABLE_SOUND
#define ENABLE_SOUND	1
#endif
#ifndef ENABLE_SDCARD
#define ENABLE_SDCARD	1
#endif
#ifndef ENABLE_NFC
#define ENABLE_NFC	0
#endif
#ifndef USE_DMG_BUTTON_MATRIX
#define USE_DMG_BUTTON_MATRIX 0
#endif
#define AUDIO_DEFAULT_VOLUME 3
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#define PEANUT_GB_USE_BIOS 0

/* Use DMA for all drawing to LCD. Benefits aren't fully realised at the moment
 * due to busy loops waiting for DMA completion. */
#define USE_DMA		0

/**
 * Reducing VSYNC calculation to lower multiple.
 * When setting a clock IRQ to DMG_CLOCK_FREQ_REDUCED, count to
 * SCREEN_REFRESH_CYCLES_REDUCED to obtain the time required each VSYNC.
 * DMG_CLOCK_FREQ_REDUCED = 2^18, and SCREEN_REFRESH_CYCLES_REDUCED = 4389.
 * Currently unused.
 */
#define VSYNC_REDUCTION_FACTOR 16u
#define SCREEN_REFRESH_CYCLES_REDUCED (SCREEN_REFRESH_CYCLES/VSYNC_REDUCTION_FACTOR)
#define DMG_CLOCK_FREQ_REDUCED (DMG_CLOCK_FREQ/VSYNC_REDUCTION_FACTOR)

/* C Headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RP2040 Headers */
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/bootrom.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <sys/unistd.h>
#include <hardware/irq.h>

/* Project headers */
#include "hedley.h"
#include "minigb_apu.h"
#include "peanut_gb.h"
#include "mk_ili9225.h"
#include "sdcard.h"
#include "i2s.h"
#include "gbcolors.h"
#if ENABLE_NFC
#include "mfrc522.h"
#endif
#if ENABLE_SOUND
#include "boot_sound_data.h"
#endif

/* GPIO Connections. */
#if USE_DMG_BUTTON_MATRIX
#define GPIO_P10	6
#define GPIO_P11	2
#define GPIO_P12	5
#define GPIO_P13	4
#define GPIO_P14	3
#define GPIO_P15	7
#else
#define GPIO_UP		2
#define GPIO_DOWN	3
#define GPIO_LEFT	4
#define GPIO_RIGHT	5
#define GPIO_A		6
#define GPIO_B		7
#define GPIO_SELECT	8
#define GPIO_START	9
#endif
#define GPIO_CS		17
#define GPIO_CLK	18
#define GPIO_SDA	19
#define GPIO_RS		20
#define GPIO_RST	21
#define GPIO_LED	22

#define ROM_SELECTOR_PAGE_SIZE 22
#define ROM_SELECTOR_AUTOSTART_US (10ULL * 1000ULL * 1000ULL)
#ifndef DEFAULT_INTERLACE
#define DEFAULT_INTERLACE 0
#endif
#ifndef NFC_MAP_FILE
#define NFC_MAP_FILE "nfc_games.csv"
#endif
#ifndef RP2040_GB_SYS_CLOCK_VCO
#define RP2040_GB_SYS_CLOCK_VCO 1596000000u
#endif
#ifndef RP2040_GB_SYS_CLOCK_DIV1
#define RP2040_GB_SYS_CLOCK_DIV1 6u
#endif
#ifndef RP2040_GB_SYS_CLOCK_DIV2
#define RP2040_GB_SYS_CLOCK_DIV2 1u
#endif

#if defined(DISPLAY_ST7789)
#define ROM_SELECTOR_X 42u
#define ROM_SELECTOR_Y 0u
#define LOADING_SCREEN_X 18u
#define LOADING_SCREEN_WIDTH 320u
#define LOADING_SCREEN_HEIGHT 240u
#else
#define ROM_SELECTOR_X 0u
#define ROM_SELECTOR_Y 0u
#define LOADING_SCREEN_X 0u
#define LOADING_SCREEN_WIDTH SCREEN_SIZE_X
#define LOADING_SCREEN_HEIGHT SCREEN_SIZE_Y
#endif

#define LOADING_ANIM_FALL_DURATION_US (3ULL * 1000ULL * 1000ULL)
#define LOADING_ANIM_MIN_VISIBLE_US  (4ULL * 1000ULL * 1000ULL)
#define LOADING_ANIM_TEXT "NINTENDO"
#define LOADING_ANIM_TEXT_WIDTH 128u
#define LOADING_ANIM_TEXT_HEIGHT 16u
#define LOADING_ANIM_TARGET_Y ((LOADING_SCREEN_HEIGHT / 2u) - (LOADING_ANIM_TEXT_HEIGHT / 2u))
#define LOADING_ANIM_START_Y 0u

#if ENABLE_SOUND
/**
 * Global variables for audio task
 * stream contains N=AUDIO_SAMPLES samples
 * each sample is 32 bits
 * 16 bits for the left channel + 16 bits for the right channel in stereo interleaved format)
 * This is intended to be played at AUDIO_SAMPLE_RATE Hz
 */
uint16_t *stream;
static i2s_config_t i2s_config;
static bool i2s_config_ready = false;
#endif

/** Definition of ROM data
 * We're going to erase and reprogram a region 1Mb from the start of the flash
 * Once done, we can access this at XIP_BASE + 1Mb.
 * Game Boy DMG ROM size ranges from 32768 bytes (e.g. Tetris) to 1,048,576 bytes (e.g. Pokemod Red)
 */
#define FLASH_TARGET_OFFSET (1024 * 1024)
#define ROM_FLASH_MAX_SIZE (1024 * 1024)
static const uint8_t *rom = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
static uint32_t rom_size = ROM_FLASH_MAX_SIZE;
#if defined(RP2040_GB_HAS_BUILTIN_ROM)
extern const uint8_t rp2040_gb_builtin_rom[];
extern const unsigned int rp2040_gb_builtin_rom_size;
#endif
static unsigned char rom_bank0[65536];

static uint8_t ram[32768];
static palette_t palette;	// Colour palette
static uint8_t manual_palette_selected=0;
static volatile bool lcd_core_running = false;
static volatile bool lcd_core_ready = false;
static volatile bool loading_anim_active = false;
static volatile bool loading_anim_rom_done = false;
static volatile bool loading_anim_done = false;
static volatile uint64_t loading_anim_start_us = 0;
#if ENABLE_SOUND
static bool loading_anim_boot_sound_played = false;
#endif

static void ensure_lcd_core_running(void);
_Noreturn void main_core1(void);

#define LCD_LINE_QUEUE_DEPTH 8u
static uint8_t pixels_buffer[LCD_LINE_QUEUE_DEPTH][LCD_WIDTH];
static volatile uint8_t lcd_line_slot_state[LCD_LINE_QUEUE_DEPTH];
static uint8_t lcd_write_slot = 0u;

static struct
{
	unsigned a	: 1;
	unsigned b	: 1;
	unsigned select	: 1;
	unsigned start	: 1;
	unsigned right	: 1;
	unsigned left	: 1;
	unsigned up	: 1;
	unsigned down	: 1;
} prev_joypad_bits;

/* Multicore command structure. */
union core_cmd {
    struct {
	/* Does nothing. */
#define CORE_CMD_NOP		0
	/* Set line "data" on the LCD. Pixel data is in pixels_buffer. */
#define CORE_CMD_LCD_LINE	1
	/* Control idle mode on the LCD. Limits colours to 2 bits. */
#define CORE_CMD_IDLE_SET	2
	/* Set a specific pixel. For debugging. */
#define CORE_CMD_SET_PIXEL	3
	/* Start ROM-loading animation on the LCD core. */
#define CORE_CMD_LOADING_ANIM_START	4
	uint8_t cmd;
	uint8_t slot;
	uint8_t unused2;
	uint8_t data;
    };
    uint32_t full;
};

#define putstdio(x) write(1, x, strlen(x))

static void console_startup_sync(void)
{
	const uint32_t wait_for_usb_ms = 1500;
	const uint32_t settle_ms = 120;
	absolute_time_t deadline = make_timeout_time_ms(wait_for_usb_ms);

	while (!time_reached(deadline)) {
		if (stdio_usb_connected()) {
			break;
		}
		sleep_ms(10);
	}

	sleep_ms(settle_ms);

	for (unsigned i = 0; i < 3; ++i) {
		printf("\033c\033[2J\033[H");
		fflush(stdout);
		sleep_ms(40);
	}
}

/* Functions required for communication with the ILI9225. */
void mk_ili9225_set_rst(bool state)
{
	gpio_put(GPIO_RST, state);
}

void mk_ili9225_set_rs(bool state)
{
	gpio_put(GPIO_RS, state);
}

void mk_ili9225_set_cs(bool state)
{
	gpio_put(GPIO_CS, state);
}

void mk_ili9225_set_led(bool state)
{
	gpio_put(GPIO_LED, state);
}

void mk_ili9225_spi_write16(const uint16_t *halfwords, size_t len)
{
	spi_write16_blocking(spi0, halfwords, len);
}

void mk_ili9225_delay_ms(unsigned ms)
{
	sleep_ms(ms);
}

static void read_button_states(bool *up, bool *down, bool *left, bool *right,
			       bool *a, bool *b, bool *select, bool *start)
{
#if USE_DMG_BUTTON_MATRIX
	/* DMG joypad matrix:
	 * P14 low selects D-Pad: P10=Right, P11=Left, P12=Up, P13=Down
	 * P15 low selects buttons: P10=A, P11=B, P12=Select, P13=Start
	 * All lines are active-low. */
	gpio_put(GPIO_P14, 0);
	gpio_put(GPIO_P15, 1);
	sleep_us(5);
	*right = gpio_get(GPIO_P10);
	*left = gpio_get(GPIO_P11);
	*up = gpio_get(GPIO_P12);
	*down = gpio_get(GPIO_P13);

	gpio_put(GPIO_P14, 1);
	gpio_put(GPIO_P15, 0);
	sleep_us(5);
	*a = gpio_get(GPIO_P10);
	*b = gpio_get(GPIO_P11);
	*select = gpio_get(GPIO_P12);
	*start = gpio_get(GPIO_P13);

	gpio_put(GPIO_P14, 1);
	gpio_put(GPIO_P15, 1);
#else
	*up = gpio_get(GPIO_UP);
	*down = gpio_get(GPIO_DOWN);
	*left = gpio_get(GPIO_LEFT);
	*right = gpio_get(GPIO_RIGHT);
	*a = gpio_get(GPIO_A);
	*b = gpio_get(GPIO_B);
	*select = gpio_get(GPIO_SELECT);
	*start = gpio_get(GPIO_START);
#endif
}

static void init_button_gpio_only(void)
{
#if USE_DMG_BUTTON_MATRIX
	gpio_set_function(GPIO_P10, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P11, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P12, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P13, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P14, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P15, GPIO_FUNC_SIO);

	gpio_set_dir(GPIO_P10, false);
	gpio_set_dir(GPIO_P11, false);
	gpio_set_dir(GPIO_P12, false);
	gpio_set_dir(GPIO_P13, false);
	gpio_set_dir(GPIO_P14, true);
	gpio_set_dir(GPIO_P15, true);

	gpio_pull_up(GPIO_P10);
	gpio_pull_up(GPIO_P11);
	gpio_pull_up(GPIO_P12);
	gpio_pull_up(GPIO_P13);

	gpio_put(GPIO_P14, true);
	gpio_put(GPIO_P15, true);
#else
	gpio_set_function(GPIO_UP, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_DOWN, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LEFT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_RIGHT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_A, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_B, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_SELECT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_START, GPIO_FUNC_SIO);

	gpio_set_dir(GPIO_UP, false);
	gpio_set_dir(GPIO_DOWN, false);
	gpio_set_dir(GPIO_LEFT, false);
	gpio_set_dir(GPIO_RIGHT, false);
	gpio_set_dir(GPIO_A, false);
	gpio_set_dir(GPIO_B, false);
	gpio_set_dir(GPIO_SELECT, false);
	gpio_set_dir(GPIO_START, false);

	gpio_pull_up(GPIO_UP);
	gpio_pull_up(GPIO_DOWN);
	gpio_pull_up(GPIO_LEFT);
	gpio_pull_up(GPIO_RIGHT);
	gpio_pull_up(GPIO_A);
	gpio_pull_up(GPIO_B);
	gpio_pull_up(GPIO_SELECT);
	gpio_pull_up(GPIO_START);
#endif
}

static void check_bootsel_combo(void)
{
	bool up, down, left, right, a, b, select, start;

	init_button_gpio_only();
	sleep_ms(20);
	read_button_states(&up, &down, &left, &right, &a, &b, &select, &start);

	if(!select && !b) {
		printf("BOOTSEL: SELECT+B detected, entering USB boot\r\n");
		stdio_flush();
		sleep_ms(50);
		reset_usb_boot(0, 0);
	}
}

static void select_default_rom_source(void)
{
#if defined(RP2040_GB_HAS_BUILTIN_ROM)
	rom = rp2040_gb_builtin_rom;
	rom_size = rp2040_gb_builtin_rom_size;
#else
	rom = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
	rom_size = ROM_FLASH_MAX_SIZE;
#endif
}

#if ENABLE_NFC
static char nfc_hex_digit(uint8_t value)
{
	value &= 0x0Fu;
	return (value < 10u) ? (char)('0' + value) : (char)('A' + (value - 10u));
}

static char nfc_ascii_upper(char value)
{
	if(value >= 'a' && value <= 'z') {
		return (char)(value - ('a' - 'A'));
	}
	return value;
}

static void nfc_uid_to_hex(const mfrc522_uid_t *uid, char *uid_hex, size_t uid_hex_size)
{
	if(uid_hex == NULL || uid_hex_size == 0u) {
		return;
	}
	uid_hex[0] = '\0';
	if(uid == NULL || uid_hex_size < ((size_t)uid->len * 2u + 1u)) {
		return;
	}

	for(uint8_t i = 0; i < uid->len; ++i) {
		uid_hex[i * 2u] = nfc_hex_digit((uint8_t)(uid->bytes[i] >> 4u));
		uid_hex[i * 2u + 1u] = nfc_hex_digit(uid->bytes[i]);
	}
	uid_hex[(size_t)uid->len * 2u] = '\0';
}

static bool nfc_uid_field_matches(const char *field, const char *uid_hex)
{
	size_t uid_pos = 0;

	for(const char *p = field; *p != '\0' && *p != ';' && *p != ',' && *p != '\r' && *p != '\n'; ++p) {
		if(*p == ' ' || *p == '\t' || *p == ':' || *p == '-') {
			continue;
		}
		if(uid_hex[uid_pos] == '\0' || nfc_ascii_upper(*p) != uid_hex[uid_pos]) {
			return false;
		}
		uid_pos++;
	}

	return uid_hex[uid_pos] == '\0';
}

static void nfc_trim_filename(char *filename)
{
	size_t len;
	char *src = filename;

	while(*src == ' ' || *src == '\t') {
		src++;
	}
	if(src != filename) {
		memmove(filename, src, strlen(src) + 1u);
	}

	len = strlen(filename);
	while(len > 0u && (filename[len - 1u] == ' ' || filename[len - 1u] == '\t' ||
	                  filename[len - 1u] == '\r' || filename[len - 1u] == '\n')) {
		filename[--len] = '\0';
	}
}

static bool nfc_lookup_rom_from_sd(const mfrc522_uid_t *uid, char *rom_filename, size_t rom_filename_size)
{
	char uid_hex[MFRC522_UID_MAX_LEN * 2u + 1u];
	char line[160];
	sd_card_t *pSD = sd_get_by_num(0);
	FIL fil;
	FRESULT fr;

	if(pSD == NULL) {
		printf("NFC: SD driver not available for mapping file\n");
		return false;
	}

	nfc_uid_to_hex(uid, uid_hex, sizeof(uid_hex));
	if(uid_hex[0] == '\0') {
		printf("NFC: invalid UID length for mapping\n");
		return false;
	}

	fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
	if(fr != FR_OK) {
		printf("NFC: f_mount error while opening %s: %s (%d)\n", NFC_MAP_FILE, FRESULT_str(fr), fr);
		return false;
	}

	fr = f_open(&fil, NFC_MAP_FILE, FA_READ);
	if(fr != FR_OK) {
		printf("NFC: mapping file %s not available: %s (%d)\n", NFC_MAP_FILE, FRESULT_str(fr), fr);
		f_unmount(pSD->pcName);
		return false;
	}

	printf("NFC: looking up UID %s in %s\n", uid_hex, NFC_MAP_FILE);
	while(f_gets(line, sizeof(line), &fil) != NULL) {
		char *cursor = line;
		char *separator;

		while(*cursor == ' ' || *cursor == '\t') {
			cursor++;
		}
		if(*cursor == '\0' || *cursor == '\r' || *cursor == '\n' || *cursor == '#') {
			continue;
		}

		separator = strchr(cursor, ';');
		if(separator == NULL) {
			separator = strchr(cursor, ',');
		}
		if(separator == NULL) {
			continue;
		}

		if(nfc_uid_field_matches(cursor, uid_hex)) {
			strncpy(rom_filename, separator + 1, rom_filename_size - 1u);
			rom_filename[rom_filename_size - 1u] = '\0';
			nfc_trim_filename(rom_filename);
			f_close(&fil);
			f_unmount(pSD->pcName);

			if(rom_filename[0] == '\0') {
				printf("NFC: empty ROM mapping for UID %s\n", uid_hex);
				return false;
			}
			printf("NFC: matched UID %s -> %s\n", uid_hex, rom_filename);
			return true;
		}
	}

	f_close(&fil);
	f_unmount(pSD->pcName);
	printf("NFC: no ROM mapping for UID %s\n", uid_hex);
	return false;
}

static void nfc_print_uid(const mfrc522_uid_t *uid)
{
	for(uint8_t i = 0; i < uid->len; ++i) {
		printf("%02X", uid->bytes[i]);
	}
}

#define NFC_BOOT_SCAN_ATTEMPTS 2u
#define NFC_BOOT_SCAN_DELAY_MS 100u

static void draw_scaled_text(const char *text, uint8_t x, uint8_t y, uint8_t scale,
			      bool bold, uint16_t color, uint16_t bgcolor)
{
	uint16_t glyph[8u * 8u];
	uint16_t scaled[16u * 16u];
	size_t len;

	if(text == NULL || scale != 2u) {
		return;
	}

	len = strlen(text);
	for(size_t i = 0; i < len; ++i) {
		memset(scaled, 0, sizeof(scaled));
		mk_ili9225_get_letter(glyph, text[i], color, bgcolor);
		for(uint8_t gy = 0; gy < 8u; ++gy) {
			for(uint8_t gx = 0; gx < 8u; ++gx) {
				uint16_t src = glyph[(gy * 8u) + gx];
				for(uint8_t sy = 0; sy < 2u; ++sy) {
					for(uint8_t sx = 0; sx < 2u; ++sx) {
						uint8_t dx = (uint8_t)(gx * 2u + sx);
						uint8_t dy = (uint8_t)(gy * 2u + sy);
						scaled[(dy * 16u) + dx] = src;
						if(bold && src == color && (dx + 1u) < 16u) {
							scaled[(dy * 16u) + dx + 1u] = color;
						}
					}
				}
			}
		}
		mk_ili9225_blit(scaled, (uint8_t)(x + (uint8_t)(i * 16u)), y, 16u, 16u);
	}
}

#if ENABLE_SOUND
static void play_boot_sound(void)
{
	int16_t boot_buf[AUDIO_SAMPLES * 2u];
	const size_t output_frames = (boot_sound_data_len * AUDIO_SAMPLE_RATE) / BOOT_SOUND_SAMPLE_RATE;

	if(!i2s_config_ready || boot_sound_data_len == 0u) {
		return;
	}

	for(size_t out_pos = 0; out_pos < output_frames; out_pos += AUDIO_SAMPLES) {
		const size_t chunk_frames = ((output_frames - out_pos) > AUDIO_SAMPLES) ? AUDIO_SAMPLES : (output_frames - out_pos);
		for(size_t i = 0; i < AUDIO_SAMPLES; ++i) {
			int16_t sample = 0;
			if(i < chunk_frames) {
				const size_t src_index = ((out_pos + i) * BOOT_SOUND_SAMPLE_RATE) / AUDIO_SAMPLE_RATE;
				const uint8_t pcm = boot_sound_data[src_index < boot_sound_data_len ? src_index : (boot_sound_data_len - 1u)];
				sample = (int16_t)(((int)pcm - 128) << 8);
			}
			boot_buf[i * 2u + 0u] = sample;
			boot_buf[i * 2u + 1u] = sample;
		}
		i2s_dma_write(&i2s_config, boot_buf);
	}
}

static void maybe_play_loading_boot_sound(void)
{
	uint64_t start_us;
	uint64_t elapsed_us;

	if(loading_anim_boot_sound_played) {
		return;
	}

	start_us = __atomic_load_n(&loading_anim_start_us, __ATOMIC_SEQ_CST);
	if(start_us == 0u) {
		return;
	}

	elapsed_us = time_us_64() - start_us;
	if(elapsed_us < LOADING_ANIM_FALL_DURATION_US) {
		return;
	}

	loading_anim_boot_sound_played = true;
	play_boot_sound();
}
#endif

static bool nfc_try_select_rom(char *rom_filename, size_t rom_filename_size)
{
	mfrc522_uid_t uid;
	uint8_t version;
	uint64_t scan_start_us;

	if(rom_filename == NULL || rom_filename_size == 0u) {
		return false;
	}
	rom_filename[0] = '\0';

	printf("NFC: init RC522 on spi1 CS=GP%d RST=GP%d\n", MFRC522_PIN_CS, MFRC522_PIN_RST);
	mfrc522_init();
	version = mfrc522_version();
	printf("NFC: RC522 VersionReg=0x%02X\n", version);
	if(version == 0x00u || version == 0xFFu) {
		printf("NFC: RC522 not detected, skipping NFC boot\n");
		return false;
	}

	printf("NFC: waiting for boot tag\n");
	scan_start_us = time_us_64();
	for(uint8_t attempt = 0; attempt < NFC_BOOT_SCAN_ATTEMPTS; ++attempt) {
		if(mfrc522_read_uid(&uid)) {
			uint32_t elapsed_ms = (uint32_t)((time_us_64() - scan_start_us) / 1000u);
			printf("NFC: detected on attempt %u/%u after %lu ms during startup scan\n",
			       (unsigned)(attempt + 1u),
			       (unsigned)NFC_BOOT_SCAN_ATTEMPTS,
			       (unsigned long)elapsed_ms);
			printf("NFC: UID=");
			nfc_print_uid(&uid);
			printf("\n");
			return nfc_lookup_rom_from_sd(&uid, rom_filename, rom_filename_size);
		}
		sleep_ms(NFC_BOOT_SCAN_DELAY_MS);
	}

	printf("NFC: no tag found, showing ROM selector\n");
	return false;
}
#endif

static void build_save_filename(struct gb_s *gb, char *filename, size_t filename_size)
{
	char rom_name[16];

	if(filename == NULL || filename_size == 0u) {
		return;
	}

	gb_get_rom_name(gb, rom_name);
	snprintf(filename, filename_size, "%s.sav", rom_name);
}

static inline void rom_selector_text(char *text, uint8_t row, uint16_t color, uint16_t bgcolor)
{
	mk_ili9225_text(text, ROM_SELECTOR_X, (uint8_t)(ROM_SELECTOR_Y + row * 8u), color, bgcolor);
}

static void refresh_rom_bank0(void)
{
	uint32_t copy_size = rom_size;

	if(copy_size > sizeof(rom_bank0)) {
		copy_size = sizeof(rom_bank0);
	}

	memset(rom_bank0, 0xFF, sizeof(rom_bank0));
	if(copy_size > 0) {
		memcpy(rom_bank0, rom, copy_size);
	}
}

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
	(void) gb;
	if(addr < sizeof(rom_bank0))
		return rom_bank0[addr];

	if(addr < rom_size) {
		return rom[addr];
	}

	return 0xFF;
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
	(void) gb;
	return ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
		       const uint8_t val)
{
	ram[addr] = val;
}

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t addr)
{
#if 1
	const char* gb_err_str[4] = {
			"UNKNOWN",
			"INVALID OPCODE",
			"INVALID READ",
			"INVALID WRITE"
		};
	printf("Error %d occurred: %s at %04X\n.\n", gb_err, gb_err_str[gb_err], addr);
//	abort();
#endif
}

#if ENABLE_LCD 
void core1_lcd_draw_line(const uint_fast8_t slot, const uint_fast8_t line)
{
	static uint16_t fb[LCD_WIDTH];
	const uint8_t *line_pixels = pixels_buffer[slot];

	for(unsigned int x = 0; x < LCD_WIDTH; x++)
	{
		fb[x] = palette[(line_pixels[x] & LCD_PALETTE_ALL) >> 4]
				[line_pixels[x] & 3];
	}

	mk_ili9225_set_x(line + 16);
	mk_ili9225_write_pixels(fb, LCD_WIDTH);
	__atomic_store_n(&lcd_line_slot_state[slot], 0u, __ATOMIC_SEQ_CST);
}

static void core1_loading_animation_tick(void)
{
	static bool drawn = false;
	static uint8_t prev_y = LOADING_ANIM_START_Y;
	static const char logo_text[] = LOADING_ANIM_TEXT;
	uint64_t start_us = __atomic_load_n(&loading_anim_start_us, __ATOMIC_SEQ_CST);
	uint64_t elapsed_us;
	uint8_t y = LOADING_ANIM_TARGET_Y;
	const uint8_t x = (uint8_t)(LOADING_SCREEN_X + ((LOADING_SCREEN_WIDTH - LOADING_ANIM_TEXT_WIDTH) / 2u));

	if(start_us == 0u) {
		start_us = time_us_64();
		__atomic_store_n(&loading_anim_start_us, start_us, __ATOMIC_SEQ_CST);
		elapsed_us = 0u;
	} else {
		elapsed_us = time_us_64() - start_us;
	}

	if (elapsed_us < LOADING_ANIM_FALL_DURATION_US) {
		y = (uint8_t)(LOADING_ANIM_START_Y +
			     (((uint64_t)(LOADING_ANIM_TARGET_Y - LOADING_ANIM_START_Y) * elapsed_us) /
			      LOADING_ANIM_FALL_DURATION_US));
	}

	if (!drawn || y != prev_y) {
		if (drawn) {
			draw_scaled_text(logo_text, x, prev_y, 2u, true, 0x0000, 0x0000);
		}
		draw_scaled_text(logo_text, x, y, 2u, true, 0xFFFF, 0x0000);
		prev_y = y;
		drawn = true;
	}

	if (elapsed_us >= LOADING_ANIM_MIN_VISIBLE_US &&
	    __atomic_load_n(&loading_anim_rom_done, __ATOMIC_SEQ_CST)) {
		mk_ili9225_fill(0x0000);
		drawn = false;
		prev_y = LOADING_ANIM_START_Y;
		__atomic_store_n(&loading_anim_active, false, __ATOMIC_SEQ_CST);
		__atomic_store_n(&loading_anim_done, true, __ATOMIC_SEQ_CST);
	}
}

static void lcd_start_loading_animation(void)
{
	union core_cmd cmd;
	__atomic_store_n(&loading_anim_done, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&loading_anim_rom_done, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&loading_anim_start_us, 0u, __ATOMIC_SEQ_CST);
#if ENABLE_SOUND
	loading_anim_boot_sound_played = false;
#endif
	__atomic_store_n(&loading_anim_active, true, __ATOMIC_SEQ_CST);
	cmd.cmd = CORE_CMD_LOADING_ANIM_START;
	cmd.data = 0u;
	multicore_fifo_push_blocking(cmd.full);
}

static void lcd_wait_loading_animation_done(void)
{
	while(!__atomic_load_n(&loading_anim_done, __ATOMIC_SEQ_CST)) {
#if ENABLE_SOUND
		maybe_play_loading_boot_sound();
#endif
		tight_loop_contents();
	}
}

static void ensure_lcd_core_running(void)
{
	if (__atomic_load_n(&lcd_core_running, __ATOMIC_SEQ_CST)) {
		return;
	}

	__atomic_store_n(&lcd_core_ready, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&lcd_core_running, true, __ATOMIC_SEQ_CST);
	putstdio("CORE1 ");
	multicore_launch_core1(main_core1);
	while(!__atomic_load_n(&lcd_core_ready, __ATOMIC_SEQ_CST)) {
		tight_loop_contents();
	}
	putstdio("LCD ");
}

_Noreturn
void main_core1(void)
{
	union core_cmd cmd;

	/* Initialise and control LCD on core 1. */
	mk_ili9225_init();

	/* Clear LCD screen. */
	mk_ili9225_fill(0x0000);
	__atomic_store_n(&lcd_core_ready, true, __ATOMIC_SEQ_CST);

	/* Set LCD window to DMG size. */
#if defined(DISPLAY_ILI9225)
	mk_ili9225_fill_rect(31,16,LCD_WIDTH,LCD_HEIGHT,0x0000);
#endif

	// Sleep used for debugging LCD window.
	//sleep_ms(1000);

	/* Handle commands coming from core0. */
	while(1)
	{
		if(multicore_fifo_rvalid()) {
			cmd.full = multicore_fifo_pop_blocking();
			switch(cmd.cmd)
			{
			case CORE_CMD_LCD_LINE:
				core1_lcd_draw_line(cmd.slot, cmd.data);
				break;

			case CORE_CMD_IDLE_SET:
				mk_ili9225_display_control(true, cmd.data);
				break;

			case CORE_CMD_LOADING_ANIM_START:
				mk_ili9225_fill(0x0000);
				break;

			case CORE_CMD_NOP:
			default:
				break;
			}
		} else if(__atomic_load_n(&loading_anim_active, __ATOMIC_SEQ_CST)) {
			core1_loading_animation_tick();
			sleep_ms(10);
		} else {
			tight_loop_contents();
		}
	}

	HEDLEY_UNREACHABLE();
}
#endif

#if ENABLE_LCD
void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH],
		   const uint_fast8_t line)
{
	union core_cmd cmd;
	uint8_t slot = lcd_write_slot;

	while(__atomic_load_n(&lcd_line_slot_state[slot], __ATOMIC_SEQ_CST))
		tight_loop_contents();

	memcpy(pixels_buffer[slot], pixels, LCD_WIDTH);
	
	/* Populate command. */
	cmd.cmd = CORE_CMD_LCD_LINE;
	cmd.slot = slot;
	cmd.data = line;

	__atomic_store_n(&lcd_line_slot_state[slot], 1u, __ATOMIC_SEQ_CST);
	multicore_fifo_push_blocking(cmd.full);
	lcd_write_slot = (uint8_t)((slot + 1u) % LCD_LINE_QUEUE_DEPTH);
}
#endif

#if ENABLE_SDCARD
static bool sd_storage_ready(void)
{
	sd_card_t *pSD = sd_get_by_num(0);
	FRESULT fr;

	if(pSD == NULL) {
		printf("W SD disabled: no SD driver config\n");
		return false;
	}

	fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
	if(fr != FR_OK) {
		printf("W SD unavailable: %s (%d). Continuing without SD features.\n",
		       FRESULT_str(fr), fr);
		return false;
	}

	f_unmount(pSD->pcName);
	return true;
}

/**
 * Load a save file from the SD card
 */
void read_cart_ram_file(struct gb_s *gb) {
	char filename[32];
	uint_fast32_t save_size;
	UINT br;
	
	build_save_filename(gb, filename, sizeof(filename));
	save_size=gb_get_save_size(gb);
	if(save_size>0) {
		sd_card_t *pSD=sd_get_by_num(0);
		FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
		if (FR_OK!=fr) {
			printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
			return;
		}

		FIL fil;
		bool file_open = false;
		fr=f_open(&fil,filename,FA_READ);
		if (fr==FR_OK) {
			file_open = true;
			f_read(&fil,ram,f_size(&fil),&br);
		} else if(fr == FR_NO_FILE) {
			printf("W save file %s does not exist yet; starting with empty cartridge RAM\n", filename);
		} else {
			printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
		}
		
		if(file_open) {
			fr=f_close(&fil);
			if(fr!=FR_OK) {
				printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
			}
		}
		f_unmount(pSD->pcName);
	}
	printf("I read_cart_ram_file(%s) COMPLETE (%lu bytes)\n",filename,save_size);
}

/**
 * Write a save file to the SD card
 */
void write_cart_ram_file(struct gb_s *gb) {
	char filename[32];
	uint_fast32_t save_size;
	UINT bw;
	
	build_save_filename(gb, filename, sizeof(filename));
	save_size=gb_get_save_size(gb);
	if(save_size>0) {
		sd_card_t *pSD=sd_get_by_num(0);
		FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
		if (FR_OK!=fr) {
			printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
			return;
		}

		FIL fil;
		bool file_open = false;
		fr=f_open(&fil,filename,FA_CREATE_ALWAYS | FA_WRITE);
		if (fr==FR_OK) {
			file_open = true;
			f_write(&fil,ram,save_size,&bw);
		} else {
			printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
		}
		
		if(file_open) {
			fr=f_close(&fil);
			if(fr!=FR_OK) {
				printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
			}
		}
		f_unmount(pSD->pcName);
	}
	printf("I write_cart_ram_file(%s) COMPLETE (%lu bytes)\n",filename,save_size);
}

/**
 * Load a .gb rom file in flash from the SD card 
 */ 
void load_cart_rom_file(char *filename) {
	UINT br;
	uint8_t buffer[FLASH_SECTOR_SIZE];
	bool mismatch=false;
	uint32_t bytes_loaded = 0;
	const uint8_t *flash_rom = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
	sd_card_t *pSD=sd_get_by_num(0);
	printf("[I] ROM loading starts: %s\n", filename);
#if ENABLE_LCD
	ensure_lcd_core_running();
	lcd_start_loading_animation();
#endif
	FRESULT fr=f_mount(&pSD->fatfs,pSD->pcName,1);
	if (FR_OK!=fr) {
		printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
#if ENABLE_LCD
		__atomic_store_n(&loading_anim_rom_done, true, __ATOMIC_SEQ_CST);
		lcd_wait_loading_animation_done();
#endif
		return;
	}
	FIL fil;
	fr=f_open(&fil,filename,FA_READ);
	if (fr==FR_OK) {
		uint32_t flash_target_offset=FLASH_TARGET_OFFSET;
		for(;;) {
#if ENABLE_SOUND
			maybe_play_loading_boot_sound();
#endif
			memset(buffer,0xFF,sizeof buffer);
			f_read(&fil,buffer,sizeof buffer,&br);
			if(br==0) break; /* end of file */

			if((flash_target_offset + FLASH_SECTOR_SIZE) > (FLASH_TARGET_OFFSET + ROM_FLASH_MAX_SIZE)) {
				printf("E ROM file too large for reserved flash region (%u bytes max)\n",
				       ROM_FLASH_MAX_SIZE);
				mismatch=true;
				break;
			}

			flash_range_erase(flash_target_offset,FLASH_SECTOR_SIZE);
			flash_range_program(flash_target_offset,buffer,FLASH_SECTOR_SIZE);
			
			/* Read back target region and check programming */
			for(uint32_t i=0;i<FLASH_SECTOR_SIZE;i++) {
				if(flash_rom[(flash_target_offset-FLASH_TARGET_OFFSET)+i]!=buffer[i]) {
					mismatch=true;
				}
			}
			bytes_loaded += br;

			/* Next sector */
			flash_target_offset+=FLASH_SECTOR_SIZE;
		}
		if(!mismatch && bytes_loaded > 0) {
			rom = flash_rom;
			rom_size = bytes_loaded;
	        printf("[I] Programming successful!\n");
		} else {
			printf("E Programming failed!\n");
		}
	} else {
		printf("E f_open(%s) error: %s (%d)\n",filename,FRESULT_str(fr),fr);
	}
	
	fr=f_close(&fil);
	if(fr!=FR_OK) {
		printf("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
	}
	f_unmount(pSD->pcName);
#if ENABLE_LCD
	__atomic_store_n(&loading_anim_rom_done, true, __ATOMIC_SEQ_CST);
	lcd_wait_loading_animation_done();
#endif

	printf("I load_cart_rom_file(%s) COMPLETE (%lu bytes)\n",filename,(unsigned long)bytes_loaded);
}

/**
 * Function used by the rom file selector to display one page of .gb rom files
 */
uint16_t rom_file_selector_display_page(char filename[ROM_SELECTOR_PAGE_SIZE][256],uint16_t num_page) {
	sd_card_t *pSD=sd_get_by_num(0);
    DIR dj;
    FILINFO fno;
    FRESULT fr;

    fr=f_mount(&pSD->fatfs,pSD->pcName,1);
    if (FR_OK!=fr) {
        printf("E f_mount error: %s (%d)\n",FRESULT_str(fr),fr);
        return 0;
    }

	/* clear the filenames array */
	for(uint8_t ifile=0;ifile<ROM_SELECTOR_PAGE_SIZE;ifile++) {
		strcpy(filename[ifile],"");
	}

    /* search *.gb files */
	uint16_t num_file=0;
	fr=f_findfirst(&dj, &fno, "", "*.gb");

	/* skip the first N pages */
	if(num_page>0) {
		while(num_file<num_page*ROM_SELECTOR_PAGE_SIZE && fr == FR_OK && fno.fname[0]) {
			num_file++;
			fr=f_findnext(&dj, &fno);
		}
	}

	/* store the filenames of this page */
	num_file=0;
	while(num_file<ROM_SELECTOR_PAGE_SIZE && fr == FR_OK && fno.fname[0]) {
		strcpy(filename[num_file],fno.fname);
		num_file++;
		fr=f_findnext(&dj, &fno);
	}
	f_closedir(&dj);
	f_unmount(pSD->pcName);

	/* display *.gb rom files on screen */
	mk_ili9225_fill(0x0000);
	for(uint8_t ifile=0;ifile<num_file;ifile++) {
		rom_selector_text(filename[ifile], ifile, 0xFFFF, 0x0000);
    }
	return num_file;
}

/**
 * The ROM selector displays pages of up to ROM_SELECTOR_PAGE_SIZE rom files
 * allowing the user to select which rom file to start
 * Copy your *.gb rom files to the root directory of the SD card
 */
void rom_file_selector(const char *auto_rom_filename) {
	uint16_t num_page=0;
	char filename[ROM_SELECTOR_PAGE_SIZE][256];
	uint16_t num_file;
	
	/* display the first page with up to ROM_SELECTOR_PAGE_SIZE rom files */
	num_file=rom_file_selector_display_page(filename,num_page);
	if(num_file==0) {
		return;
	}

	/* select the first rom */
	uint8_t selected=0;
	rom_selector_text(filename[selected], selected, 0xFFFF, 0xF800);

	if(auto_rom_filename != NULL && auto_rom_filename[0] != '\0') {
		printf("I ROM selector auto-start request: %s\n", auto_rom_filename);
		load_cart_rom_file((char *)auto_rom_filename);
		return;
	}

	/* get user's input */
	bool up,down,left,right,a,b,select,start;
	bool autostart_enabled=true;
	uint64_t autostart_start_time=time_us_64();
	while(true) {
		read_button_states(&up, &down, &left, &right, &a, &b, &select, &start);
		bool any_pressed=(!up) || (!down) || (!left) || (!right) ||
				 (!a) || (!b) || (!select) || (!start);
		if(autostart_enabled && !any_pressed &&
		   (time_us_64() - autostart_start_time) >= ROM_SELECTOR_AUTOSTART_US) {
			printf("I ROM selector idle timeout: auto-starting %s\n", filename[0]);
			load_cart_rom_file(filename[0]);
			break;
		}
		if(any_pressed) {
			autostart_enabled=false;
		}
		if(!start) {
			/* re-start the last game (no need to reprogram flash) */
			break;
		}
		if(!a | !b) {
			/* copy the rom from the SD card to flash and start the game */
			load_cart_rom_file(filename[selected]);
			break;
		}
		if(!down) {
			/* select the next rom */
			rom_selector_text(filename[selected], selected, 0xFFFF, 0x0000);
			selected++;
			if(selected>=num_file) selected=0;
			rom_selector_text(filename[selected], selected, 0xFFFF, 0xF800);
			sleep_ms(150);
		}
		if(!up) {
			/* select the previous rom */
			rom_selector_text(filename[selected], selected, 0xFFFF, 0x0000);
			if(selected==0) {
				selected=num_file-1;
			} else {
				selected--;
			}
			rom_selector_text(filename[selected], selected, 0xFFFF, 0xF800);
			sleep_ms(150);
		}
		if(!right) {
			/* select the next page */
			num_page++;
			num_file=rom_file_selector_display_page(filename,num_page);
			if(num_file==0) {
				/* no files in this page, go to the previous page */
				num_page--;
				num_file=rom_file_selector_display_page(filename,num_page);
			}
			/* select the first file */
			selected=0;
			rom_selector_text(filename[selected], selected, 0xFFFF, 0xF800);
			sleep_ms(150);
		}
		if((!left) && num_page>0) {
			/* select the previous page */
			num_page--;
			num_file=rom_file_selector_display_page(filename,num_page);
			/* select the first file */
			selected=0;
			rom_selector_text(filename[selected], selected, 0xFFFF, 0xF800);
			sleep_ms(150);
		}
		tight_loop_contents();
	}
}

#endif

int main(void)
{
	static struct gb_s gb;
	enum gb_init_error_e ret;
#if ENABLE_SDCARD
	bool sd_ready = false;
#endif
#if ENABLE_NFC
	char nfc_rom_filename[256];
	bool nfc_boot_pending = false;
#endif

	/* Initialise USB serial connection for debugging. */
	stdio_init_all();
	console_startup_sync();
	time_init();
	putstdio("INIT: ");
	check_bootsel_combo();

#if ENABLE_NFC
	nfc_boot_pending = nfc_try_select_rom(nfc_rom_filename, sizeof(nfc_rom_filename));
#endif

	/* Overclock. */
	{
		const unsigned vco = RP2040_GB_SYS_CLOCK_VCO;
		const unsigned div1 = RP2040_GB_SYS_CLOCK_DIV1;
		const unsigned div2 = RP2040_GB_SYS_CLOCK_DIV2;

		vreg_set_voltage(VREG_VOLTAGE_1_15);
		sleep_ms(2);
		set_sys_clock_pll(vco, div1, div2);
		sleep_ms(2);
		printf("Clock: sys=%lu Hz (vco=%u div1=%u div2=%u)\n",
		       (unsigned long)clock_get_hz(clk_sys),
		       vco, div1, div2);
	}

	/* Initialise GPIO pins. */
#if USE_DMG_BUTTON_MATRIX
	gpio_set_function(GPIO_P10, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P11, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P12, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P13, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P14, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_P15, GPIO_FUNC_SIO);
#else
	gpio_set_function(GPIO_UP, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_DOWN, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LEFT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_RIGHT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_A, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_B, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_SELECT, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_START, GPIO_FUNC_SIO);
#endif
	gpio_set_function(GPIO_CS, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_CLK, GPIO_FUNC_SPI);
	gpio_set_function(GPIO_SDA, GPIO_FUNC_SPI);
	gpio_set_function(GPIO_RS, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_RST, GPIO_FUNC_SIO);
	gpio_set_function(GPIO_LED, GPIO_FUNC_SIO);

#if USE_DMG_BUTTON_MATRIX
	gpio_set_dir(GPIO_P10, false);
	gpio_set_dir(GPIO_P11, false);
	gpio_set_dir(GPIO_P12, false);
	gpio_set_dir(GPIO_P13, false);
	gpio_set_dir(GPIO_P14, true);
	gpio_set_dir(GPIO_P15, true);
#else
	gpio_set_dir(GPIO_UP, false);
	gpio_set_dir(GPIO_DOWN, false);
	gpio_set_dir(GPIO_LEFT, false);
	gpio_set_dir(GPIO_RIGHT, false);
	gpio_set_dir(GPIO_A, false);
	gpio_set_dir(GPIO_B, false);
	gpio_set_dir(GPIO_SELECT, false);
	gpio_set_dir(GPIO_START, false);
#endif
	gpio_set_dir(GPIO_CS, true);
	gpio_set_dir(GPIO_RS, true);
	gpio_set_dir(GPIO_RST, true);
	gpio_set_dir(GPIO_LED, true);
	gpio_set_slew_rate(GPIO_CLK, GPIO_SLEW_RATE_FAST);
	gpio_set_slew_rate(GPIO_SDA, GPIO_SLEW_RATE_FAST);
	
#if USE_DMG_BUTTON_MATRIX
	gpio_pull_up(GPIO_P10);
	gpio_pull_up(GPIO_P11);
	gpio_pull_up(GPIO_P12);
	gpio_pull_up(GPIO_P13);

	/* Deselect both matrix lines by default. */
	gpio_put(GPIO_P14, true);
	gpio_put(GPIO_P15, true);
#else
	gpio_pull_up(GPIO_UP);
	gpio_pull_up(GPIO_DOWN);
	gpio_pull_up(GPIO_LEFT);
	gpio_pull_up(GPIO_RIGHT);
	gpio_pull_up(GPIO_A);
	gpio_pull_up(GPIO_B);
	gpio_pull_up(GPIO_SELECT);
	gpio_pull_up(GPIO_START);
#endif

	/* Set SPI clock to use high frequency. */
	clock_configure(clk_peri, 0,
			CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
			125 * 1000 * 1000, 125 * 1000 * 1000);
#if defined(DISPLAY_ST7789)
	spi_init(spi0, 50*1000*1000);
	spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
#else
	spi_init(spi0, 30*1000*1000);
	spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
#endif

#if ENABLE_SOUND
	// Allocate memory for the stream buffer
	stream=malloc(AUDIO_BUFFER_SIZE_BYTES);
    assert(stream!=NULL);
    memset(stream,0,AUDIO_BUFFER_SIZE_BYTES);  // Zero out the stream buffer
	
	// Initialize I2S sound driver
	i2s_config = i2s_get_default_config();
	i2s_config.sample_freq=AUDIO_SAMPLE_RATE;
	i2s_config.dma_trans_count =AUDIO_SAMPLES;
	i2s_volume(&i2s_config,AUDIO_DEFAULT_VOLUME);
	i2s_init(&i2s_config);
	i2s_config_ready = true;
#endif

#if ENABLE_SDCARD
	sd_ready = sd_storage_ready();
#endif

while(true)
{
		select_default_rom_source();

#if ENABLE_SDCARD
		if(sd_ready) {
			{
#if ENABLE_LCD
				/* ROM File selector */
				mk_ili9225_init();
				mk_ili9225_fill(0x0000);
#if ENABLE_NFC
				if(nfc_boot_pending) {
					printf("NFC: auto-starting %s via ROM selector path\n", nfc_rom_filename);
					rom_file_selector(nfc_rom_filename);
					nfc_boot_pending = false;
				} else
#endif
				{
					rom_file_selector(NULL);
				}
#endif
			}
		}
#endif

		/* Initialise GB context. */
		refresh_rom_bank0();
		ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read,
			      &gb_cart_ram_write, &gb_error, NULL);
	putstdio("GB ");

	if(ret != GB_INIT_NO_ERROR)
	{
		printf("Error: %d\n", ret);
		goto out;
	}

	/* Default to original DMG-green palette for every game boot. */
	char rom_title[16];
	manual_palette_selected = NUMBER_OF_MANUAL_PALETTES - 1;
	manual_assign_palette(palette, manual_palette_selected);
	printf("I default palette = %u (DMG green)\n", manual_palette_selected);
	
#if ENABLE_LCD
	gb_init_lcd(&gb, &lcd_draw_line);
	gb.direct.interlace = DEFAULT_INTERLACE;
	printf("I default interlace = %u\n", (unsigned)gb.direct.interlace);

	/* Start Core1, which processes requests to the LCD. */
	ensure_lcd_core_running();
#endif

#if ENABLE_SOUND
	// Initialize audio emulation
	audio_init();
	
	putstdio("AUDIO ");
#endif

#if ENABLE_SDCARD
		/* Load Save File. */
		if(sd_ready) {
			read_cart_ram_file(&gb);
		}
#endif

	putstdio("\n> ");
	uint_fast32_t frames = 0;
	uint64_t start_time = time_us_64();
	uint32_t fps_frames = 0;
	uint64_t fps_report_time = start_time;
	while(1)
	{
		int input;
		uint64_t frame_start_time = time_us_64();

		gb.gb_frame = 0;

		do {
			__gb_step_cpu(&gb);
			tight_loop_contents();
		} while(HEDLEY_LIKELY(gb.gb_frame == 0));

		frames++;
		fps_frames++;
#if ENABLE_SOUND
		if(!gb.direct.frame_skip) {
			audio_callback(NULL, stream, AUDIO_BUFFER_SIZE_BYTES);
			i2s_dma_write(&i2s_config, stream);
		}
#endif

		/* Update buttons state */
		bool up,down,left,right,a,b,select,start;
		prev_joypad_bits.up=gb.direct.joypad_bits.up;
		prev_joypad_bits.down=gb.direct.joypad_bits.down;
		prev_joypad_bits.left=gb.direct.joypad_bits.left;
		prev_joypad_bits.right=gb.direct.joypad_bits.right;
		prev_joypad_bits.a=gb.direct.joypad_bits.a;
		prev_joypad_bits.b=gb.direct.joypad_bits.b;
		prev_joypad_bits.select=gb.direct.joypad_bits.select;
		prev_joypad_bits.start=gb.direct.joypad_bits.start;
		read_button_states(&up, &down, &left, &right, &a, &b, &select, &start);
		gb.direct.joypad_bits.up = up;
		gb.direct.joypad_bits.down = down;
		gb.direct.joypad_bits.left = left;
		gb.direct.joypad_bits.right = right;
		gb.direct.joypad_bits.a = a;
		gb.direct.joypad_bits.b = b;
		gb.direct.joypad_bits.select = select;
		gb.direct.joypad_bits.start = start;

		/* hotkeys (select + * combo)*/
		if(!gb.direct.joypad_bits.select) {
#if ENABLE_SOUND
			if(!gb.direct.joypad_bits.up && prev_joypad_bits.up) {
				/* select + up: increase sound volume */
				i2s_increase_volume(&i2s_config);
			}
			if(!gb.direct.joypad_bits.down && prev_joypad_bits.down) {
				/* select + down: decrease sound volume */
				i2s_decrease_volume(&i2s_config);
			}
#endif
			if(!gb.direct.joypad_bits.right && prev_joypad_bits.right) {
				/* select + right: select the next manual color palette */
				if(manual_palette_selected < (NUMBER_OF_MANUAL_PALETTES - 1)) {
					manual_palette_selected++;
					manual_assign_palette(palette,manual_palette_selected);
					printf("I manual palette = %u\n", manual_palette_selected);
				}
			}
			if(!gb.direct.joypad_bits.left && prev_joypad_bits.left) {
				/* select + left: select the previous manual color palette */
				if(manual_palette_selected>0) {
					manual_palette_selected--;
					manual_assign_palette(palette,manual_palette_selected);
					printf("I manual palette = %u\n", manual_palette_selected);
				}
			}
			if(!gb.direct.joypad_bits.start && prev_joypad_bits.start) {
				/* select + start: save ram and resets to the game selection menu */
#if ENABLE_SDCARD				
				if(sd_ready) {
					write_cart_ram_file(&gb);
				}
#endif				
				goto out;
			}
			if(!gb.direct.joypad_bits.a && prev_joypad_bits.a) {
				/* select + A: enable/disable frame-skip => fast-forward */
				gb.direct.frame_skip=!gb.direct.frame_skip;
				printf("I gb.direct.frame_skip = %d\n",gb.direct.frame_skip);
			}
		}

		uint64_t frame_end_time = time_us_64();
		uint64_t frame_us = frame_end_time - frame_start_time;
		uint64_t elapsed_us = frame_end_time - fps_report_time;
		if(elapsed_us >= 1000000ULL) {
			uint64_t fps_x100 = ((uint64_t)fps_frames * 100000000ULL) / elapsed_us;
			printf("FPS: %llu.%02llu | frames=%lu elapsed_ms=%llu | last_frame_ms=%llu | frameskip=%u interlace=%u\n",
			       (unsigned long long)(fps_x100 / 100ULL),
			       (unsigned long long)(fps_x100 % 100ULL),
			       (unsigned long)fps_frames,
			       (unsigned long long)(elapsed_us / 1000ULL),
			       (unsigned long long)(frame_us / 1000ULL),
			       (unsigned)gb.direct.frame_skip,
			       (unsigned)gb.direct.interlace);
			stdio_flush();
			fps_frames = 0;
			fps_report_time = frame_end_time;
		}

		/* Serial monitor commands */ 
		input = getchar_timeout_us(0);
		if(input == PICO_ERROR_TIMEOUT)
			continue;

		switch(input)
		{
#if 0
		static bool invert = false;
		static bool sleep = false;
		static uint8_t freq = 1;
		static ili9225_color_mode_e colour = ILI9225_COLOR_MODE_FULL;

		case 'i':
			invert = !invert;
			mk_ili9225_display_control(invert, colour);
			break;

		case 'f':
			freq++;
			freq &= 0x0F;
			mk_ili9225_set_drive_freq(freq);
			printf("Freq %u\n", freq);
			break;
#endif
		case 'c':
		{
			static ili9225_color_mode_e mode = ILI9225_COLOR_MODE_FULL;
			union core_cmd cmd;

			mode = !mode;
			cmd.cmd = CORE_CMD_IDLE_SET;
			cmd.data = mode;
			multicore_fifo_push_blocking(cmd.full);
			break;
		}

		case 'i':
			gb.direct.interlace = !gb.direct.interlace;
			break;

		case 'f':
			gb.direct.frame_skip = !gb.direct.frame_skip;
			break;

		case 'b':
		{
			uint64_t end_time;
			uint32_t diff;
			uint32_t fps;

			end_time = time_us_64();
			diff = end_time-start_time;
			fps = ((uint64_t)frames*1000*1000)/diff;
			printf("Frames: %u\n"
				"Time: %lu us\n"
				"FPS: %lu\n",
				frames, diff, fps);
			stdio_flush();
			frames = 0;
			start_time = time_us_64();
			break;
		}

		case 'n':
		case ']':
		{
			/* Serial: next manual color palette (same as select + right). */
			if(manual_palette_selected < (NUMBER_OF_MANUAL_PALETTES - 1)) {
				manual_palette_selected++;
				manual_assign_palette(palette, manual_palette_selected);
			}
			printf("I manual palette = %u\n", manual_palette_selected);
			break;
		}

		case 'p':
		case '[':
		{
			/* Serial: previous manual color palette (same as select + left). */
			if(manual_palette_selected > 0) {
				manual_palette_selected--;
				manual_assign_palette(palette, manual_palette_selected);
			}
			printf("I manual palette = %u\n", manual_palette_selected);
			break;
		}

		case 'g':
		case 'G':
		{
			/* Serial: force DMG-green manual palette. */
			manual_palette_selected = NUMBER_OF_MANUAL_PALETTES - 1;
			manual_assign_palette(palette, manual_palette_selected);
			printf("I manual palette = %u (DMG green)\n", manual_palette_selected);
			break;
		}

		case 'a':
		case 'A':
		{
			/* Serial: restore automatic per-game palette. */
			auto_assign_palette(palette, gb_colour_hash(&gb), gb_get_rom_name(&gb, rom_title));
			printf("I auto palette restored for current ROM\n");
			break;
		}

		case '?':
		case 'h':
		case 'H':
		{
			printf("Serial keys: n/] next palette, p/[ prev palette, g DMG green, a auto palette\n");
			break;
		}

		case '\n':
		case '\r':
		{
			gb.direct.joypad_bits.start = 0;
			break;
		}

		case '\b':
		{
			gb.direct.joypad_bits.select = 0;
			break;
		}

		case '8':
		{
			gb.direct.joypad_bits.up = 0;
			break;
		}

		case '2':
		{
			gb.direct.joypad_bits.down = 0;
			break;
		}

		case '4':
		{
			gb.direct.joypad_bits.left= 0;
			break;
		}

		case '6':
		{
			gb.direct.joypad_bits.right = 0;
			break;
		}

		case 'z':
		case 'w':
		{
			gb.direct.joypad_bits.a = 0;
			break;
		}

		case 'x':
		{
			gb.direct.joypad_bits.b = 0;
			break;
		}

		case 'q':
			goto out;

		default:
			break;
		}
	}
out:
	puts("\nEmulation Ended");
	/* stop lcd task running on core 1 */
	multicore_reset_core1(); 
	__atomic_store_n(&lcd_core_running, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&lcd_core_ready, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&loading_anim_active, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&loading_anim_done, false, __ATOMIC_SEQ_CST);
	__atomic_store_n(&loading_anim_rom_done, false, __ATOMIC_SEQ_CST);

}

}
