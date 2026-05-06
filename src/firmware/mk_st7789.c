/**
 * ST7789 compatibility layer that exposes the mk_ili9225_* API used by
 * main.c. This allows the emulator core to run on ST7789 SPI panels.
 */

#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "mk_ili9225.h"

/* GPIO mapping kept identical to README/main.c for LCD pins. */
#define ST7789_GPIO_CS    17u
#define ST7789_GPIO_DC    20u
#define ST7789_GPIO_RST   21u
#define ST7789_GPIO_LED   22u

/* Game Boy source frame dimensions. */
#define GB_LCD_WIDTH      160u
#define GB_LCD_HEIGHT     144u

/* ST7789 2.8" panels are commonly 240x320 physical.
 * We use a 320x240 logical space. Default MADCTL is rotated 180 degrees
 * from the earlier 0xA0 orientation to match the final enclosure view. */
#define ST7789_WIDTH      320u
#define ST7789_HEIGHT     240u

/* Panel-specific offsets (tweak if the image appears shifted). */
#define ST7789_X_OFFSET   0u
#define ST7789_Y_OFFSET   0u

/* Game Boy render area:
 * 160x144 scaled to 266x240 (approx 1.66x wide, 1.67x high), centered. */
#define GB_RENDER_WIDTH   266u
#define GB_RENDER_HEIGHT  240u
#define GB_RENDER_X_SHIFT 18u
#define GB_RENDER_X       (((ST7789_WIDTH - GB_RENDER_WIDTH) / 2u) + GB_RENDER_X_SHIFT)
#define GB_RENDER_Y       ((ST7789_HEIGHT - GB_RENDER_HEIGHT) / 2u)

/* ST7789 commands. */
#define ST7789_SWRESET    0x01
#define ST7789_SLPIN      0x10
#define ST7789_SLPOUT     0x11
#define ST7789_NORON      0x13
#define ST7789_INVOFF     0x20
#define ST7789_INVON      0x21
#define ST7789_DISPOFF    0x28
#define ST7789_DISPON     0x29
#define ST7789_CASET      0x2A
#define ST7789_RASET      0x2B
#define ST7789_RAMWR      0x2C
#define ST7789_MADCTL     0x36
#define ST7789_COLMOD     0x3A

#ifndef ST7789_MADCTL_VALUE
#define ST7789_MADCTL_VALUE 0x60u
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

#define ST7789_RGB_ORDER_RGB 0
#define ST7789_RGB_ORDER_RBG 1
#define ST7789_RGB_ORDER_GRB 2
#define ST7789_RGB_ORDER_GBR 3
#define ST7789_RGB_ORDER_BRG 4
#define ST7789_RGB_ORDER_BGR 5

#define ST7789_MAX_LINE_REPEAT 2u
#define ST7789_DMA_BAND_LINES 8u
#define ST7789_DMA_QUEUE_DEPTH 4u

typedef enum {
    ST7789_SLOT_FREE = 0,
    ST7789_SLOT_RESERVED,
    ST7789_SLOT_READY,
    ST7789_SLOT_ACTIVE
} st7789_slot_state_t;

typedef struct {
    volatile st7789_slot_state_t state;
    uint16_t y0;
    uint16_t y1;
    size_t len;
    uint8_t payload[GB_RENDER_WIDTH * 2u * ST7789_DMA_BAND_LINES];
} st7789_dma_slot_t;

static int st7789_current_y = -1;
static uint8_t st7789_line_scaled[GB_RENDER_WIDTH * 2u];
static uint8_t st7789_band_buffer[GB_RENDER_WIDTH * 2u * ST7789_DMA_BAND_LINES];
static uint16_t st7789_band_y0 = 0u;
static uint16_t st7789_band_y1 = 0u;
static uint16_t st7789_band_rows = 0u;
static size_t st7789_band_len = 0u;
static int st7789_dma_channel = -1;
static volatile bool st7789_dma_initialized = false;
static volatile bool st7789_dma_active = false;
static volatile uint8_t st7789_dma_head = 0u;
static volatile uint8_t st7789_dma_tail = 0u;
static st7789_dma_slot_t st7789_dma_slots[ST7789_DMA_QUEUE_DEPTH];

