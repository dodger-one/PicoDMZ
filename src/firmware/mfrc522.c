#include "mfrc522.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#ifndef MFRC522_SPI_INST
#define MFRC522_SPI_INST spi1
#endif

#ifndef MFRC522_SPI_BAUD
#define MFRC522_SPI_BAUD (4000 * 1000)
#endif

#ifndef MFRC522_PIN_MISO
#define MFRC522_PIN_MISO 12u
#endif
#ifndef MFRC522_PIN_MOSI
#define MFRC522_PIN_MOSI 15u
#endif
#ifndef MFRC522_PIN_SCK
#define MFRC522_PIN_SCK 14u
#endif
#ifndef MFRC522_PIN_CS
#define MFRC522_PIN_CS 10u
#endif
#ifndef MFRC522_PIN_RST
#define MFRC522_PIN_RST 11u
#endif

#define MFRC522_CMD_IDLE        0x00u
#define MFRC522_CMD_MEM         0x01u
#define MFRC522_CMD_CALC_CRC    0x03u
#define MFRC522_CMD_TRANSCEIVE  0x0Cu
#define MFRC522_CMD_SOFT_RESET  0x0Fu

#define MFRC522_REG_COMMAND       0x01u
#define MFRC522_REG_COM_IRQ       0x04u
#define MFRC522_REG_DIV_IRQ       0x05u
#define MFRC522_REG_ERROR         0x06u
#define MFRC522_REG_FIFO_DATA     0x09u
#define MFRC522_REG_FIFO_LEVEL    0x0Au
#define MFRC522_REG_CONTROL       0x0Cu
#define MFRC522_REG_BIT_FRAMING   0x0Du
#define MFRC522_REG_MODE          0x11u
#define MFRC522_REG_TX_CONTROL    0x14u
#define MFRC522_REG_TX_ASK        0x15u
#define MFRC522_REG_CRC_RESULT_H  0x21u
#define MFRC522_REG_CRC_RESULT_L  0x22u
#define MFRC522_REG_T_MODE        0x2Au
#define MFRC522_REG_T_PRESCALER   0x2Bu
#define MFRC522_REG_T_RELOAD_H    0x2Cu
#define MFRC522_REG_T_RELOAD_L    0x2Du
#define MFRC522_REG_VERSION       0x37u

#define PICC_CMD_REQA       0x26u
#define PICC_CMD_SEL_CL1    0x93u
#define PICC_CMD_SEL_CL2    0x95u

static inline void mfrc522_select(bool selected)
{
    gpio_put(MFRC522_PIN_CS, selected ? 0 : 1);
}

static uint8_t mfrc522_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1u) & 0x7Eu) | 0x80u), 0x00u };
    uint8_t rx[2] = { 0 };

    mfrc522_select(true);
    spi_write_read_blocking(MFRC522_SPI_INST, tx, rx, sizeof(tx));
    mfrc522_select(false);
    return rx[1];
}

static void mfrc522_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1u) & 0x7Eu), value };

    mfrc522_select(true);
    spi_write_blocking(MFRC522_SPI_INST, tx, sizeof(tx));
    mfrc522_select(false);
}

static void mfrc522_set_bits(uint8_t reg, uint8_t mask)
{
    mfrc522_write_reg(reg, (uint8_t)(mfrc522_read_reg(reg) | mask));
}

static void mfrc522_clear_bits(uint8_t reg, uint8_t mask)
{
    mfrc522_write_reg(reg, (uint8_t)(mfrc522_read_reg(reg) & (uint8_t)~mask));
}

static void mfrc522_antenna_on(void)
{
    if ((mfrc522_read_reg(MFRC522_REG_TX_CONTROL) & 0x03u) != 0x03u) {
        mfrc522_set_bits(MFRC522_REG_TX_CONTROL, 0x03u);
    }
}

