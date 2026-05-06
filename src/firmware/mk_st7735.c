/**
 * ST7735 compatibility layer that exposes the mk_ili9225_* API used by
 * main.c. This allows the emulator core to run on 1.8" ST7735 SPI panels.
 */

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "mk_ili9225.h"

/* GPIO mapping kept identical to README/main.c for LCD pins. */
#define ST7735_GPIO_CS    17u
#define ST7735_GPIO_DC    20u
#define ST7735_GPIO_RST   21u
#define ST7735_GPIO_LED   22u

/* Game Boy source frame dimensions. */
#define GB_LCD_WIDTH      160u
#define GB_LCD_HEIGHT     144u

/* Rendered ST7735 area (landscape). */
#define ST7735_WIDTH      160u
#define ST7735_HEIGHT     128u

/* Panel-specific offsets (tweak if the image appears shifted). */
#define ST7735_X_OFFSET   0u
#define ST7735_Y_OFFSET   0u

#ifndef ST7735_SCALE_GB_TO_PANEL
#define ST7735_SCALE_GB_TO_PANEL 1
#endif

/* ST7735 commands. */
#define ST7735_SWRESET    0x01
#define ST7735_SLPIN      0x10
#define ST7735_SLPOUT     0x11
#define ST7735_NORON      0x13
#define ST7735_INVOFF     0x20
#define ST7735_INVON      0x21
#define ST7735_DISPOFF    0x28
#define ST7735_DISPON     0x29
#define ST7735_CASET      0x2A
#define ST7735_RASET      0x2B
#define ST7735_RAMWR      0x2C
#define ST7735_MADCTL     0x36
#define ST7735_COLMOD     0x3A
#define ST7735_FRMCTR1    0xB1
#define ST7735_FRMCTR2    0xB2
#define ST7735_FRMCTR3    0xB3
#define ST7735_INVCTR     0xB4
#define ST7735_PWCTR1     0xC0
#define ST7735_PWCTR2     0xC1
#define ST7735_PWCTR3     0xC2
#define ST7735_PWCTR4     0xC3
#define ST7735_PWCTR5     0xC4
#define ST7735_VMCTR1     0xC5

static int st7735_current_y = -1;

static void st7735_select(bool selected)
{
    gpio_put(ST7735_GPIO_CS, !selected);
}

static void st7735_dc(bool data_mode)
{
    gpio_put(ST7735_GPIO_DC, data_mode);
}

static void st7735_write_command(uint8_t cmd)
{
    st7735_select(true);
    st7735_dc(false);
    spi_write_blocking(spi0, &cmd, 1);
    st7735_select(false);
}

static void st7735_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    st7735_select(true);
    st7735_dc(true);
    spi_write_blocking(spi0, data, len);
    st7735_select(false);
}

static void st7735_write_command_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7735_write_command(cmd);
    st7735_write_data(data, len);
}

static void st7735_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4];
    uint8_t raset[4];

    x0 += ST7735_X_OFFSET;
    x1 += ST7735_X_OFFSET;
    y0 += ST7735_Y_OFFSET;
    y1 += ST7735_Y_OFFSET;

    caset[0] = (uint8_t)(x0 >> 8);
    caset[1] = (uint8_t)(x0 & 0xFF);
    caset[2] = (uint8_t)(x1 >> 8);
    caset[3] = (uint8_t)(x1 & 0xFF);

    raset[0] = (uint8_t)(y0 >> 8);
    raset[1] = (uint8_t)(y0 & 0xFF);
    raset[2] = (uint8_t)(y1 >> 8);
    raset[3] = (uint8_t)(y1 & 0xFF);

    st7735_write_command_data(ST7735_CASET, caset, sizeof(caset));
    st7735_write_command_data(ST7735_RASET, raset, sizeof(raset));
    st7735_write_command(ST7735_RAMWR);
}

static void st7735_hw_reset(void)
{
    gpio_put(ST7735_GPIO_RST, true);
    sleep_ms(5);
    gpio_put(ST7735_GPIO_RST, false);
    sleep_ms(25);
    gpio_put(ST7735_GPIO_RST, true);
    sleep_ms(120);
}