static bool st7789_dma_queue_busy(void);
static void st7789_wait_for_idle(void);
static void st7789_write_command_raw(uint8_t cmd);
static void st7789_write_data_raw(const uint8_t *data, size_t len);
static void st7789_write_command_data_raw(uint8_t cmd, const uint8_t *data, size_t len);
static void st7789_set_addr_window_raw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
static void st7789_flush_pending_band(void);

static const char *st7789_rgb_order_name(void)
{
    switch (ST7789_RGB_ORDER) {
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
        return "RGB?";
    }
}

static inline uint16_t st7789_remap_rgb565(uint16_t c)
{
#if (ST7789_RGB_ORDER == ST7789_RGB_ORDER_RGB) && !ST7789_INVERT_RGB565
    return c;
#else
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(c & 0x1Fu);
    uint8_t out_r5 = r5;
    uint8_t out_g6 = g6;
    uint8_t out_b5 = b5;

#if ST7789_RGB_ORDER == ST7789_RGB_ORDER_RGB
    out_r5 = r5;
    out_g6 = g6;
    out_b5 = b5;
#elif ST7789_RGB_ORDER == ST7789_RGB_ORDER_RBG
    out_r5 = r5;
    out_g6 = (uint8_t)((b5 << 1) | (b5 >> 4));
    out_b5 = (uint8_t)(g6 >> 1);
#elif ST7789_RGB_ORDER == ST7789_RGB_ORDER_GRB
    out_r5 = (uint8_t)(g6 >> 1);
    out_g6 = (uint8_t)((r5 << 1) | (r5 >> 4));
    out_b5 = b5;
#elif ST7789_RGB_ORDER == ST7789_RGB_ORDER_GBR
    out_r5 = (uint8_t)(g6 >> 1);
    out_g6 = (uint8_t)((b5 << 1) | (b5 >> 4));
    out_b5 = r5;
#elif ST7789_RGB_ORDER == ST7789_RGB_ORDER_BRG
    out_r5 = b5;
    out_g6 = (uint8_t)((r5 << 1) | (r5 >> 4));
    out_b5 = (uint8_t)(g6 >> 1);
#elif ST7789_RGB_ORDER == ST7789_RGB_ORDER_BGR
    out_r5 = b5;
    out_g6 = g6;
    out_b5 = r5;
#else
#error Unsupported ST7789_RGB_ORDER value
#endif

#if ST7789_INVERT_RGB565
    out_r5 = (uint8_t)(31u - out_r5);
    out_g6 = (uint8_t)(63u - out_g6);
    out_b5 = (uint8_t)(31u - out_b5);
#endif

    return (uint16_t)(((uint16_t)out_r5 << 11) |
                      ((uint16_t)out_g6 << 5) |
                      (uint16_t)out_b5);
#endif
}

static inline void st7789_pack_rgb565(uint16_t c, uint8_t *dst)
{
    c = st7789_remap_rgb565(c);

#if ST7789_SWAP_RGB565_BYTES
    dst[0] = (uint8_t)(c & 0xFFu);
    dst[1] = (uint8_t)(c >> 8);
#else
    dst[0] = (uint8_t)(c >> 8);
    dst[1] = (uint8_t)(c & 0xFFu);
#endif
}

static void st7789_scale_line_160_to_266(const uint16_t *src)
{
    uint16_t x_out;

    for (x_out = 0; x_out < GB_RENDER_WIDTH; ++x_out) {
        const uint16_t src_x = (uint16_t)(((uint32_t)x_out * GB_LCD_WIDTH) / GB_RENDER_WIDTH);
        const uint16_t c = src[src_x];
        st7789_pack_rgb565(c, &st7789_line_scaled[(size_t)x_out * 2u]);
    }
}

