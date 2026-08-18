#ifndef CONFIG_H
#define CONFIG_H

#include "ch32v00x.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


// Debugging Switch
#define DEBUG_ENABLE        1 // Set to 0 to turn off all serial prints

#if DEBUG_ENABLE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

// =============================================================
// WIRELESS RADIO HARDWARE SELECTION
// =============================================================
// 0 = NRF24L01 (2.4 GHz)
// 1 = SX1278 LoRa Ra-02 (433 MHz)
#define USE_WIRELESS_LORA       1

// SPI1 Pins for NRF24L01 / SX1278 LoRa
#define NRF_SCK_PIN             GPIO_Pin_5  // PC5
#define NRF_MOSI_PIN            GPIO_Pin_6 // PC6
#define NRF_MISO_PIN            GPIO_Pin_7 // PC7
#define NRF_CSN_PIN             GPIO_Pin_3  // PC3
#define NRF_CE_PIN              GPIO_Pin_4   // PD4

// SX1278 LoRa Specific Pin Aliases
#define LORA_SCK_PIN            GPIO_Pin_5  // PC5 (SPI1_SCK)
#define LORA_MOSI_PIN           GPIO_Pin_6 // PC6 (SPI1_MOSI)
#define LORA_MISO_PIN           GPIO_Pin_7 // PC7 (SPI1_MISO)
#define LORA_NSS_PIN            GPIO_Pin_3  // PC3 (NSS / CS)
#define LORA_RST_PIN            GPIO_Pin_4  // PD4 (Reset)
#define LORA_FREQUENCY          433000000 // 433 MHz

// User Interface
#define BUTTON_PIN              GPIO_Pin_6 // PD6
#define LED_PIN                 GPIO_Pin_2    // PD2

// Water Level Probe Pins (Input with Pull-up)
#define SENSOR_PIN_25           GPIO_Pin_0  // PC0
#define SENSOR_PIN_50           GPIO_Pin_1  // PC1
#define SENSOR_PIN_75           GPIO_Pin_2  // PC2
#define SENSOR_PIN_100          GPIO_Pin_4 // PC4

// Water Level Probe Common Power Pin (Pulsed 2ms Drive for Ultra-Low Power &
// Zero Corrosion)
#define SENSOR_POWER_PIN        GPIO_Pin_3 // PD3 (Pin 20)

// Sensor Logic Constants
#define LEVEL_DEBOUNCE_CYCLES   2  // Consecutive wake cycles required to confirm level change (~10s anti-slosh)
#define PROBE_SAMPLE_COUNT      5  // Number of multi-samples per reading
#define PROBE_MAJORITY_VOTE     3  // >= 3 out of 5 (> 50% true majority) to declare probe wet
#define PROBE_SETTLE_DELAY_MS   10 // Robust 10ms probe stabilization (handles long wires & low TDS water)
#define BATTERY_LOW_THRESHOLD   20 // Warn at <= 20% (approx 2.88V) 
#define SENSOR_ERROR_VAL        0xFE

// =============================================================
// BATTERY MONITORING CONFIGURATION
// =============================================================
// 1 = Use CH32V003 Internal 1.20V Bandgap (Zero-Drain 0uA, No Resistors Needed on PCB) [RECOMMENDED]
// 0 = Use External Resistor Divider on PA1 (100k + 100k)
#define USE_INTERNAL_VREF_BATTERY   1

#define BATTERY_ADC_PIN         GPIO_Pin_1 // PA1
#define BATTERY_ADC_CHANNEL     ADC_Channel_1

// --- Battery Calibration Macros for External Resistor Divider Mode (When USE_INTERNAL_VREF_BATTERY = 0) ---
/*
 * Connection Diagram (Legacy Mode):
 * [Battery +] --- [BAT_RESISTOR_UP] ---+--- PA1 (ADC Pin)
 *                                      |
 *                              [BAT_RESISTOR_DOWN]
 *                                      |
 *                                    [GND]
 */
#define BAT_RESISTOR_UP         100000UL // = 100,000 ohm = 100kΩ R1 (from Battery to PA1)
#define BAT_RESISTOR_DOWN       100000UL // = 100,000 ohm = 100kΩ R2 (from PA1 to GND)
#define BAT_VOLTAGE_SCALE       ((1200UL * (BAT_RESISTOR_UP + BAT_RESISTOR_DOWN)) / BAT_RESISTOR_DOWN)

#define BAT_MIN_MV              2700UL // 0% battery voltage in millivolts
#define BAT_MAX_MV              3300UL // 100% battery voltage in millivolts
// --- ----------------------------------------------------------- ---

// Heartbeat interval in hours (Formula: Hours * 60min * 60sec * 1000ms)
// Deep Sleep Settings
#define HEARTBEAT_HOURS         4
#define AWU_SLEEP_SEC           5 // Wake up every ~5 seconds (matches actual 5s AWU sleep window)
#define HEARTBEAT_CYCLES        ((HEARTBEAT_HOURS * 3600) / AWU_SLEEP_SEC)

// Protocol Constants - 3-byte address for pairing
extern const uint8_t PAIRING_ADDR[3];

// Pairing ACK Configuration
#define PAIRING_ACK_WAIT_MS                     800 // Time to listen for controller ACK after each TX
#define PAIRING_ACK_STATUS_FAILED               0x00
#define PAIRING_ACK_STATUS_PAIRED               0x01
#define PAIRING_ACK_STATUS_TIMEOUT              0x02
#define PAIRING_ACK_STATUS_SLOTS_FULL           0x03
#define PAIRING_ACK_STATUS_ALREADY_PAIRED       0x04
#define PKT_TYPE_PAIRING_RESP                   0x07

// Data ACK Configuration
#define DATA_ACK_WAIT_MS                        400 // Time to listen for data ACK after each TX
#define DATA_MAX_RETRIES                        8   // Max retries if no ACK received (balanced for battery & range)
#define DATA_RETRY_DELAY_MS                     120 // Base delay between retries
#define PKT_TYPE_DATA_ACK                       0x03  // ACK packet type from controller

// Timing
#define BOOT_SAFETY_DELAY_MS                    5000
#define REBOOT_PRESS_TIME_MS                    3000 // 3s hold -> Device Reboot
#define RESET_PRESS_TIME_MS                     5000 // 5s hold -> Factory Reset & Pairing Mode
#define PAIRING_TIME_MINS                       1
#define PAIRING_BURST_COUNT                     ((PAIRING_TIME_MINS * 60 * 1000) / PAIRING_ACK_WAIT_MS)

// Hardware Watchdog (IWDG) Configuration
#define ENABLE_HARDWARE_IWDG                    1    // 1 = Enabled (~10s hardware timeout), 0 = Disabled
#define IWDG_RELOAD_VALUE                       4095 // 4095 (Max 12-bit 0x0FFF) * 256 / 100-128kHz LSI = ~8.2s-10.0s timeout

#endif
