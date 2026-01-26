#ifndef CONFIG_H
#define CONFIG_H

#include "ch32v00x.h"
#include <stdint.h>


// SPI1 Pins for NRF24L01
#define NRF_SCK_PIN GPIO_Pin_5  // PC5
#define NRF_MOSI_PIN GPIO_Pin_6 // PC6
#define NRF_MISO_PIN GPIO_Pin_7 // PC7
#define NRF_CSN_PIN GPIO_Pin_3  // PC3
#define NRF_CE_PIN GPIO_Pin_4   // PD4

// User Interface
#define BUTTON_PIN GPIO_Pin_6 // PD6

// Timing
#define LONG_PRESS_TIME_MS 3000
#define PAIRING_TIMEOUT_MS 30000
#define DATA_SEND_INTERVAL_MS 5000

// Protocol Constants - 3-byte address for pairing
extern const uint8_t PAIRING_ADDR[3];
#define DATA_ADDR_BASE "TANK" // Will append slot/ID

#endif