static void st7735_init_sequence(void)
{
    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    const uint8_t invctr[] = {0x07};
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    const uint8_t pwctr2[] = {0xC5};
    const uint8_t pwctr3[] = {0x0A, 0x00};
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    const uint8_t vmctr1[] = {0x0E};
    const uint8_t colmod[] = {0x05};
    const uint8_t madctl[] = {0xA8}; /* MY | MV | BGR => 160x128 landscape */

    st7735_write_command(ST7735_SWRESET);
    sleep_ms(150);

    st7735_write_command(ST7735_SLPOUT);
    sleep_ms(255);

    st7735_write_command_data(ST7735_FRMCTR1, frmctr1, sizeof(frmctr1));
    st7735_write_command_data(ST7735_FRMCTR2, frmctr2, sizeof(frmctr2));
    st7735_write_command_data(ST7735_FRMCTR3, frmctr3, sizeof(frmctr3));
    st7735_write_command_data(ST7735_INVCTR, invctr, sizeof(invctr));
    st7735_write_command_data(ST7735_PWCTR1, pwctr1, sizeof(pwctr1));
    st7735_write_command_data(ST7735_PWCTR2, pwctr2, sizeof(pwctr2));
    st7735_write_command_data(ST7735_PWCTR3, pwctr3, sizeof(pwctr3));
    st7735_write_command_data(ST7735_PWCTR4, pwctr4, sizeof(pwctr4));
    st7735_write_command_data(ST7735_PWCTR5, pwctr5, sizeof(pwctr5));
    st7735_write_command_data(ST7735_VMCTR1, vmctr1, sizeof(vmctr1));

    st7735_write_command(ST7735_INVOFF);
    st7735_write_command_data(ST7735_COLMOD, colmod, sizeof(colmod));
    st7735_write_command_data(ST7735_MADCTL, madctl, sizeof(madctl));

    st7735_set_addr_window(0, 0, ST7735_WIDTH - 1, ST7735_HEIGHT - 1);

    st7735_write_command(ST7735_NORON);
    sleep_ms(10);
    st7735_write_command(ST7735_DISPON);
    sleep_ms(120);
}

unsigned mk_ili9225_init(void)
{
    printf("LCD: ST7735 init\n");

    gpio_put(ST7735_GPIO_LED, false);
    st7735_select(false);
    st7735_dc(true);

    st7735_hw_reset();
    st7735_init_sequence();

    mk_ili9225_fill(0x0000);
    gpio_put(ST7735_GPIO_LED, true);
    return 0;
}

void mk_ili9225_display_control(bool invert, ili9225_color_mode_e colour_mode)
{
    (void)colour_mode;
    st7735_write_command(invert ? ST7735_INVON : ST7735_INVOFF);
}

void mk_ili9225_invert_display(bool invert)
{
    mk_ili9225_display_control(invert, ILI9225_COLOR_MODE_FULL);
}

void mk_ili9225_set_window(uint16_t hor_start, uint16_t hor_end,
    uint16_t vert_start, uint16_t vert_end)
{
    if (hor_start > hor_end || vert_start > vert_end) {
        return;
    }

    if (hor_start >= ST7735_WIDTH || vert_start >= ST7735_HEIGHT) {
        return;
    }

    if (hor_end >= ST7735_WIDTH) {
        hor_end = ST7735_WIDTH - 1;
    }

    if (vert_end >= ST7735_HEIGHT) {
        vert_end = ST7735_HEIGHT - 1;
    }

    st7735_set_addr_window(hor_start, vert_start, hor_end, vert_end);
}

void mk_ili9225_set_address(uint8_t x, uint8_t y)
{
    if (x >= ST7735_WIDTH || y >= ST7735_HEIGHT) {
        return;
    }

    st7735_set_addr_window(x, y, x, y);
}

void mk_ili9225_set_x(uint8_t x)
{
    int gb_line = (int)x - 16;

    if (gb_line < 0 || gb_line >= (int)GB_LCD_HEIGHT) {
        st7735_current_y = -1;
        return;
    }

#if ST7735_SCALE_GB_TO_PANEL
    st7735_current_y = (gb_line * (int)ST7735_HEIGHT) / (int)GB_LCD_HEIGHT;
#else
    if (gb_line >= (int)ST7735_HEIGHT) {
        st7735_current_y = -1;
        return;
    }
    st7735_current_y = gb_line;
#endif
}

