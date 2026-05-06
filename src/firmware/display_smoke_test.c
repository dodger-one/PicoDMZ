#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

/* GPIO pinout from README.md / main firmware. */
#define GPIO_CS   17
#define GPIO_SCK  18
#define GPIO_MOSI 19
#define GPIO_DC   20
#define GPIO_RST  21
#define GPIO_BLK  22

/* ST7789: logical space after 90 deg CCW rotation. */
#define ST7789_WIDTH    320
#define ST7789_HEIGHT   240
#define ST7789_XSTART   0
#define ST7789_YSTART   0

/* ST7789 commands. */
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_COLMOD   0x3A
#define ST7789_MADCTL   0x36
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_DISPON   0x29
#define ST7789_NORON    0x13
#define ST7789_INVON    0x21
#define ST7789_INVOFF   0x20

#ifndef ST7789_MADCTL_VALUE
#define ST7789_MADCTL_VALUE 0xA0u
#endif

#ifndef ST7789_COLMOD_VALUE
#define ST7789_COLMOD_VALUE 0x55u
#endif

#ifndef ST7789_INIT_INVERT
#define ST7789_INIT_INVERT 0
#endif

#ifndef ST7789_SWAP_RGB565_BYTES
#define ST7789_SWAP_RGB565_BYTES 0
#endif

#ifndef ST7789_RGB_ORDER
#define ST7789_RGB_ORDER 0
#endif

#ifndef ST7789_INVERT_RGB565
#define ST7789_INVERT_RGB565 0
#endif

#define ST7789_RGB_ORDER_RGB 0u
#define ST7789_RGB_ORDER_RBG 1u
#define ST7789_RGB_ORDER_GRB 2u
#define ST7789_RGB_ORDER_GBR 3u
#define ST7789_RGB_ORDER_BRG 4u
#define ST7789_RGB_ORDER_BGR 5u

typedef struct {
    uint8_t madctl;
    uint8_t colmod;
    bool init_invert;
    bool swap_rgb565_bytes;
    uint8_t rgb_order;
    bool invert_rgb565;
} st7789_combo_t;

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

#define COMBO_MADCTL_COUNT 2u
#define COMBO_COLMOD_COUNT 2u
#define COMBO_INVERT_COUNT 2u
#define COMBO_SWAP_COUNT 2u
#define COMBO_ORDER_COUNT 6u
#define COMBO_INV565_COUNT 2u
#define MAX_COMBOS (COMBO_MADCTL_COUNT * COMBO_COLMOD_COUNT * COMBO_INVERT_COUNT * COMBO_SWAP_COUNT * COMBO_ORDER_COUNT * COMBO_INV565_COUNT)

static st7789_combo_t g_combos[MAX_COMBOS];
static size_t g_combo_count;
static size_t g_combo_index;
static bool g_auto_cycle = true;

static void dbg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
    stdio_flush();
}

static const char *rgb_order_name(uint8_t order)
{
    switch (order) {
    case ST7789_RGB_ORDER_RGB:
        return "RGB";
    case ST7789_RGB_ORDER_RBG:
        return "RBG";
    case ST7789_RGB_ORDER_GRB:
        return "GRB";
    case ST7789_RGB_ORDER_GBR:
        return "GBR";
    case ST7789_RGB_ORDER_BRG:
        return "BRG";
    case ST7789_RGB_ORDER_BGR:
        return "BGR";
    default:
        return "?";
    }
}