static void st7789_select(bool selected)
{
    gpio_put(ST7789_GPIO_CS, !selected);
}

static void st7789_dc(bool data_mode)
{
    gpio_put(ST7789_GPIO_DC, data_mode);
}

static void st7789_write_command_raw(uint8_t cmd)
{
    st7789_select(true);
    st7789_dc(false);
    spi_write_blocking(spi0, &cmd, 1);
    st7789_select(false);
}

static void st7789_write_data_raw(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    st7789_select(true);
    st7789_dc(true);
    spi_write_blocking(spi0, data, len);
    st7789_select(false);
}

static void st7789_write_command_data_raw(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7789_write_command_raw(cmd);
    st7789_write_data_raw(data, len);
}

static void st7789_set_addr_window_raw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4];
    uint8_t raset[4];

    x0 += ST7789_X_OFFSET;
    x1 += ST7789_X_OFFSET;
    y0 += ST7789_Y_OFFSET;
    y1 += ST7789_Y_OFFSET;

    caset[0] = (uint8_t)(x0 >> 8);
    caset[1] = (uint8_t)(x0 & 0xFF);
    caset[2] = (uint8_t)(x1 >> 8);
    caset[3] = (uint8_t)(x1 & 0xFF);

    raset[0] = (uint8_t)(y0 >> 8);
    raset[1] = (uint8_t)(y0 & 0xFF);
    raset[2] = (uint8_t)(y1 >> 8);
    raset[3] = (uint8_t)(y1 & 0xFF);

    st7789_write_command_data_raw(ST7789_CASET, caset, sizeof(caset));
    st7789_write_command_data_raw(ST7789_RASET, raset, sizeof(raset));
    st7789_write_command_raw(ST7789_RAMWR);
}

static bool st7789_dma_queue_busy(void)
{
    uint_fast8_t i;

    if (st7789_dma_active) {
        return true;
    }

    for (i = 0; i < ST7789_DMA_QUEUE_DEPTH; ++i) {
        if (st7789_dma_slots[i].state != ST7789_SLOT_FREE) {
            return true;
        }
    }

    return false;
}

static void st7789_wait_for_idle(void)
{
    if (!st7789_dma_initialized) {
        return;
    }

    st7789_flush_pending_band();

    while (st7789_dma_queue_busy()) {
        tight_loop_contents();
    }

    while (spi_is_busy(spi0)) {
        tight_loop_contents();
    }
}

static void st7789_write_command(uint8_t cmd)
{
    st7789_wait_for_idle();
    st7789_write_command_raw(cmd);
}

static void st7789_write_data(const uint8_t *data, size_t len)
{
    st7789_wait_for_idle();
    st7789_write_data_raw(data, len);
}

static void st7789_write_command_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    st7789_wait_for_idle();
    st7789_write_command_data_raw(cmd, data, len);
}

static void st7789_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    st7789_wait_for_idle();
    st7789_set_addr_window_raw(x0, y0, x1, y1);
}

static void st7789_dma_start_next_locked(void)
{
    st7789_dma_slot_t *slot;

    if (st7789_dma_active) {
        return;
    }

    slot = &st7789_dma_slots[st7789_dma_head];
    if (slot->state != ST7789_SLOT_READY) {
        return;
    }

    slot->state = ST7789_SLOT_ACTIVE;
    st7789_dma_active = true;

    st7789_set_addr_window_raw(GB_RENDER_X,
                               (uint16_t)(GB_RENDER_Y + slot->y0),
                               (uint16_t)(GB_RENDER_X + GB_RENDER_WIDTH - 1u),
                               (uint16_t)(GB_RENDER_Y + slot->y1 - 1u));
    st7789_select(true);
    st7789_dc(true);
    dma_channel_transfer_from_buffer_now((uint)st7789_dma_channel, slot->payload, slot->len);
}