static bool mfrc522_transceive(const uint8_t *send_data, uint8_t send_len,
                               uint8_t *back_data, uint8_t *back_len,
                               uint8_t valid_bits)
{
    uint16_t timeout = 2000u;
    uint8_t back_capacity = *back_len;
    uint8_t irq;
    uint8_t fifo_level;
    uint8_t last_bits;

    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    mfrc522_write_reg(MFRC522_REG_COM_IRQ, 0x7Fu);
    mfrc522_set_bits(MFRC522_REG_FIFO_LEVEL, 0x80u);

    for (uint8_t i = 0; i < send_len; ++i) {
        mfrc522_write_reg(MFRC522_REG_FIFO_DATA, send_data[i]);
    }

    mfrc522_write_reg(MFRC522_REG_BIT_FRAMING, valid_bits);
    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE);
    mfrc522_set_bits(MFRC522_REG_BIT_FRAMING, 0x80u);

    do {
        irq = mfrc522_read_reg(MFRC522_REG_COM_IRQ);
        if (irq & 0x30u) {
            break;
        }
    } while (--timeout != 0u && !(irq & 0x01u));

    mfrc522_clear_bits(MFRC522_REG_BIT_FRAMING, 0x80u);

    if (timeout == 0u || (irq & 0x01u)) {
        return false;
    }

    if (mfrc522_read_reg(MFRC522_REG_ERROR) & 0x1Bu) {
        return false;
    }

    fifo_level = mfrc522_read_reg(MFRC522_REG_FIFO_LEVEL);
    last_bits = mfrc522_read_reg(MFRC522_REG_CONTROL) & 0x07u;
    if (last_bits != 0u) {
        *back_len = (uint8_t)((fifo_level - 1u) * 8u + last_bits);
    } else {
        *back_len = (uint8_t)(fifo_level * 8u);
    }

    if (fifo_level == 0u) {
        fifo_level = 1u;
    }
    if (fifo_level > back_capacity) {
        fifo_level = back_capacity;
    }

    for (uint8_t i = 0; i < fifo_level; ++i) {
        back_data[i] = mfrc522_read_reg(MFRC522_REG_FIFO_DATA);
    }

    return true;
}

static bool mfrc522_calc_crc(const uint8_t *data, uint8_t len, uint8_t result[2])
{
    uint16_t timeout = 5000u;

    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    mfrc522_write_reg(MFRC522_REG_DIV_IRQ, 0x04u);
    mfrc522_set_bits(MFRC522_REG_FIFO_LEVEL, 0x80u);

    for (uint8_t i = 0; i < len; ++i) {
        mfrc522_write_reg(MFRC522_REG_FIFO_DATA, data[i]);
    }

    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_CALC_CRC);
    while (--timeout != 0u) {
        if (mfrc522_read_reg(MFRC522_REG_DIV_IRQ) & 0x04u) {
            result[0] = mfrc522_read_reg(MFRC522_REG_CRC_RESULT_L);
            result[1] = mfrc522_read_reg(MFRC522_REG_CRC_RESULT_H);
            mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
            return true;
        }
    }

    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE);
    return false;
}

static bool mfrc522_anticoll(uint8_t cascade_level, uint8_t serial[5])
{
    uint8_t response[16] = { 0 };
    uint8_t response_bits = sizeof(response);
    const uint8_t anticoll[] = { cascade_level, 0x20u };

    if (!mfrc522_transceive(anticoll, sizeof(anticoll), response, &response_bits, 0x00u)) {
        return false;
    }
    if (response_bits < 40u) {
        return false;
    }

    uint8_t bcc = response[0] ^ response[1] ^ response[2] ^ response[3];
    if (bcc != response[4]) {
        return false;
    }

    memcpy(serial, response, 5u);
    return true;
}

