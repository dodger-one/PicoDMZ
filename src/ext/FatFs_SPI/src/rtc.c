/* rtc.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use 
this file except in compliance with the License. You may obtain a copy of the 
License at

   http://www.apache.org/licenses/LICENSE-2.0 
Unless required by applicable law or agreed to in writing, software distributed 
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR 
CONDITIONS OF ANY KIND, either express or implied. See the License for the 
specific language governing permissions and limitations under the License.
*/
#include <stdio.h>
#include <time.h>
//
#if __has_include("hardware/rtc.h")
#define FATFS_SPI_HAVE_HW_RTC 1
#include "hardware/rtc.h"
#else
#define FATFS_SPI_HAVE_HW_RTC 0
typedef struct {
    int16_t year;
    int8_t month;
    int8_t day;
    int8_t dotw;
    int8_t hour;
    int8_t min;
    int8_t sec;
} datetime_t;
#endif
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
//
#include "ff.h"
#include "util.h"  // calculate_checksum
//
#include "rtc.h"

static time_t epochtime;

// Make an attempt to save a recent time stamp across reset:
typedef struct rtc_save {
    uint32_t signature;
    datetime_t datetime;
    uint32_t checksum;  // last, not included in checksum
} rtc_save_t;
static rtc_save_t rtc_save __attribute__((section(".uninitialized_data")));

static void update_epochtime() {
#if FATFS_SPI_HAVE_HW_RTC
    bool rc = rtc_get_datetime(&rtc_save.datetime);
    if (rc) {
        rtc_save.signature = 0xBABEBABE;
        struct tm timeinfo = {
            .tm_sec = rtc_save.datetime.sec,
            .tm_min = rtc_save.datetime.min,
            .tm_hour = rtc_save.datetime.hour,
            .tm_mday = rtc_save.datetime.day,
            .tm_mon = rtc_save.datetime.month - 1,
            .tm_year = rtc_save.datetime.year - 1900,
            .tm_wday = 0,
            .tm_yday = 0,
            .tm_isdst = -1
        };
        rtc_save.checksum = calculate_checksum((uint32_t *)&rtc_save,
                                               offsetof(rtc_save_t, checksum));
        epochtime = mktime(&timeinfo);
        rtc_save.datetime.dotw = timeinfo.tm_wday;
    }
#else
    if (epochtime == 0) {
        struct tm timeinfo = {
            .tm_sec = 0,
            .tm_min = 0,
            .tm_hour = 0,
            .tm_mday = 1,
            .tm_mon = 0,
            .tm_year = 124,
            .tm_wday = 1,
            .tm_yday = 0,
            .tm_isdst = -1
        };
        epochtime = mktime(&timeinfo);
    }
#endif
}

time_t time(time_t *pxTime) {
    update_epochtime();
    if (pxTime) {
        *pxTime = epochtime;
    }
    return epochtime;
}

void time_init() {
#if FATFS_SPI_HAVE_HW_RTC
    rtc_init();
    datetime_t t = {0, 0, 0, 0, 0, 0, 0};
    rtc_get_datetime(&t);
    if (!t.year && rtc_save.datetime.year) {
        uint32_t xor_checksum = calculate_checksum(
            (uint32_t *)&rtc_save, offsetof(rtc_save_t, checksum));
        if (rtc_save.signature == 0xBABEBABE &&
            rtc_save.checksum == xor_checksum) {
            rtc_set_datetime(&rtc_save.datetime);
        }
    }
#else
    update_epochtime();
#endif
}

// Called by FatFs:
DWORD get_fattime(void) {
    datetime_t t = {0, 0, 0, 0, 0, 0, 0};
#if FATFS_SPI_HAVE_HW_RTC
    bool rc = rtc_get_datetime(&t);
    if (!rc) return 0;
#else
    t.year = 2024;
    t.month = 1;
    t.day = 1;
    t.hour = 0;
    t.min = 0;
    t.sec = 0;
#endif

    DWORD fattime = 0;
    // bit31:25
    // Year origin from the 1980 (0..127, e.g. 37 for 2017)
    uint8_t yr = t.year - 1980;
    fattime |= (0b01111111 & yr) << 25;
    // bit24:21
    // Month (1..12)
    uint8_t mo = t.month;
    fattime |= (0b00001111 & mo) << 21;
    // bit20:16
    // Day of the month (1..31)
    uint8_t da = t.day;
    fattime |= (0b00011111 & da) << 16;
    // bit15:11
    // Hour (0..23)
    uint8_t hr = t.hour;
    fattime |= (0b00011111 & hr) << 11;
    // bit10:5
    // Minute (0..59)
    uint8_t mi = t.min;
    fattime |= (0b00111111 & mi) << 5;
    // bit4:0
    // Second / 2 (0..29, e.g. 25 for 50)
    uint8_t sd = t.sec / 2;
    fattime |= (0b00011111 & sd);
    return fattime;
}