static void st7789_dma_irq_handler(void)
{
    st7789_dma_slot_t *slot;

    dma_hw->ints1 = 1u << (uint)st7789_dma_channel;

    while (spi_is_busy(spi0)) {
        tight_loop_contents();
    }

    st7789_select(false);

    slot = &st7789_dma_slots[st7789_dma_head];
    slot->state = ST7789_SLOT_FREE;
    st7789_dma_head = (uint8_t)((st7789_dma_head + 1u) % ST7789_DMA_QUEUE_DEPTH);
    st7789_dma_active = false;
    st7789_dma_start_next_locked();
}

static void st7789_dma_init(void)
{
    dma_channel_config cfg;
    uint_fast8_t i;

    if (st7789_dma_initialized) {
        return;
    }

    st7789_dma_channel = dma_claim_unused_channel(true);
    cfg = dma_channel_get_default_config((uint)st7789_dma_channel);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, spi_get_dreq(spi0, true));

    dma_channel_configure((uint)st7789_dma_channel,
                          &cfg,
                          &spi_get_hw(spi0)->dr,
                          NULL,
                          0,
                          false);

    for (i = 0; i < ST7789_DMA_QUEUE_DEPTH; ++i) {
        st7789_dma_slots[i].state = ST7789_SLOT_FREE;
    }

    st7789_band_y0 = 0u;
    st7789_band_y1 = 0u;
    st7789_band_rows = 0u;
    st7789_band_len = 0u;
    st7789_dma_head = 0u;
    st7789_dma_tail = 0u;
    st7789_dma_active = false;

    irq_set_exclusive_handler(DMA_IRQ_1, st7789_dma_irq_handler);
    dma_channel_set_irq1_enabled((uint)st7789_dma_channel, true);
    irq_set_enabled(DMA_IRQ_1, true);
    st7789_dma_initialized = true;
}

static st7789_dma_slot_t *st7789_dma_reserve_slot(void)
{
    st7789_dma_slot_t *slot;

    while (1) {
        uint32_t flags = save_and_disable_interrupts();
        slot = &st7789_dma_slots[st7789_dma_tail];
        if (slot->state == ST7789_SLOT_FREE) {
            slot->state = ST7789_SLOT_RESERVED;
            st7789_dma_tail = (uint8_t)((st7789_dma_tail + 1u) % ST7789_DMA_QUEUE_DEPTH);
            restore_interrupts(flags);
            return slot;
        }
        restore_interrupts(flags);
        tight_loop_contents();
    }
}

static void st7789_dma_queue_packet(uint16_t y0, uint16_t y1,
                                    const uint8_t *payload, size_t len)
{
    st7789_dma_slot_t *slot;
    if (y1 <= y0 || payload == NULL || len == 0u) {
        return;
    }

    slot = st7789_dma_reserve_slot();
    slot->y0 = y0;
    slot->y1 = y1;
    slot->len = len;
    memcpy(slot->payload, payload, len);

    {
        uint32_t flags = save_and_disable_interrupts();
        slot->state = ST7789_SLOT_READY;
        st7789_dma_start_next_locked();
        restore_interrupts(flags);
    }
}

static void st7789_flush_pending_band(void)
{
    if (st7789_band_rows == 0u) {
        return;
    }

    st7789_dma_queue_packet(st7789_band_y0, st7789_band_y1,
                            st7789_band_buffer, st7789_band_len);
    st7789_band_y0 = 0u;
    st7789_band_y1 = 0u;
    st7789_band_rows = 0u;
    st7789_band_len = 0u;
}

static void st7789_hw_reset(void)
{
    gpio_put(ST7789_GPIO_RST, true);
    sleep_ms(5);
    gpio_put(ST7789_GPIO_RST, false);
    sleep_ms(25);
    gpio_put(ST7789_GPIO_RST, true);
    sleep_ms(120);
}