void mk_ili9225_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb)
{
    static uint8_t txbuf[GB_LCD_WIDTH * 2];
    uint_fast16_t i;
    uint_fast16_t count;

    if (pixels == NULL || nmemb == 0 || st7735_current_y < 0) {
        return;
    }

    count = nmemb;
    if (count > ST7735_WIDTH) {
        count = ST7735_WIDTH;
    }

    st7735_set_addr_window(0, (uint16_t)st7735_current_y,
                           (uint16_t)(count - 1), (uint16_t)st7735_current_y);

    for (i = 0; i < count; ++i) {
        txbuf[(size_t)i * 2] = (uint8_t)(pixels[i] >> 8);
        txbuf[(size_t)i * 2 + 1] = (uint8_t)(pixels[i] & 0xFF);
    }

    st7735_write_data(txbuf, (size_t)count * 2);
}

void mk_ili9225_write_pixels_start(void)
{
    /* Not required for the ST7735 compatibility path. */
}

void mk_ili9225_write_pixels_end(void)
{
    /* Not required for the ST7735 compatibility path. */
}

void mk_ili9225_power_control(uint8_t drive_power, bool sleep)
{
    (void)drive_power;
    st7735_write_command(sleep ? ST7735_SLPIN : ST7735_SLPOUT);
}

void mk_ili9225_set_gate_scan(uint16_t hor_start, uint16_t hor_end)
{
    (void)hor_start;
    (void)hor_end;
}

void mk_ili9225_set_drive_freq(uint16_t f)
{
    (void)f;
}

void mk_ili9225_exit(void)
{
    st7735_write_command(ST7735_DISPOFF);
}

void mk_ili9225_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    static uint8_t color_block[128];
    uint16_t x0 = x;
    uint16_t y0 = y;
    uint16_t x1;
    uint16_t y1;
    uint32_t pixels;
    size_t i;

    if (w == 0 || h == 0) {
        return;
    }

    if (x0 >= ST7735_WIDTH || y0 >= ST7735_HEIGHT) {
        return;
    }

    x1 = (uint16_t)(x0 + w - 1);
    y1 = (uint16_t)(y0 + h - 1);

    if (x1 >= ST7735_WIDTH) {
        x1 = ST7735_WIDTH - 1;
    }

    if (y1 >= ST7735_HEIGHT) {
        y1 = ST7735_HEIGHT - 1;
    }

    color_block[0] = (uint8_t)(color >> 8);
    color_block[1] = (uint8_t)(color & 0xFF);
    for (i = 2; i < sizeof(color_block); i += 2) {
        color_block[i] = color_block[0];
        color_block[i + 1] = color_block[1];
    }

    st7735_set_addr_window(x0, y0, x1, y1);

    st7735_select(true);
    st7735_dc(true);
    pixels = ((uint32_t)x1 - x0 + 1u) * ((uint32_t)y1 - y0 + 1u);
    while (pixels != 0u) {
        uint32_t chunk_pixels = (uint32_t)(sizeof(color_block) / 2u);
        if (chunk_pixels > pixels) {
            chunk_pixels = pixels;
        }
        spi_write_blocking(spi0, color_block, chunk_pixels * 2u);
        pixels -= chunk_pixels;
    }
    st7735_select(false);
}

void mk_ili9225_fill(uint16_t color)
{
    mk_ili9225_fill_rect(0, 0, ST7735_WIDTH, ST7735_HEIGHT, color);
}

void mk_ili9225_pixel(uint8_t x, uint8_t y, uint16_t color)
{
    mk_ili9225_fill_rect(x, y, 1, 1, color);
}

void mk_ili9225_blit(uint16_t *fbuf, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    static uint8_t txbuf[8 * 8 * 2];
    uint16_t x0 = x;
    uint16_t y0 = y;
    uint16_t x1;
    uint16_t y1;
    uint32_t pixels;
    uint32_t sent = 0;

    if (fbuf == NULL || w == 0 || h == 0) {
        return;
    }

    if (x0 >= ST7735_WIDTH || y0 >= ST7735_HEIGHT) {
        return;
    }

    x1 = (uint16_t)(x0 + w - 1);
    y1 = (uint16_t)(y0 + h - 1);

    if (x1 >= ST7735_WIDTH) {
        x1 = ST7735_WIDTH - 1;
    }

    if (y1 >= ST7735_HEIGHT) {
        y1 = ST7735_HEIGHT - 1;
    }

    pixels = ((uint32_t)x1 - x0 + 1u) * ((uint32_t)y1 - y0 + 1u);
    st7735_set_addr_window(x0, y0, x1, y1);

    st7735_select(true);
    st7735_dc(true);
    while (sent < pixels) {
        uint32_t chunk_pixels = (uint32_t)(sizeof(txbuf) / 2u);
        uint32_t i;

        if (chunk_pixels > (pixels - sent)) {
            chunk_pixels = pixels - sent;
        }

        for (i = 0; i < chunk_pixels; ++i) {
            uint16_t c = fbuf[sent + i];
            txbuf[i * 2u] = (uint8_t)(c >> 8);
            txbuf[i * 2u + 1u] = (uint8_t)(c & 0xFF);
        }

        spi_write_blocking(spi0, txbuf, chunk_pixels * 2u);
        sent += chunk_pixels;
    }
    st7735_select(false);
}

