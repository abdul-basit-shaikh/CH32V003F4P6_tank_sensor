#ifndef CONFIG_H
#define CONFIG_H

#include "ch32v00x.h"
#include <stdint.h>
#include <stdio.h>

// Debugging Switch
#define DEBUG_ENABLE 1 // Set to 0 to turn off all serial prints

#if DEBUG_ENABLE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

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

// Sensor Logic Constants
#define BATTERY_LOW_THRESHOLD 25 // Warn at ~2.85V for the 2.7V-3.3V range
#define SENSOR_ERROR_VAL 0xFE

// Battery Monitoring
#define BATTERY_ADC_PIN GPIO_Pin_1 // PA1
#define BATTERY_ADC_CHANNEL ADC_Channel_1

// --- Battery Calibration Macros (Change these after PCB design) ---
/*
 * Connection Diagram:
 * [Battery +] --- [BAT_RESISTOR_UP] ---+--- PA1 (ADC Pin)
 *                                      |
 *                              [BAT_RESISTOR_DOWN]
 *                                      |
 *                                    [GND]
 */
#define BAT_RESISTOR_UP 100.0f   // R1 (from Battery to PA1) in kOhm
#define BAT_RESISTOR_DOWN 100.0f // R2 (from PA1 to GND) in kOhm
#define BAT_MIN_VOLTS 2.7f       // 0% battery voltage for this 3.3V pack
#define BAT_MAX_VOLTS 3.3f       // 100% battery voltage for this 3.3V pack
#define BAT_MIN_MV 2700UL
#define BAT_MAX_MV 3300UL
// --- ----------------------------------------------------------- ---

// Timing
#define BOOT_SAFETY_DELAY_MS 5000 
#define RESET_PRESS_TIME_MS 5000
#define PAIRING_TIME_MINS 1
#define PAIRING_INTERVAL_MS_DELAY 50
#define PAIRING_BURST_COUNT                                                    \
  ((PAIRING_TIME_MINS * 60 * 1000) / PAIRING_INTERVAL_MS_DELAY)

// Heartbeat interval in hours (Formula: Hours * 60min * 60sec * 1000ms)
// Deep Sleep Settings
#define HEARTBEAT_HOURS 4
#define AWU_SLEEP_SEC 15 // Wake up every 15 seconds
#define HEARTBEAT_CYCLES ((HEARTBEAT_HOURS * 3600) / AWU_SLEEP_SEC)

// Protocol Constants - 3-byte address for pairing
extern const uint8_t PAIRING_ADDR[3];

// Pairing ACK Configuration
#define PAIRING_ACK_WAIT_MS 500 // Time to listen for controller ACK after each TX
#define PAIRING_ACK_STATUS_FAILED 0x00
#define PAIRING_ACK_STATUS_PAIRED 0x01
#define PAIRING_ACK_STATUS_TIMEOUT 0x02
#define PAIRING_ACK_STATUS_SLOTS_FULL 0x03
#define PAIRING_ACK_STATUS_ALREADY_PAIRED 0x04
#define PKT_TYPE_PAIRING_RESP 0x07

// Data ACK Configuration
#define DATA_ACK_WAIT_MS 300    // Time to listen for data ACK after each TX
#define DATA_MAX_RETRIES 15     // Max retries if no ACK received
#define DATA_RETRY_DELAY_MS 100 // Delay between retries
#define PKT_TYPE_DATA_ACK 0x03  // ACK packet type from controller

#endif
