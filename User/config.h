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
#define LED_PIN GPIO_Pin_2    // PD2

// Water Level Probe Pins (Input with Pull-up)
#define SENSOR_PIN_25 GPIO_Pin_0  // PC0
#define SENSOR_PIN_50 GPIO_Pin_1  // PC1
#define SENSOR_PIN_75 GPIO_Pin_2  // PC2
#define SENSOR_PIN_100 GPIO_Pin_4 // PC4

// Battery Monitoring
#define BATTERY_ADC_PIN GPIO_Pin_1 // PA1
#define BATTERY_ADC_CHANNEL ADC_Channel_1

// Timing
#define RESET_PRESS_TIME_MS 5000 // 5s  -> Factory Reset (Un-pair) Pairing
#define PAIRING_TIME_MINS 1      // Set pairing duration in minutes
#define PAIRING_BURST_COUNT ((PAIRING_TIME_MINS * 60 * 1000) / 50)
#define DATA_SEND_INTERVAL_MS 5000

// Heartbeat interval in hours (Formula: Hours * 60min * 60sec * 1000ms)
#define HEARTBEAT_HOURS 4
#define HEARTBEAT_INTERVAL_MS (HEARTBEAT_HOURS * 60UL * 60UL * 1000UL)

// Protocol Constants - 3-byte address for pairing
extern const uint8_t PAIRING_ADDR[3];

#endif
