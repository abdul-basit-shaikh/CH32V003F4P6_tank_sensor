#include "lora_sx1278.h"
#include "config.h"
#include "debug.h"
#include <string.h>

// SX1278 Registers
#define REG_FIFO                    0x00
#define REG_OP_MODE                 0x01
#define REG_FRF_MSB                 0x06
#define REG_FRF_MID                 0x07
#define REG_FRF_LSB                 0x08
#define REG_PA_CONFIG               0x09
#define REG_PA_RAMP                 0x0a
#define REG_OCP                     0x0b
#define REG_LNA                     0x0c
#define REG_FIFO_ADDR_PTR           0x0d
#define REG_FIFO_TX_BASE_ADDR       0x0e
#define REG_FIFO_RX_BASE_ADDR       0x0f
#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS               0x12
#define REG_RX_NB_BYTES             0x13
#define REG_PKT_SNR_VALUE           0x19
#define REG_PKT_RSSI_VALUE          0x1a
#define REG_MODEM_CONFIG_1          0x1d
#define REG_MODEM_CONFIG_2          0x1e
#define REG_PAYLOAD_LENGTH          0x22
#define REG_MODEM_CONFIG_3          0x26
#define REG_SYNC_WORD               0x39
#define REG_VERSION                 0x42

// SX1278 433MHz LoRa Modes (Bit 7 = LoRa, Bit 3 = Low Frequency Mode On for 433MHz)
#define MODE_LORA_BASE              0x88 // LoRa (0x80) | LowFrequencyMode (0x08)
#define MODE_SLEEP                  0x00
#define MODE_STDBY                  0x01
#define MODE_TX                     0x03
#define MODE_RX_CONTINUOUS          0x05

#define PA_BOOST                    0x80
#define IRQ_TX_DONE_MASK            0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK  0x20
#define IRQ_RX_DONE_MASK            0x40

static uint8_t spi_transfer(uint8_t data) {
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI1);
}

static inline void nss_low(void) {
    GPIO_ResetBits(GPIOC, LORA_NSS_PIN);
}

static inline void nss_high(void) {
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    GPIO_SetBits(GPIOC, LORA_NSS_PIN);
}

uint8_t lora_read_reg(uint8_t reg) {
    nss_low();
    spi_transfer((uint8_t)(reg & 0x7F));
    uint8_t val = spi_transfer(0x00);
    nss_high();
    return val;
}

void lora_write_reg(uint8_t reg, uint8_t val) {
    nss_low();
    spi_transfer((uint8_t)(reg | 0x80));
    spi_transfer(val);
    nss_high();
}

static void lora_write_buf(uint8_t reg, const uint8_t *buf, size_t len) {
    nss_low();
    spi_transfer((uint8_t)(reg | 0x80));
    for (size_t i = 0; i < len; i++) {
        spi_transfer(buf[i]);
    }
    nss_high();
}

static void lora_read_buf(uint8_t reg, uint8_t *buf, size_t len) {
    nss_low();
    spi_transfer((uint8_t)(reg & 0x7F));
    for (size_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(0x00);
    }
    nss_high();
}

void lora_reset(void) {
    GPIO_ResetBits(GPIOD, LORA_RST_PIN);
    Delay_Ms(10);
    GPIO_SetBits(GPIOD, LORA_RST_PIN);
    Delay_Ms(20);
}

void lora_power_down(void) {
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_SLEEP); // 0x88
}

void lora_power_up_tx(void) {
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY); // 0x89
}

void lora_power_up_rx(void) {
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY);
    Delay_Ms(2);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);
    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_reg(REG_IRQ_FLAGS, 0xFF);
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_RX_CONTINUOUS); // 0x8D
}

