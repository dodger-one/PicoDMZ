#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "mfrc522.h"

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

static void print_uid(const mfrc522_uid_t *uid)
{
    printf("UID len=%u hex=", uid->len);
    for (uint8_t i = 0; i < uid->len; ++i) {
        printf("%02X", uid->bytes[i]);
        if ((uint8_t)(i + 1u) < uid->len) {
            putchar(':');
        }
    }
    printf("\r\n");
}

int main(void)
{
    mfrc522_uid_t last_uid = { 0 };
    absolute_time_t next_probe_log;
    bool had_tag = false;

    stdio_init_all();
    console_startup_sync();

    printf("NFC smoke test\r\n");
    printf("Pins: SPI1 MISO=GP12 MOSI=GP15 SCK=GP14 CS=GP10 RST=GP11\r\n");
    printf("Initializing RC522...\r\n");

    mfrc522_init();

    {
        const uint8_t version = mfrc522_version();
        printf("RC522 VersionReg=0x%02X\r\n", version);
        if (version == 0x00u || version == 0xFFu) {
            printf("WARN: RC522 not detected or SPI wiring/power issue.\r\n");
        }
    }

    printf("Bring a tag near the reader.\r\n");
    next_probe_log = make_timeout_time_ms(2000);

    while (true) {
        mfrc522_uid_t uid = { 0 };

        if (mfrc522_read_uid(&uid)) {
            if (!had_tag || uid.len != last_uid.len ||
                memcmp(uid.bytes, last_uid.bytes, uid.len) != 0) {
                printf("Tag detected: ");
                print_uid(&uid);
                last_uid = uid;
            }
            had_tag = true;
            next_probe_log = make_timeout_time_ms(2000);
        } else {
            if (had_tag) {
                printf("Tag removed\r\n");
                memset(&last_uid, 0, sizeof(last_uid));
            } else if (time_reached(next_probe_log)) {
                printf("Waiting for tag...\r\n");
                next_probe_log = make_timeout_time_ms(2000);
            }
            had_tag = false;
        }

        sleep_ms(100);
    }
}