static void st7789_init_sequence(void)
{
    const uint8_t colmod[] = {(uint8_t)ST7789_COLMOD_VALUE};
    const uint8_t madctl[] = {(uint8_t)ST7789_MADCTL_VALUE};

    st7789_write_command(ST7789_SWRESET);
    sleep_ms(150);

    st7789_write_command(ST7789_SLPOUT);
    sleep_ms(120);

#if ST7789_INIT_INVERT
    st7789_write_command(ST7789_INVON);
#else
    st7789_write_command(ST7789_INVOFF);
#endif
    st7789_write_command_data(ST7789_COLMOD, colmod, sizeof(colmod));
    st7789_write_command_data(ST7789_MADCTL, madctl, sizeof(madctl));

    st7789_set_addr_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);

    st7789_write_command(ST7789_NORON);
    sleep_ms(10);
    st7789_write_command(ST7789_DISPON);
    sleep_ms(120);
}

unsigned mk_ili9225_init(void)
{
    printf("LCD: ST7789 init\n");
    printf("LCD cfg: MADCTL=0x%02X COLMOD=0x%02X INV=%u SWAP=%u ORDER=%s INV565=%u\n",
           (unsigned)((uint8_t)ST7789_MADCTL_VALUE),
           (unsigned)((uint8_t)ST7789_COLMOD_VALUE),
           (unsigned)ST7789_INIT_INVERT,
           (unsigned)ST7789_SWAP_RGB565_BYTES,
           st7789_rgb_order_name(),
           (unsigned)ST7789_INVERT_RGB565);

    gpio_put(ST7789_GPIO_LED, false);
    st7789_select(false);
    st7789_dc(true);
    st7789_dma_init();

    st7789_hw_reset();
    st7789_init_sequence();

    mk_ili9225_fill(0x0000);
    gpio_put(ST7789_GPIO_LED, true);
    return 0;
}

void mk_ili9225_display_control(bool invert, ili9225_color_mode_e colour_mode)
{
    (void)colour_mode;
    st7789_write_command(invert ? ST7789_INVON : ST7789_INVOFF);
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

    if (hor_start >= ST7789_WIDTH || vert_start >= ST7789_HEIGHT) {
        return;
    }

    if (hor_end >= ST7789_WIDTH) {
        hor_end = ST7789_WIDTH - 1;
    }

    if (vert_end >= ST7789_HEIGHT) {
        vert_end = ST7789_HEIGHT - 1;
    }

    st7789_set_addr_window(hor_start, vert_start, hor_end, vert_end);
}

void mk_ili9225_set_address(uint8_t x, uint8_t y)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) {
        return;
    }

    st7789_set_addr_window(x, y, x, y);
}

void mk_ili9225_set_x(uint8_t x)
{
    int gb_line = (int)x - 16;

    if (gb_line < 0 || gb_line >= (int)GB_LCD_HEIGHT) {
        st7789_current_y = -1;
        return;
    }

    st7789_current_y = gb_line;
}

void mk_ili9225_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb)
{
    uint16_t y0;
    uint16_t y1;
    uint16_t rows;
    uint16_t row;
    uint_fast16_t count;

    if (pixels == NULL || nmemb == 0 || st7789_current_y < 0) {
        return;
    }

    count = nmemb;
    if (count < GB_LCD_WIDTH) {
        return;
    }

    st7789_scale_line_160_to_266(pixels);

    y0 = (uint16_t)(((uint32_t)st7789_current_y * GB_RENDER_HEIGHT) / GB_LCD_HEIGHT);
    y1 = (uint16_t)(((uint32_t)(st7789_current_y + 1) * GB_RENDER_HEIGHT) / GB_LCD_HEIGHT);
    if (y1 <= y0) {
        y1 = y0 + 1u;
    }

    if (y1 > GB_RENDER_HEIGHT) {
        y1 = GB_RENDER_HEIGHT;
    }

    rows = (uint16_t)(y1 - y0);
    if (rows == 0u) {
        return;
    }

    if (st7789_band_rows != 0u &&
        (y0 != st7789_band_y1 ||
         (uint16_t)(st7789_band_rows + rows) > ST7789_DMA_BAND_LINES)) {
        st7789_flush_pending_band();
    }

    if (st7789_band_rows == 0u) {
        st7789_band_y0 = y0;
        st7789_band_y1 = y0;
        st7789_band_len = 0u;
    }

    for (row = 0; row < rows; ++row) {
        memcpy(&st7789_band_buffer[st7789_band_len], st7789_line_scaled,
               sizeof(st7789_line_scaled));
        st7789_band_len += sizeof(st7789_line_scaled);
        st7789_band_rows++;
        st7789_band_y1++;
    }

    if (st7789_band_rows >= ST7789_DMA_BAND_LINES ||
        st7789_current_y == (int)(GB_LCD_HEIGHT - 1u)) {
        st7789_flush_pending_band();
    }
}