void mk_ili9225_get_letter(uint16_t *fbuf,char l,uint16_t color,uint16_t bgcolor) {
	uint8_t letter[8];
	uint8_t row;
	
	switch(l)
	{
		case 'a':
		case 'A':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case 'b':
		case 'B':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'c':
		case 'C':
		{
			const uint8_t letter_[8]={0b00011110,
						              0b00110000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00110000,
						              0b00011110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'd':
		case 'D':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01101100,
						              0b01111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'e':
		case 'E':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'f':
		case 'F':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'g':
		case 'G':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100000,
						              0b01101110,
						              0b01100110,
						              0b01100110,
						              0b00111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'h':
		case 'H':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'i':
		case 'I':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'j':
		case 'J':
		{
			const uint8_t letter_[8]={0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'k':
		case 'K':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11001100,
						              0b11011000,
						              0b11110000,
						              0b11011000,
						              0b11001100,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'l':
		case 'L':
		{
			const uint8_t letter_[8]={0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'm':
		case 'M':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11101110,
						              0b11111110,
						              0b11010110,
						              0b11000110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'n':
		case 'N':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11100110,
						              0b11110110,
						              0b11011110,
						              0b11001110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'o':
		case 'O':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'p':
		case 'P':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'q':
		case 'Q':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11011100,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'r':
		case 'R':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 's':
		case 'S':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01110000,
						              0b00111100,
						              0b00001110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 't':
		case 'T':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'u':
		case 'U':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'v':
		case 'V':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00111100,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'w':
		case 'W':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11000110,
						              0b11000110,
						              0b11010110,
						              0b11111110,
						              0b11101110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'x':
		case 'X':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00111100,
						              0b01100110,
						              0b11000011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'y':
		case 'Y':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'z':
		case 'Z':
		{
			const uint8_t letter_[8]={0b11111110,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01100000,
						              0b11000000,
						              0b11111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '-':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b01111110,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '(':
		case '[':
		case '{':
		{
			const uint8_t letter_[8]={0b00001100,
						              0b00011000,
						              0b00110000,
									  0b00110000,
						              0b00110000,
						              0b00011000,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case ')':
		case ']':
		case '}':
		{
			const uint8_t letter_[8]={0b00110000,
						              0b00011000,
						              0b00001100,
									  0b00001100,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case ',':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00110000};
			memcpy(letter,letter_,8);
			break;
		}

		case '.':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '!':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00011000,
									  0b00011000,
						              0b00011000,
						              0b00000000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '&':
		{
			const uint8_t letter_[8]={0b00111000,
						              0b01101100,
						              0b01101000,
									  0b01110110,
						              0b11011100,
						              0b11001110,
						              0b01111011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '\'':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00110000,
									  0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '0':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01101110,
									  0b01111110,
						              0b01110110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '1':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00111000,
						              0b01111000,
									  0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '2':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '3':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00011100,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '4':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00111100,
						              0b01101100,
									  0b11001100,
						              0b11111110,
						              0b00001100,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '5':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01111100,
									  0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '6':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00110000,
						              0b01100000,
									  0b01111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '7':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00000110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '8':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '9':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111110,
						              0b00000110,
						              0b00001100,
						              0b00111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		default:
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}
	}

	for(uint8_t y=0;y<8;y++) {
		row=letter[y];
		for(uint8_t x=0;x<8;x++) {
			if(row & 128) {
				fbuf[y*8+x]=color;
			} else {
				fbuf[y*8+x]=bgcolor;
			}
			row=row<<1;
		}
	}
}

void mk_ili9225_text(char *s,uint8_t x,uint8_t y,uint16_t color,uint16_t bgcolor) {
	uint16_t fbuf[8*8];
	for(uint8_t i=0;i<strlen(s);i++) {
		mk_ili9225_get_letter(fbuf,s[i],color,bgcolor);
		mk_ili9225_blit(fbuf,x,y,8,8);
		x+=8;
		if(x>(ST7735_WIDTH-8)) {
			break;
		}
	}
}