static inline uint16_t remap_rgb565(uint16_t c, const st7789_combo_t *cfg)
{
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(c & 0x1Fu);
    uint8_t out_r5 = r5;
    uint8_t out_g6 = g6;
    uint8_t out_b5 = b5;

    switch (cfg->rgb_order) {
    case ST7789_RGB_ORDER_RGB:
        out_r5 = r5;
        out_g6 = g6;
        out_b5 = b5;
        break;
    case ST7789_RGB_ORDER_RBG:
        out_r5 = r5;
        out_g6 = (uint8_t)((b5 << 1) | (b5 >> 4));
        out_b5 = (uint8_t)(g6 >> 1);
        break;
    case ST7789_RGB_ORDER_GRB:
        out_r5 = (uint8_t)(g6 >> 1);
        out_g6 = (uint8_t)((r5 << 1) | (r5 >> 4));
        out_b5 = b5;
        break;
    case ST7789_RGB_ORDER_GBR:
        out_r5 = (uint8_t)(g6 >> 1);
        out_g6 = (uint8_t)((b5 << 1) | (b5 >> 4));
        out_b5 = r5;
        break;
    case ST7789_RGB_ORDER_BRG:
        out_r5 = b5;
        out_g6 = (uint8_t)((r5 << 1) | (r5 >> 4));
        out_b5 = (uint8_t)(g6 >> 1);
        break;
    case ST7789_RGB_ORDER_BGR:
        out_r5 = b5;
        out_g6 = g6;
        out_b5 = r5;
        break;
    default:
        break;
    }

    if (cfg->invert_rgb565) {
        out_r5 = (uint8_t)(31u - out_r5);
        out_g6 = (uint8_t)(63u - out_g6);
        out_b5 = (uint8_t)(31u - out_b5);
    }

    return (uint16_t)(((uint16_t)out_r5 << 11) |
                      ((uint16_t)out_g6 << 5) |
                      (uint16_t)out_b5);
}

static inline void pack_rgb565(uint16_t c, uint8_t *dst, const st7789_combo_t *cfg)
{
    c = remap_rgb565(c, cfg);
    if (cfg->swap_rgb565_bytes) {
        dst[0] = (uint8_t)(c & 0xFFu);
        dst[1] = (uint8_t)(c >> 8);
    } else {
        dst[0] = (uint8_t)(c >> 8);
        dst[1] = (uint8_t)(c & 0xFFu);
    }
}

static void st7789_select(bool selected)
{
    gpio_put(GPIO_CS, !selected);
}

static void st7789_dc(bool data_mode)
{
    gpio_put(GPIO_DC, data_mode);
}

static void st7789_write_command(uint8_t cmd)
{
    st7789_select(true);
    st7789_dc(false);
    spi_write_blocking(spi0, &cmd, 1);
    st7789_select(false);
}

static void st7789_write_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    st7789_select(true);
    st7789_dc(true);
    spi_write_blocking(spi0, data, len);
    st7789_select(false);
}

static void st7789_write_command_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7789_write_command(cmd);
    st7789_write_data(data, len);
}

static void st7789_hw_reset(void)
{
    gpio_put(GPIO_RST, true);
    sleep_ms(5);
    gpio_put(GPIO_RST, false);
    sleep_ms(25);
    gpio_put(GPIO_RST, true);
    sleep_ms(120);
}

static void st7789_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4];
    uint8_t raset[4];

    x0 += ST7789_XSTART;
    x1 += ST7789_XSTART;
    y0 += ST7789_YSTART;
    y1 += ST7789_YSTART;

    caset[0] = (uint8_t)(x0 >> 8);
    caset[1] = (uint8_t)(x0 & 0xFF);
    caset[2] = (uint8_t)(x1 >> 8);
    caset[3] = (uint8_t)(x1 & 0xFF);

    raset[0] = (uint8_t)(y0 >> 8);
    raset[1] = (uint8_t)(y0 & 0xFF);
    raset[2] = (uint8_t)(y1 >> 8);
    raset[3] = (uint8_t)(y1 & 0xFF);

    st7789_write_command_data(ST7789_CASET, caset, sizeof(caset));
    st7789_write_command_data(ST7789_RASET, raset, sizeof(raset));
    st7789_write_command(ST7789_RAMWR);
}

static void st7789_init_sequence(const st7789_combo_t *cfg)
{
    const uint8_t colmod[] = {cfg->colmod};
    const uint8_t madctl[] = {cfg->madctl};

    st7789_write_command(ST7789_SWRESET);
    sleep_ms(150);

    st7789_write_command(ST7789_SLPOUT);
    sleep_ms(120);

    st7789_write_command(cfg->init_invert ? ST7789_INVON : ST7789_INVOFF);
    st7789_write_command_data(ST7789_COLMOD, colmod, sizeof(colmod));
    st7789_write_command_data(ST7789_MADCTL, madctl, sizeof(madctl));

    st7789_set_addr_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);

    st7789_write_command(ST7789_NORON);
    sleep_ms(10);
    st7789_write_command(ST7789_DISPON);
    sleep_ms(120);
}

