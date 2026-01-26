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
#define REG_RX_ADDR_P0 0x0A
#define REG_TX_ADDR 0x10
#define REG_RX_PW_P0 0x11
#define REG_FIFO_STATUS 0x17

// Commands
#define CMD_R_REG 0x00
#define CMD_W_REG 0x20
#define CMD_R_RX_PL 0x61
#define CMD_W_TX_PL 0xA0
#define CMD_FLUSH_TX 0xE1
#define CMD_FLUSH_RX 0xE2
#define CMD_NOP 0xFF

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
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
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

void nrf24_init(void) {
  spi_init();
  printf("[RADIO] Configuring Hardware (250kbps, -12dBm, NO-ACK)...\r\n");

  Delay_Ms(150); // Increased settling delay

// Temporarily enable debug for init
#define INIT_DEBUG 1
#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x00 -> VAL 0x02\r\n");
#endif
  nrf_write_reg(REG_CONFIG, 0x02); // PWR_UP, NO-CRC, Transmitter

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x01 -> VAL 0x00\r\n");
#endif
  nrf_write_reg(REG_EN_AA, 0x00); // RAW LINK: Disable Auto-Ack

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x03 -> VAL 0x01\r\n");
#endif
  nrf_write_reg(REG_SETUP_AW, 0x01); // 3-byte address (E7 E7 E7)

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x04 -> VAL 0x00\r\n");
#endif
  nrf_write_reg(REG_SETUP_RETR, 0x00); // RAW LINK: Disable Retries

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x05 -> VAL 0x63\r\n");
#endif
  nrf_write_reg(REG_RF_CH, 99); // Channel 99 - matches receiver

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x06 -> VAL 0x26\r\n");
#endif
  nrf_write_reg(REG_RF_SETUP, 0x26); // 250kbps, 0dBm (MAX POWER)

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x11 -> VAL 0x20\r\n");
#endif
  nrf_write_reg(REG_RX_PW_P0, 32); // Payload 32B

#if INIT_DEBUG
  printf("[SPI] WRITE REG 0x07 -> VAL 0x70\r\n");
#endif
  nrf_write_reg(0x07, 0x70); // Clear flags
  printf("[RADIO] Hardware Ready (SAFE-RAW).\r\n");
}

void nrf24_set_tx_addr(const uint8_t *addr) {
  printf("[RADIO] Targeting Address (3-Byte): %02X %02X %02X\r\n", addr[0],
         addr[1], addr[2]);
  nrf_write_buf(REG_TX_ADDR, addr, 3);
  nrf_write_buf(REG_RX_ADDR_P0, addr, 3);
}

void nrf24_set_rx_addr(const uint8_t *addr) {
  printf("[RADIO] Setting RX Address...\r\n");
  nrf_write_buf(REG_RX_ADDR_P0, addr, 5);
  nrf_write_reg(REG_RX_PW_P0, 32);
}

void nrf24_power_up_tx(void) {
  printf("[RADIO] >>> POWER_UP: TRANSMITTER MODE (NO-CRC) <<<\r\n");
  nrf_write_reg(0x00, 0x02); // PWR_UP, Transmitter, No-CRC
  Delay_Ms(5);
}

void nrf24_power_up_rx(void) {
  printf("[RADIO] >>> POWER_UP: RECEIVER MODE <<<\r\n");
  nrf_write_reg(0x00, 0x0F);
  GPIO_SetBits(GPIOD, NRF_CE_PIN);
  Delay_Ms(5);
}

bool nrf24_send(uint8_t *data, uint8_t len) {
  // Reduced logging to prevent UART overflow
  // printf("[RADIO] >>> DATA DISPATCH: 32 BYTES START <<<\r\n");

  // Delay_Ms(10); // Removed - not needed
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_FLUSH_TX);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
  // printf("[SPI] CMD: FLUSH_TX\r\n");

  // printf("[SPI] CMD: W_TX_PAYLOAD (32B): ");
  // for (int i = 0; i < 32; i++)
  //   printf("%02X ", i < len ? data[i] : 0);
  // printf("\r\n");

  // Delay_Ms(10); // Removed - not needed
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_W_TX_PL);
  for (uint8_t i = 0; i < 32; i++)
    spi_xfer(i < len ? data[i] : 0);
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
  // printf("[SPI] W_TX_PAYLOAD: Dispatch complete.\r\n");

  // printf("[GPIO] CE HIGH -> Start TX Pulse\r\n");
  GPIO_SetBits(GPIOD, NRF_CE_PIN);
  Delay_Ms(1); // Short pulse for transmission
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);
  // printf("[GPIO] CE LOW -> End TX Pulse\r\n");

  // NO-ACK MODE: Just wait for TX to complete (no ACK expected)
  // In NO-ACK mode, packet is sent immediately without waiting for ACK
  Delay_Ms(2); // Small delay for transmission to complete

  // Clear any flags
  nrf_write_reg(0x07, 0x30); // Clear TX_DS and MAX_RT flags

  // printf("[RADIO] TX COMPLETE (NO-ACK MODE)\r\n");
  return true; // Always return success in NO-ACK mode
}

bool nrf24_available(void) {
  uint8_t status = nrf_read_reg(0x07);
  printf("[RADIO] >>> DATA ARRIVED: RX Payload detected <<<\r\n");
  printf("status %02X\r\n", status);
  if (status & 0x40) {
    printf("[RADIO] >>> DATA ARRIVED: RX Payload detected <<<\r\n");
    return true;
  }
  return false;
}

void nrf24_read(uint8_t *data, uint8_t len) {
  printf("[RADIO] Reading RX Payload...\r\n");
  GPIO_ResetBits(GPIOC, NRF_CSN_PIN);
  spi_xfer(CMD_R_RX_PL);
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t b = spi_xfer(CMD_NOP);
    if (i < len)
      data[i] = b;
  }
  GPIO_SetBits(GPIOC, NRF_CSN_PIN);
  nrf_write_reg(0x07, 0x40); // Clear RX_DR
}

void nrf24_power_down(void) {
  printf("[RADIO] Powering DOWN...\r\n");
  GPIO_ResetBits(GPIOD, NRF_CE_PIN);
  nrf_write_reg(REG_CONFIG, 0x08);
}

uint8_t nrf24_get_status(void) { return nrf_read_reg(0x07); }
uint8_t nrf24_get_observe_tx(void) { return nrf_read_reg(0x08); }
uint8_t nrf24_get_setup(void) { return nrf_read_reg(0x06); }
