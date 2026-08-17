#ifndef LORA_SX1278_H
#define LORA_SX1278_H

#include "ch32v00x.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize SX1278 LoRa Module on CH32V003 (SPI1 + GPIOs).
 * Configures 433 MHz, 125 kHz BW, SF7, CR 4/5, +17dBm TX Power.
 * @return 0 on success, non-zero on communication error.
 */
uint8_t lora_init(void);

/**
 * Hardware reset SX1278.
 */
void lora_reset(void);

/**
 * Send a raw data packet over LoRa (up to 256 bytes).
 * @param buf Data buffer
 * @param size Buffer length
 * @return 0 on success, non-zero on timeout.
 */
uint8_t lora_send(const uint8_t *buf, size_t size);

/**
 * Check if a packet is available to read in FIFO.
 */
bool lora_available(void);

/**
 * Read received packet (e.g. Pairing / Data ACK from controller).
 * @param buf Output buffer
 * @param max_len Max buffer capacity
 * @return Number of bytes received.
 */
uint8_t lora_read(uint8_t *buf, uint8_t max_len);

/**
 * Put SX1278 into Continuous RX Mode.
 */
void lora_power_up_rx(void);

/**
 * Put SX1278 into Standby Mode.
 */
void lora_power_up_tx(void);

/**
 * Put SX1278 into Ultra-Low Power Sleep mode (~0.2uA).
 */
void lora_power_down(void);

/**
 * Low-level register read/write.
 */
uint8_t lora_read_reg(uint8_t reg);
void lora_write_reg(uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif // LORA_SX1278_H