static void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color,
                             const st7789_combo_t *cfg)
{
    static uint8_t color_block[128];
    uint16_t x1;
    uint16_t y1;
    uint32_t pixels;
    size_t i;

    if (w == 0 || h == 0) {
        return;
    }

    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) {
        return;
    }

    x1 = (uint16_t)(x + w - 1u);
    y1 = (uint16_t)(y + h - 1u);

    if (x1 >= ST7789_WIDTH) {
        x1 = ST7789_WIDTH - 1u;
    }
    if (y1 >= ST7789_HEIGHT) {
        y1 = ST7789_HEIGHT - 1u;
    }

    pack_rgb565(color, &color_block[0], cfg);
    for (i = 2; i < sizeof(color_block); i += 2) {
        color_block[i] = color_block[0];
        color_block[i + 1] = color_block[1];
    }

    st7789_set_addr_window(x, y, x1, y1);
    st7789_select(true);
    st7789_dc(true);

    pixels = ((uint32_t)x1 - x + 1u) * ((uint32_t)y1 - y + 1u);
    while (pixels != 0u) {
        uint32_t chunk_pixels = (uint32_t)(sizeof(color_block) / 2u);
        if (chunk_pixels > pixels) {
            chunk_pixels = pixels;
        }
        spi_write_blocking(spi0, color_block, chunk_pixels * 2u);
        pixels -= chunk_pixels;
    }

    st7789_select(false);
}

static void st7789_fill_screen(uint16_t color, const st7789_combo_t *cfg)
{
    st7789_fill_rect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color, cfg);
}

static void draw_digit_7seg(uint16_t x, uint16_t y, uint16_t scale,
                            uint8_t digit, uint16_t color,
                            const st7789_combo_t *cfg)
{
    static const uint8_t masks[16] = {
        0x3Fu, 0x06u, 0x5Bu, 0x4Fu,
        0x66u, 0x6Du, 0x7Du, 0x07u,
        0x7Fu, 0x6Fu, 0x77u, 0x7Cu,
        0x39u, 0x5Eu, 0x79u, 0x71u
    };
    uint8_t m = masks[digit & 0x0Fu];
    uint16_t t = scale;
    uint16_t l = (uint16_t)(4u * scale);

    if (m & 0x01u) {
        st7789_fill_rect((uint16_t)(x + t), y, l, t, color, cfg);
    }
    if (m & 0x02u) {
        st7789_fill_rect((uint16_t)(x + t + l), (uint16_t)(y + t), t, l, color, cfg);
    }
    if (m & 0x04u) {
        st7789_fill_rect((uint16_t)(x + t + l), (uint16_t)(y + (2u * t) + l), t, l, color, cfg);
    }
    if (m & 0x08u) {
        st7789_fill_rect((uint16_t)(x + t), (uint16_t)(y + (2u * (t + l))), l, t, color, cfg);
    }
    if (m & 0x10u) {
        st7789_fill_rect(x, (uint16_t)(y + (2u * t) + l), t, l, color, cfg);
    }
    if (m & 0x20u) {
        st7789_fill_rect(x, (uint16_t)(y + t), t, l, color, cfg);
    }
    if (m & 0x40u) {
        st7789_fill_rect((uint16_t)(x + t), (uint16_t)(y + t + l), l, t, color, cfg);
    }
}

static void draw_num3(uint16_t x, uint16_t y, uint16_t scale,
                      uint16_t value, uint16_t color,
                      const st7789_combo_t *cfg)
{
    uint16_t step = (uint16_t)(8u * scale);
    uint16_t v = (uint16_t)(value % 1000u);
    uint8_t d0 = (uint8_t)((v / 100u) % 10u);
    uint8_t d1 = (uint8_t)((v / 10u) % 10u);
    uint8_t d2 = (uint8_t)(v % 10u);

    draw_digit_7seg(x, y, scale, d0, color, cfg);
    draw_digit_7seg((uint16_t)(x + step), y, scale, d1, color, cfg);
    draw_digit_7seg((uint16_t)(x + (2u * step)), y, scale, d2, color, cfg);
}

