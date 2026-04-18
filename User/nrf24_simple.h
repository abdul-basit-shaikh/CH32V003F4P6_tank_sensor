#ifndef NRF24_SIMPLE_H
#define NRF_SIMPLE_H

#include <stdbool.h>
#include <stdint.h>

void nrf24_init(void);
void nrf24_set_tx_addr(const uint8_t *addr);
void nrf24_set_rx_addr(const uint8_t *addr);
bool nrf24_send(uint8_t *data, uint8_t len);
bool nrf24_available(void);
void nrf24_read(uint8_t *data, uint8_t len);
void nrf24_power_up_tx(void);
void nrf24_power_up_rx(void);
void nrf24_power_down(void);
void nrf24_flush_rx(void);
uint8_t nrf24_get_status(void);
uint8_t nrf24_get_observe_tx(void);
uint8_t nrf24_get_setup(void);
uint8_t nrf_read_reg(uint8_t reg);

#endif