static bool mfrc522_select_picc(uint8_t cascade_level, const uint8_t serial[5], uint8_t *sak)
{
    uint8_t command[9] = { 0 };
    uint8_t response[3] = { 0 };
    uint8_t response_bits = sizeof(response);

    command[0] = cascade_level;
    command[1] = 0x70u;
    memcpy(&command[2], serial, 5u);
    if (!mfrc522_calc_crc(command, 7u, &command[7])) {
        return false;
    }

    if (!mfrc522_transceive(command, sizeof(command), response, &response_bits, 0x00u)) {
        return false;
    }
    if (response_bits != 24u) {
        return false;
    }

    if (sak != NULL) {
        *sak = response[0];
    }
    return true;
}

void mfrc522_init(void)
{
    gpio_set_function(MFRC522_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(MFRC522_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(MFRC522_PIN_SCK, GPIO_FUNC_SPI);
    gpio_pull_up(MFRC522_PIN_MISO);
    spi_init(MFRC522_SPI_INST, MFRC522_SPI_BAUD);
    spi_set_format(MFRC522_SPI_INST, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_init(MFRC522_PIN_CS);
    gpio_set_dir(MFRC522_PIN_CS, GPIO_OUT);
    gpio_put(MFRC522_PIN_CS, 1);

    gpio_init(MFRC522_PIN_RST);
    gpio_set_dir(MFRC522_PIN_RST, GPIO_OUT);
    gpio_put(MFRC522_PIN_RST, 0);
    sleep_ms(20);
    gpio_put(MFRC522_PIN_RST, 1);
    sleep_ms(50);

    mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET);
    sleep_ms(50);

    mfrc522_write_reg(MFRC522_REG_T_MODE, 0x8Du);
    mfrc522_write_reg(MFRC522_REG_T_PRESCALER, 0x3Eu);
    mfrc522_write_reg(MFRC522_REG_T_RELOAD_L, 30u);
    mfrc522_write_reg(MFRC522_REG_T_RELOAD_H, 0u);
    mfrc522_write_reg(MFRC522_REG_TX_ASK, 0x40u);
    mfrc522_write_reg(MFRC522_REG_MODE, 0x3Du);
    mfrc522_antenna_on();
}

uint8_t mfrc522_version(void)
{
    return mfrc522_read_reg(MFRC522_REG_VERSION);
}

bool mfrc522_read_uid(mfrc522_uid_t *uid)
{
    uint8_t response[16] = { 0 };
    uint8_t response_bits = sizeof(response);
    uint8_t serial_cl1[5] = { 0 };
    uint8_t serial_cl2[5] = { 0 };
    uint8_t sak = 0;
    const uint8_t reqa = PICC_CMD_REQA;

    if (uid == NULL) {
        return false;
    }
    memset(uid, 0, sizeof(*uid));

    if (!mfrc522_transceive(&reqa, 1u, response, &response_bits, 0x07u)) {
        return false;
    }
    if (response_bits != 16u) {
        return false;
    }

    if (!mfrc522_anticoll(PICC_CMD_SEL_CL1, serial_cl1)) {
        return false;
    }

    if (serial_cl1[0] != 0x88u) {
        if (!mfrc522_select_picc(PICC_CMD_SEL_CL1, serial_cl1, &sak)) {
            return false;
        }
        memcpy(uid->bytes, serial_cl1, 4u);
        uid->len = 4u;
        return true;
    }

    if (!mfrc522_select_picc(PICC_CMD_SEL_CL1, serial_cl1, &sak)) {
        return false;
    }
    if ((sak & 0x04u) == 0u) {
        return false;
    }

    if (!mfrc522_anticoll(PICC_CMD_SEL_CL2, serial_cl2)) {
        return false;
    }
    if (!mfrc522_select_picc(PICC_CMD_SEL_CL2, serial_cl2, &sak)) {
        return false;
    }

    uid->bytes[0] = serial_cl1[1];
    uid->bytes[1] = serial_cl1[2];
    uid->bytes[2] = serial_cl1[3];
    uid->bytes[3] = serial_cl2[0];
    uid->bytes[4] = serial_cl2[1];
    uid->bytes[5] = serial_cl2[2];
    uid->bytes[6] = serial_cl2[3];
    uid->len = 7u;
    return true;
}