static void draw_hud(const st7789_combo_t *cfg, size_t index, size_t total)
{
    uint16_t idx = (uint16_t)(index + 1u);
    uint16_t tot = (uint16_t)total;

    st7789_fill_rect(0, 0, ST7789_WIDTH, 46, 0x0000, cfg);
    draw_num3(8, 6, 2, idx, 0x07E0, cfg);
    st7789_fill_rect(86, 6, 4, 26, 0xFFFF, cfg);
    draw_num3(100, 10, 1, tot, 0xFFFF, cfg);

    /* Visual flags (left->right): MADCTL_BGR, COLMOD_05, INVON, SWAP, INV565 */
    st7789_fill_rect(168, 8, 16, 16, ((cfg->madctl & 0x08u) != 0u) ? 0xF800 : 0x39E7, cfg);
    st7789_fill_rect(188, 8, 16, 16, (cfg->colmod == 0x05u) ? 0xFFE0 : 0x39E7, cfg);
    st7789_fill_rect(208, 8, 16, 16, cfg->init_invert ? 0xF81F : 0x39E7, cfg);
    st7789_fill_rect(228, 8, 16, 16, cfg->swap_rgb565_bytes ? 0x07FF : 0x39E7, cfg);
    st7789_fill_rect(248, 8, 16, 16, cfg->invert_rgb565 ? 0xFFFF : 0x39E7, cfg);

    /* RGB order index 0..5 as six small indicators. */
    for (uint8_t i = 0; i < 6; ++i) {
        uint16_t c = (i == cfg->rgb_order) ? 0x07E0 : 0x2104;
        st7789_fill_rect((uint16_t)(168 + (i * 18u)), 30, 14, 10, c, cfg);
    }
}

static void draw_test_pattern(const st7789_combo_t *cfg, size_t index, size_t total)
{
    static const uint16_t dmg_green[4] = {
        0xE7DA, 0x8E2E, 0x4308, 0x19C3
    };

    st7789_fill_screen(0x0000, cfg);
    draw_hud(cfg, index, total);

    /* DMG-like pseudo green grayscale swatches. */
    for (uint8_t i = 0; i < 4; ++i) {
        st7789_fill_rect((uint16_t)(16 + i * 76u), 58, 68, 64, dmg_green[i], cfg);
    }

    /* Neutral grayscale reference swatches. */
    st7789_fill_rect(16, 130, 68, 40, 0xFFFF, cfg);
    st7789_fill_rect(92, 130, 68, 40, 0xBDF7, cfg);
    st7789_fill_rect(168, 130, 68, 40, 0x7BEF, cfg);
    st7789_fill_rect(244, 130, 68, 40, 0x0000, cfg);

    /* RGB reference bars. */
    st7789_fill_rect(16, 178, 96, 44, 0xF800, cfg);
    st7789_fill_rect(112, 178, 96, 44, 0x07E0, cfg);
    st7789_fill_rect(208, 178, 96, 44, 0x001F, cfg);
}

static void print_combo(const st7789_combo_t *cfg, size_t index, size_t total)
{
    dbg("Combo %03u/%03u | MADCTL=0x%02X COLMOD=0x%02X INV=%u SWAP=%u ORDER=%s INV565=%u",
        (unsigned)(index + 1u),
        (unsigned)total,
        (unsigned)cfg->madctl,
        (unsigned)cfg->colmod,
        (unsigned)cfg->init_invert,
        (unsigned)cfg->swap_rgb565_bytes,
        rgb_order_name(cfg->rgb_order),
        (unsigned)cfg->invert_rgb565);
}

static void apply_combo(size_t index)
{
    const st7789_combo_t *cfg = &g_combos[index];

    st7789_hw_reset();
    st7789_init_sequence(cfg);
    draw_test_pattern(cfg, index, g_combo_count);
    print_combo(cfg, index, g_combo_count);
}

static void build_combo_table(void)
{
    static const uint8_t madctl_values[COMBO_MADCTL_COUNT] = {0xA0u, 0xA8u};
    static const uint8_t colmod_values[COMBO_COLMOD_COUNT] = {0x55u, 0x05u};
    size_t idx = 0;

    for (size_t m = 0; m < COMBO_MADCTL_COUNT; ++m) {
        for (size_t c = 0; c < COMBO_COLMOD_COUNT; ++c) {
            for (size_t inv = 0; inv < COMBO_INVERT_COUNT; ++inv) {
                for (size_t sw = 0; sw < COMBO_SWAP_COUNT; ++sw) {
                    for (size_t ord = 0; ord < COMBO_ORDER_COUNT; ++ord) {
                        for (size_t inv565 = 0; inv565 < COMBO_INV565_COUNT; ++inv565) {
                            g_combos[idx].madctl = madctl_values[m];
                            g_combos[idx].colmod = colmod_values[c];
                            g_combos[idx].init_invert = (inv != 0u);
                            g_combos[idx].swap_rgb565_bytes = (sw != 0u);
                            g_combos[idx].rgb_order = (uint8_t)ord;
                            g_combos[idx].invert_rgb565 = (inv565 != 0u);
                            idx++;
                        }
                    }
                }
            }
        }
    }

    g_combo_count = idx;
}

