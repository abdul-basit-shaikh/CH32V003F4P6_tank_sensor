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

// Sensor Logic Constants
#define SENSOR_STABLE_COUNT 2 // 2 cycles x 15s = 30s total stability
#define BATTERY_LOW_THRESHOLD 15
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
#define BAT_MIN_VOLTS 3.0f       // 0% Battery Voltage (e.g., 3.0V for Li-ion)
#define BAT_MAX_VOLTS 4.2f       // 100% Battery Voltage (e.g., 4.2V for Li-ion)
// --- ----------------------------------------------------------- ---

// Timing
#define RESET_PRESS_TIME_MS 5000
#define PAIRING_TIME_MINS 1
#define PAIRING_BURST_COUNT ((PAIRING_TIME_MINS * 60 * 1000) / 50)

// Heartbeat interval in hours (Formula: Hours * 60min * 60sec * 1000ms)
#define HEARTBEAT_HOURS 4
#define HEARTBEAT_INTERVAL_MS (HEARTBEAT_HOURS * 60UL * 60UL * 1000UL)

// Deep Sleep Settings
#define AWU_SLEEP_SEC 15 // Wake up every 15 seconds
#define HEARTBEAT_CYCLES ((HEARTBEAT_HOURS * 3600) / AWU_SLEEP_SEC)

// Protocol Constants - 3-byte address for pairing
extern const uint8_t PAIRING_ADDR[3];

#endif
