#include "nrf24_simple.h"
#include "config.h"
#include "debug.h"
#include <stdio.h>

// NRF24L01 Registers
#define REG_CONFIG 0x00
#define REG_EN_AA 0x01
#define REG_EN_RXADDR 0x02
#define REG_SETUP_AW 0x03
#define REG_SETUP_RETR 0x04
#define REG_RF_CH 0x05
#define REG_RF_SETUP 0x06
#define REG_STATUS 0x07
#define REG_RPD 0x09
#define REG_RX_ADDR_P0 0x0A
#define REG_TX_ADDR 0x10
#define REG_RX_PW_P0 0x11
#define REG_FIFO_STATUS 0x17

#define STATUS_RX_DR 0x40
#define STATUS_TX_DS 0x20
#define STATUS_MAX_RT 0x10
#define STATUS_ALL_IRQ (STATUS_RX_DR | STATUS_TX_DS | STATUS_MAX_RT)
#define FIFO_RX_EMPTY 0x01

// Commands
#define CMD_R_REG 0x00
#define CMD_W_REG 0x20
#define CMD_R_RX_PL 0x61
#define CMD_W_TX_PL 0xA0
#define CMD_FLUSH_TX 0xE1
#define CMD_FLUSH_RX 0xE2
#define CMD_NOP 0xFF

static uint8_t g_rpd_latched = 0;

static void spi_init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  SPI_InitTypeDef SPI_InitStructure = {0};

  // Clock Enable
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD |
                             RCC_APB2Periph_SPI1,
                         ENABLE);

  // SCK (PC5), MOSI (PC6)
  GPIO_InitStructure.GPIO_Pin = NRF_SCK_PIN | NRF_MOSI_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  // MISO (PC7)
  GPIO_InitStructure.GPIO_Pin = NRF_MISO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  // CSN (PC3)
  GPIO_InitStructure.GPIO_Pin = NRF_CSN_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);

  // CE (PD4)
  GPIO_InitStructure.GPIO_Pin = NRF_CE_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_Init(GPIOD, &GPIO_InitStructure);
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);

  // SPI Config
  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler =
      SPI_BaudRatePrescaler_16; // 48/16 = 3MHz
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(SPI1, &SPI_InitStructure);
  SPI_Cmd(SPI1, ENABLE);
}

static uint8_t spi_xfer(uint8_t data) {
  while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    ;
  SPI_I2S_SendData(SPI1, data);
  while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET)
    ;
  return SPI_I2S_ReceiveData(SPI1);
}

static void nrf_write_reg(uint8_t reg, uint8_t val) {
  // printf("[SPI] WRITE REG 0x%02X -> VAL 0x%02X\r\n", reg, val);
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_W_REG | reg);
  spi_xfer(val);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
}

uint8_t nrf_read_reg(uint8_t reg) {
  uint8_t val;
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_R_REG | reg);
  val = spi_xfer(CMD_NOP);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
  // printf("[SPI] READ REG 0x%02X <- VAL 0x%02X\r\n", reg, val);
  return val;
}

static void nrf_write_buf(uint8_t reg, const uint8_t *buf, uint8_t len) {
  // printf("[SPI] WRITE BUF 0x%02X: ", reg);
  // for (int i = 0; i < len; i++)
  //   printf("%02X ", buf[i]);
  // printf("\r\n");

  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_W_REG | reg);
  while (len--)
    spi_xfer(*buf++);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
}

static void nrf_strobe(uint8_t cmd) {
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(cmd);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
}

static void nrf_write_payload(const uint8_t *data, uint8_t len) {
  uint8_t payload_len = (len > 32) ? 32 : len;
  uint8_t i = 0;

  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_W_TX_PL);

  for (; i < payload_len; i++) {
    spi_xfer(data[i]);
  }
  for (; i < 32; i++) {
    spi_xfer(0);
  }

  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
}

static void nrf_read_payload(uint8_t *data, uint8_t len) {
  uint8_t read_len = (len > 32) ? 32 : len;
  uint8_t i = 0;

  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_R_RX_PL);

  for (; i < read_len; i++) {
    data[i] = spi_xfer(CMD_NOP);
  }
  for (; i < 32; i++) {
    (void)spi_xfer(CMD_NOP);
  }

  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
}

void nrf24_init(void) {
  spi_init();
  DEBUG_PRINT("[NRF] Init: 250k 0dBm 2BCRC NO-ACK\r\n");

  Delay_Ms(150); // Increased settling delay

  // Reset state
  nrf_write_reg(REG_CONFIG, 0x00); // Power Down
  Delay_Ms(10);

  nrf_write_reg(REG_CONFIG, 0x0E);     // PWR_UP, ENABLE-CRC (2B), Transmitter
  nrf_write_reg(REG_EN_AA, 0x00);      // RAW LINK: Disable Auto-Ack
  nrf_write_reg(REG_EN_RXADDR, 0x01);  // Enable only Pipe 0
  nrf_write_reg(REG_SETUP_AW, 0x01);   // 3-byte address
  nrf_write_reg(REG_SETUP_RETR, 0x00); // RAW LINK: Disable Retries
  nrf_write_reg(REG_RF_CH, 99);        // Channel 99
  nrf_write_reg(REG_RF_SETUP, 0x06);   // 1Mbps, 0dBm (MAX power, best clone compat)
  nrf_write_reg(REG_RX_PW_P0, 32);     // Payload 32B

  // Flag cleaning
  nrf_write_reg(REG_STATUS, STATUS_ALL_IRQ); // Clear all flags
  nrf_strobe(CMD_FLUSH_TX);
  nrf_strobe(CMD_FLUSH_RX);

  // Verify registers after init
#if DEBUG_ENABLE
  uint8_t cfg = nrf_read_reg(REG_CONFIG);
  uint8_t ch = nrf_read_reg(REG_RF_CH);
  uint8_t rf = nrf_read_reg(REG_RF_SETUP);
  DEBUG_PRINT("[NRF] CFG=%02X CH=%d RF=%02X\r\n",cfg, ch, rf);
#endif
}