static size_t find_default_combo_index(void)
{
    for (size_t i = 0; i < g_combo_count; ++i) {
        const st7789_combo_t *cfg = &g_combos[i];
        if (cfg->madctl == (uint8_t)ST7789_MADCTL_VALUE &&
            cfg->colmod == (uint8_t)ST7789_COLMOD_VALUE &&
            cfg->init_invert == (ST7789_INIT_INVERT != 0) &&
            cfg->swap_rgb565_bytes == (ST7789_SWAP_RGB565_BYTES != 0) &&
            cfg->rgb_order == (uint8_t)ST7789_RGB_ORDER &&
            cfg->invert_rgb565 == (ST7789_INVERT_RGB565 != 0)) {
            return i;
        }
    }

    return 0u;
}

static void setup_display_gpio_and_spi(void)
{
    gpio_set_function(GPIO_CS, GPIO_FUNC_SIO);
    gpio_set_function(GPIO_DC, GPIO_FUNC_SIO);
    gpio_set_function(GPIO_RST, GPIO_FUNC_SIO);
    gpio_set_function(GPIO_BLK, GPIO_FUNC_SIO);

    gpio_set_dir(GPIO_CS, true);
    gpio_set_dir(GPIO_DC, true);
    gpio_set_dir(GPIO_RST, true);
    gpio_set_dir(GPIO_BLK, true);

    gpio_set_function(GPIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_MOSI, GPIO_FUNC_SPI);
    gpio_set_slew_rate(GPIO_SCK, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(GPIO_MOSI, GPIO_SLEW_RATE_FAST);

    clock_configure(
        clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        125 * 1000 * 1000,
        125 * 1000 * 1000
    );
    spi_init(spi0, 40 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    st7789_select(false);
    st7789_dc(true);
}

static void print_help(void)
{
    dbg("Keys: c=next combo | p=previous combo | r=toggle auto-cycle | h/?=help");
    dbg("Auto-cycle changes combo every 2500 ms");
}

int main(void)
{
    absolute_time_t next_auto_change;

    stdio_init_all();
    console_startup_sync();

    dbg("ST7789 smoke test: interactive color-combo cycler");
    dbg("Pins: CS=%d SCK=%d MOSI=%d DC=%d RST=%d BLK=%d", GPIO_CS, GPIO_SCK, GPIO_MOSI, GPIO_DC, GPIO_RST, GPIO_BLK);

    setup_display_gpio_and_spi();

    gpio_put(GPIO_BLK, false);
    sleep_ms(200);
    gpio_put(GPIO_BLK, true);

    build_combo_table();
    g_combo_index = find_default_combo_index();

    dbg("Generated %u color combinations", (unsigned)g_combo_count);
    print_help();

    apply_combo(g_combo_index);
    next_auto_change = make_timeout_time_ms(2500);

    while (true) {
        int input = getchar_timeout_us(0);
        bool changed = false;

        if (input != PICO_ERROR_TIMEOUT) {
            switch (input) {
            case 'c':
            case 'C':
                g_combo_index = (g_combo_index + 1u) % g_combo_count;
                changed = true;
                break;

            case 'p':
            case 'P':
                g_combo_index = (g_combo_index == 0u) ? (g_combo_count - 1u) : (g_combo_index - 1u);
                changed = true;
                break;

            case 'r':
            case 'R':
                g_auto_cycle = !g_auto_cycle;
                dbg("Auto-cycle: %s", g_auto_cycle ? "ON" : "OFF");
                next_auto_change = make_timeout_time_ms(2500);
                break;

            case 'h':
            case 'H':
            case '?':
                print_help();
                break;

            default:
                break;
            }
        }

        if (g_auto_cycle && time_reached(next_auto_change)) {
            g_combo_index = (g_combo_index + 1u) % g_combo_count;
            changed = true;
            next_auto_change = make_timeout_time_ms(2500);
        }

        if (changed) {
            apply_combo(g_combo_index);
        }

        sleep_ms(10);
    }
}