static void lora_set_frequency(uint32_t frequency) {
    uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
    lora_write_reg(REG_FRF_MSB, (uint8_t)(frf >> 16));
    lora_write_reg(REG_FRF_MID, (uint8_t)(frf >> 8));
    lora_write_reg(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

uint8_t lora_init(void) {
    DEBUG_PRINT("\r\n========================================\r\n");
    DEBUG_PRINT("[LORA] --- SX1278 LoRa Initialization ---\r\n");

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef SPI_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD |
                           RCC_APB2Periph_SPI1 | RCC_APB2Periph_AFIO,
                           ENABLE);

    // SCK (PC5), MOSI (PC6)
    GPIO_InitStructure.GPIO_Pin = LORA_SCK_PIN | LORA_MOSI_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // MISO (PC7)
    GPIO_InitStructure.GPIO_Pin = LORA_MISO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // NSS / CS (PC3)
    GPIO_InitStructure.GPIO_Pin = LORA_NSS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_SetBits(GPIOC, LORA_NSS_PIN);

    // RST (PD4)
    GPIO_InitStructure.GPIO_Pin = LORA_RST_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
    GPIO_SetBits(GPIOD, LORA_RST_PIN);

    // SPI Config (1.5 MHz)
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);

    // Hardware Reset
    DEBUG_PRINT("[LORA] Resetting module via PD4...\r\n");
    lora_reset();

    // Check Chip Version (0x12 expected)
    uint8_t ver = lora_read_reg(REG_VERSION);
    DEBUG_PRINT("[LORA] Read Silicon Version: 0x%02X (Expected: 0x12)\r\n", ver);
    if (ver != 0x12) {
        DEBUG_PRINT("[LORA] ? ERROR: SX1278 not responding! Check SCK/MISO/MOSI/NSS wires.\r\n");
        return 1;
    }

    // Step 1: Standard FSK Sleep (0x00)
    lora_write_reg(REG_OP_MODE, MODE_SLEEP);
    Delay_Ms(15);

    // Step 2: LoRa 433MHz Sleep (0x88)
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_SLEEP);
    Delay_Ms(15);

    // Step 3: Configure ALL RF parameters WHILE IN SLEEP MODE (Mandatory for Semtech SX1278)
    lora_set_frequency(LORA_FREQUENCY);
    lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    // LNA Boost + Auto AGC
    lora_write_reg(REG_LNA, 0x23);            // Max LNA gain
    lora_write_reg(REG_MODEM_CONFIG_3, 0x04); // Auto AGC On

    // BW 125kHz (0x70) | CR 4/7 (0x06) => 0x76 (Forward Error Correction enabled)
    lora_write_reg(REG_MODEM_CONFIG_1, 0x76);

    // SF7 (0x70) | CRC ON (0x04) => 0x74
    lora_write_reg(REG_MODEM_CONFIG_2, 0x74);

    // Detection optimize & threshold for SF7
    lora_write_reg(0x31, 0x03);
    lora_write_reg(0x37, 0x0A);

    // Private Sync Word
    lora_write_reg(REG_SYNC_WORD, 0x12);

    // TX Power +17dBm PA_BOOST with Soft-Start Ramp
    lora_write_reg(REG_PA_RAMP, 0x08);   // 100us Soft-Start PA Ramp-up (prevents sudden inrush current spikes)
    lora_write_reg(REG_PA_CONFIG, 0x8F); // +17dBm Maximum Range Output
    lora_write_reg(0x4D, 0x84);          // Clean PA_DAC
    lora_write_reg(REG_OCP, 0x2B);       // 100mA safe OCP limit

    // Step 4: Now switch to Standby (0x89)
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY);
    Delay_Ms(15);

    uint8_t opmode = lora_read_reg(REG_OP_MODE);
    DEBUG_PRINT("[LORA] OpMode after init: 0x%02X (Expected 0x89)\r\n", opmode);

    DEBUG_PRINT("[LORA] ? Init Complete: 433MHz | SF7 | BW 125kHz | Power +17dBm (Soft-Start 100us)\r\n");
    DEBUG_PRINT("========================================\r\n");
    return 0;
}

uint8_t lora_send(const uint8_t *buf, size_t size) {
    if (size > 256) size = 256;

    // 1. Enter Standby (0x89) & allow 100uF capacitor to fully charge
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY);
    Delay_Ms(30); // 30ms Pre-TX Capacitor Charge Delay

    // 2. Load FIFO
    lora_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    lora_write_buf(REG_FIFO, buf, size);
    lora_write_reg(REG_PAYLOAD_LENGTH, (uint8_t)size);

    // 3. Clear IRQ flags
    lora_write_reg(REG_IRQ_FLAGS, 0xFF);

    // 4. Trigger TX (0x8B)
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_TX);

    // 5. Wait for TX_DONE flag
    uint16_t timeout = 800; // max 800ms
    while (timeout > 0) {
        uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);
        if (irq & IRQ_TX_DONE_MASK) {
            // Success! Clear IRQ and put in Standby
            lora_write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
            lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY);
            DEBUG_PRINT("[LORA TX] ? Packet Sent! (Time: %d ms, IRQ: 0x%02X)\r\n", 800 - timeout, irq);
            return 0;
        }
        Delay_Ms(1);
        timeout--;
    }

    // Timeout Diagnostic Log
    uint8_t final_op = lora_read_reg(REG_OP_MODE);
    uint8_t final_irq = lora_read_reg(REG_IRQ_FLAGS);
    DEBUG_PRINT("[LORA TX] ? TX Timeout! Final OpMode: 0x%02X, IRQ_FLAGS: 0x%02X\r\n", final_op, final_irq);
    lora_write_reg(REG_OP_MODE, MODE_LORA_BASE | MODE_STDBY);
    return 1;
}

bool lora_available(void) {
    uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);
    if (irq & IRQ_RX_DONE_MASK) {
        if (irq & IRQ_PAYLOAD_CRC_ERROR_MASK) {
            DEBUG_PRINT("[LORA RX] CRC Error! Discarding packet.\r\n");
            lora_write_reg(REG_IRQ_FLAGS, IRQ_PAYLOAD_CRC_ERROR_MASK | IRQ_RX_DONE_MASK);
            return false;
        }
        return true;
    }
    return false;
}

uint8_t lora_read(uint8_t *buf, uint8_t max_len) {
    uint8_t irq = lora_read_reg(REG_IRQ_FLAGS);
    if (irq & IRQ_PAYLOAD_CRC_ERROR_MASK) {
        lora_write_reg(REG_IRQ_FLAGS, IRQ_PAYLOAD_CRC_ERROR_MASK | IRQ_RX_DONE_MASK);
        return 0;
    }
    if ((irq & IRQ_RX_DONE_MASK) == 0) return 0;

    uint8_t count = lora_read_reg(REG_RX_NB_BYTES);
    if (count == 0) {
        lora_write_reg(REG_IRQ_FLAGS, 0xFF);
        return 0;
    }

    uint8_t current_addr = lora_read_reg(REG_FIFO_RX_CURRENT_ADDR);
    lora_write_reg(REG_FIFO_ADDR_PTR, current_addr);

    size_t to_read = (count < max_len) ? count : max_len;
    lora_read_buf(REG_FIFO, buf, to_read);

    lora_write_reg(REG_IRQ_FLAGS, 0xFF);
    DEBUG_PRINT("[LORA RX] ? Received %d Bytes from FIFO (Addr: 0x%02X)\r\n", (int)to_read, current_addr);
    return to_read;
}