void mk_ili9225_write_pixels_start(void)
{
    /* Not required for the ST7789 compatibility path. */
}

void mk_ili9225_write_pixels_end(void)
{
    st7789_flush_pending_band();
}

void mk_ili9225_power_control(uint8_t drive_power, bool sleep)
{
    (void)drive_power;
    st7789_write_command(sleep ? ST7789_SLPIN : ST7789_SLPOUT);
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
    st7789_write_command(ST7789_DISPOFF);
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

    if (x0 >= ST7789_WIDTH || y0 >= ST7789_HEIGHT) {
        return;
    }

    x1 = (uint16_t)(x0 + w - 1);
    y1 = (uint16_t)(y0 + h - 1);

    if (x1 >= ST7789_WIDTH) {
        x1 = ST7789_WIDTH - 1;
    }

    if (y1 >= ST7789_HEIGHT) {
        y1 = ST7789_HEIGHT - 1;
    }

    st7789_pack_rgb565(color, &color_block[0]);
    for (i = 2; i < sizeof(color_block); i += 2) {
        color_block[i] = color_block[0];
        color_block[i + 1] = color_block[1];
    }

    st7789_set_addr_window(x0, y0, x1, y1);

    st7789_select(true);
    st7789_dc(true);
    pixels = ((uint32_t)x1 - x0 + 1u) * ((uint32_t)y1 - y0 + 1u);
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

void mk_ili9225_fill(uint16_t color)
{
    static uint8_t color_block[128];
    uint32_t pixels;
    size_t i;

    st7789_pack_rgb565(color, &color_block[0]);
    for (i = 2; i < sizeof(color_block); i += 2) {
        color_block[i] = color_block[0];
        color_block[i + 1] = color_block[1];
    }

    st7789_set_addr_window(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    st7789_select(true);
    st7789_dc(true);

    pixels = (uint32_t)ST7789_WIDTH * (uint32_t)ST7789_HEIGHT;
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

    if (x0 >= ST7789_WIDTH || y0 >= ST7789_HEIGHT) {
        return;
    }

    x1 = (uint16_t)(x0 + w - 1);
    y1 = (uint16_t)(y0 + h - 1);

    if (x1 >= ST7789_WIDTH) {
        x1 = ST7789_WIDTH - 1;
    }

    if (y1 >= ST7789_HEIGHT) {
        y1 = ST7789_HEIGHT - 1;
    }

    pixels = ((uint32_t)x1 - x0 + 1u) * ((uint32_t)y1 - y0 + 1u);
    st7789_set_addr_window(x0, y0, x1, y1);

    st7789_select(true);
    st7789_dc(true);
    while (sent < pixels) {
        uint32_t chunk_pixels = (uint32_t)(sizeof(txbuf) / 2u);
        uint32_t i;

        if (chunk_pixels > (pixels - sent)) {
            chunk_pixels = pixels - sent;
        }

        for (i = 0; i < chunk_pixels; ++i) {
            uint16_t c = fbuf[sent + i];
            st7789_pack_rgb565(c, &txbuf[i * 2u]);
        }

        spi_write_blocking(spi0, txbuf, chunk_pixels * 2u);
        sent += chunk_pixels;
    }
    st7789_select(false);
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
		if(x>(ST7789_WIDTH-8)) {
			break;
		}
	}
}