void nrf24_set_tx_addr(const uint8_t *addr) {
  DEBUG_PRINT("[NRF] TXaddr: %02X%02X%02X\r\n", addr[0],
              addr[1], addr[2]);
  nrf_write_buf(REG_TX_ADDR, addr, 3);
  nrf_write_buf(REG_RX_ADDR_P0, addr, 3);
}

void nrf24_set_rx_addr(const uint8_t *addr) {
  DEBUG_PRINT("[NRF] RXaddr set\r\n");
  nrf_write_buf(REG_RX_ADDR_P0, addr, 3);
  nrf_write_reg(REG_RX_PW_P0, 32);
}

void nrf24_power_up_tx(void) {
  GPIO_ResetBits(GPIOD, NRF_CE_PIN); // CE LOW before mode switch!
  nrf_write_reg(REG_CONFIG, 0x00);   // Power down for PLL re-lock (clone fix)
  Delay_Ms(2);
  nrf_write_reg(REG_CONFIG, 0x0E);   // PWR_UP, PTX, 2B CRC
  Delay_Ms(2);
}

void nrf24_power_up_rx(void) {
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);  // CE LOW first

  // Full power-down cycle for PLL re-lock (clone NRF24 fix)
  nrf_write_reg(REG_CONFIG, 0x00);     // Power down completely
  Delay_Ms(2);

  // Re-write essential registers for clean state
  nrf_write_reg(REG_EN_AA, 0x00);
  nrf_write_reg(REG_RF_CH, 99);
  nrf_write_reg(REG_RF_SETUP, 0x06);
  nrf_write_reg(REG_RX_PW_P0, 32);

  // Clear flags + flush FIFOs
  nrf_write_reg(REG_STATUS, STATUS_ALL_IRQ);
  nrf_strobe(CMD_FLUSH_TX);
  nrf_strobe(CMD_FLUSH_RX);
  g_rpd_latched = 0;

  // Power up in RX mode
  nrf_write_reg(REG_CONFIG, 0x0F);    // PWR_UP + PRIM_RX + CRC 2B
  Delay_Ms(2);                         // Oscillator + PLL lock
  GPIO_SetBits(GPIOD, NRF_CE_PIN);   // Start listening
  Delay_Ms(1);
}

bool nrf24_send(const uint8_t *data, uint8_t len) {
  // 1. CE LOW (critical for clean TX trigger)
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);

  // 2. Clear flags and flush TX FIFO
  nrf_write_reg(REG_STATUS, STATUS_ALL_IRQ);
  nrf_strobe(CMD_FLUSH_TX);

  // 3. Write Payload
  nrf_write_payload(data, len);

  // 4. CE pulse (LOW->HIGH edge triggers TX)
  GPIO_SetBits(GPIOD, NRF_CE_PIN);
  Delay_Ms(1);
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);

  // 5. Wait for TX complete, clear flags
  Delay_Ms(2);
  nrf_write_reg(REG_STATUS, STATUS_TX_DS | STATUS_MAX_RT);

  return true;
}

bool nrf24_available(void) {
  if (nrf_read_reg(REG_RPD) & 0x01) {
    g_rpd_latched = 1;
  }

  uint8_t status = nrf_read_reg(REG_STATUS);
  if (status & STATUS_RX_DR) {
    DEBUG_PRINT("[NRF] RX_DR! S:0x%02X\r\n",
               status);
    return true;
  }

  // Clone fallback: sometimes RX_DR doesn't assert, but FIFO still has data.
  if ((nrf_read_reg(REG_FIFO_STATUS) & FIFO_RX_EMPTY) == 0) {
    return true;
  }

  return false;
}

void nrf24_read(uint8_t *data, uint8_t len) {
  nrf_read_payload(data, len);
  nrf_write_reg(REG_STATUS, STATUS_RX_DR); // Clear RX_DR

  // Hex dump of received packet
  DEBUG_PRINT("[NRF] RX: ");
  for (uint8_t i = 0; i < 8; i++)
    DEBUG_PRINT("%02X ", data[i]);
  DEBUG_PRINT("\r\n");
}

void nrf24_flush_rx(void) {
  GPIO_ResetBits(GPIOD, NRF_CE_PIN); // CE low during flush
  nrf_write_reg(REG_STATUS, STATUS_ALL_IRQ); // Clear all status flags
  nrf_strobe(CMD_FLUSH_RX);
  nrf_strobe(CMD_FLUSH_TX);
}

void nrf24_power_down(void) {
  DEBUG_PRINT("[NRF] PwrDn\r\n");
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);
  nrf_write_reg(REG_CONFIG, 0x08);
}

bool nrf24_take_rpd_latched(void) {
  bool seen = (g_rpd_latched != 0);
  g_rpd_latched = 0;
  return seen;
}

uint8_t nrf24_get_status(void) { return nrf_read_reg(0x07); }
uint8_t nrf24_get_observe_tx(void) { return nrf_read_reg(0x08); }
uint8_t nrf24_get_setup(void) { return nrf_read_reg(0x06); }
