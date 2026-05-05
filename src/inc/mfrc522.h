#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MFRC522_UID_MAX_LEN 10u

typedef struct {
    uint8_t bytes[MFRC522_UID_MAX_LEN];
    uint8_t len;
} mfrc522_uid_t;

void mfrc522_init(void);
uint8_t mfrc522_version(void);
bool mfrc522_read_uid(mfrc522_uid_t *uid);
