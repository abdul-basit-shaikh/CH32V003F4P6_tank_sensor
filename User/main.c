#include "ch32v00x.h"
#include "ch32v00x_exti.h"
#include "ch32v00x_flash.h"
#include "ch32v00x_gpio.h"
#include "ch32v00x_misc.h"
#include "ch32v00x_pwr.h"
#include "ch32v00x_rcc.h"
#include "ch32v00x_tim.h"
#include "ch32v00x_usart.h"
#include "config.h"
#include "debug.h"
#include "nrf24_simple.h"
#include <stdint.h>
#include <string.h>

/* ========== Power Management & AWU ========== */
void pwr_sleep_init(void) {
  // Enable PWR clock
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

  // Configure AWU (Auto Wake-up)
  RCC_LSICmd(ENABLE);
  while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET)
    ;

  // Set AWU prescaler and window for ~10s wake-up (Faster response)
  PWR_AWU_SetPrescaler(PWR_AWU_Prescaler_61440);
  PWR_AWU_SetWindowValue(8);
  PWR_AutoWakeUpCmd(ENABLE);

  // Enable AWU interrupt in NVIC
  NVIC_InitTypeDef NVIC_InitStructure = {0};
  NVIC_InitStructure.NVIC_IRQChannel = AWU_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void AWU_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void AWU_IRQHandler(void) {
  // AWU does not have a specific IT pending bit to clear in most V003 versions,
  // it's cleared by hardware or by the wake-up process.
}

void exti_init(void) {
  // Enable AFIO clock BEFORE configuring line mapping
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

  EXTI_InitTypeDef EXTI_InitStructure = {0};

  // Sensor Pins on Port C: PC0, PC1, PC2, PC4
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource0);
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource1);
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource2);
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource4);

  EXTI_InitStructure.EXTI_Line =
      EXTI_Line0 | EXTI_Line1 | EXTI_Line2 | EXTI_Line4;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  // Button Pin on Port D: PD6
  GPIO_EXTILineConfig(GPIO_PortSourceGPIOD, GPIO_PinSource6);
  EXTI_InitStructure.EXTI_Line = EXTI_Line6;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
  EXTI_Init(&EXTI_InitStructure);

  // Enable NVIC for EXTI Line 7-0
  NVIC_InitTypeDef NVIC_InitStructure = {0};
  NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
  // Clear all pending flags for sensor pins and button
  EXTI_ClearITPendingBit(EXTI_Line0 | EXTI_Line1 | EXTI_Line2 | EXTI_Line4 |
                         EXTI_Line6);
}

void enter_deep_sleep(void) {
  DEBUG_PRINT("[PWR] Entering Deep Sleep...\r\n");
  Delay_Ms(50); // Increased UART flush time

  // Clear pending flags for a clean start
  EXTI->INTFR = 0xFFFFFFFF;

  // Use "Stop" mode (SLEEPDEEP=1, PDDS=0)
  // This mode allows EXTI (Sensor changes) to wake up the MCU
  PWR->CTLR &= ~PWR_CTLR_PDDS; // PDDS = 0
  NVIC->SCTLR |= (1 << 2);     // Set SLEEPDEEP

  __WFI(); // Wait for Interrupt (AWU or EXTI)

  NVIC->SCTLR &= ~(1 << 2); // Clear SLEEPDEEP after wakeup

  // System resumes here after wake up
  SystemCoreClockUpdate();

  // Clear flags again after waking to prevent fast loops
  EXTI->INTFR = 0xFFFFFFFF;

  DEBUG_PRINT("[PWR] Woke UP.\r\n");

  // Visual confirmation of wakeup
  GPIO_ResetBits(GPIOD, LED_PIN); // Blink ON
  Delay_Ms(50);
  GPIO_SetBits(GPIOD, LED_PIN); // Blink OFF

  // Essential re-init after deep sleep
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOC |
                             RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1 |
                             RCC_APB2Periph_AFIO,
                         ENABLE);
  gpio_init(); // Re-init all GPIO modes correctly (also calls exti_init)
  adc_init();  // Re-calibrate ADC after wake up
  exti_init(); // Re-enable EXTI wake triggers

  Delay_Ms(200); // Stabilization delay
}

/* ========== Flash Storage (Last Page 64-byte) ========== */
#define SETTINGS_FLASH_ADDR 0x08003FC0
typedef struct {
  uint16_t tank_id;
  uint8_t pairing_status; // 1 = Paired, 0 = Not Paired
} sensor_settings_t;

sensor_settings_t g_settings;

void flash_read_settings(void) {
  memcpy(&g_settings, (void *)SETTINGS_FLASH_ADDR, sizeof(sensor_settings_t));

  // Check if data is valid (not empty flash 0xFF)
  if (g_settings.tank_id == 0xFFFF) {
    g_settings.tank_id = 0;
    g_settings.pairing_status = 0;
  }
}

void flash_save_settings(void) {
  FLASH_Unlock();
  FLASH_ErasePage(SETTINGS_FLASH_ADDR);

  uint32_t *pData = (uint32_t *)&g_settings;
  for (uint16_t i = 0; i < (sizeof(sensor_settings_t) + 3) / 4; i++) {
    FLASH_ProgramWord(SETTINGS_FLASH_ADDR + (i * 4), pData[i]);
  }
  FLASH_Lock();
  DEBUG_PRINT("[FLASH] Settings Saved. ID: 0x%04X, Status: %d\r\n",
              g_settings.tank_id, g_settings.pairing_status);
}

/* ========== Global Variables ========== */
const uint8_t PAIRING_ADDR[3] = {0xE7, 0xE7,
                                 0xE7}; // Address for pairing packets
volatile uint32_t g_millis = 0;         // Current system time in ms
uint32_t g_last_send = 0;               // Time of last data transmission
uint8_t g_probe_fault = 0; // 1 = Fault detected (e.g., gap in readings)

/* ========== TIM2 for millis (1ms) ========== */
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) {
  if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
    g_millis++;
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
  }
}

void timer_init(void) {
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
  NVIC_InitTypeDef NVIC_InitStructure = {0};
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

  // Period 9 and Prescaler for 1ms interrupt
  TIM_TimeBaseStructure.TIM_Period = 9;
  TIM_TimeBaseStructure.TIM_Prescaler = (SystemCoreClock / 10000) - 1;
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

  TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
  NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
  TIM_Cmd(TIM2, ENABLE);
}

uint32_t millis(void) { return g_millis; }

/* ========== GPIO Init ========== */
void gpio_init(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOA, ENABLE);

  // Button: PD6 (Input Pull-up)
  GPIO_InitStructure.GPIO_Pin = BUTTON_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  // LED: PD2 (Output Push-pull)
  GPIO_InitStructure.GPIO_Pin = LED_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOD, &GPIO_InitStructure);

  GPIO_SetBits(GPIOD, LED_PIN); // Turn off at start (Active-Low)

  // Sensor Pins: PC0, PC1, PC2, PC4 (Input Pull-up)
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
  GPIO_InitStructure.GPIO_Pin =
      SENSOR_PIN_25 | SENSOR_PIN_50 | SENSOR_PIN_75 | SENSOR_PIN_100;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  // Initialize EXTI for these pins
  exti_init();
}

/**
 * @brief Reads all tank probes and applies software patches for hardware
 * reliability and fault monitoring.
 *
 * LOGIC EXPLANATION (Hardware Patch):
 * In a standard water tank, if a higher probe (e.g., 50%) is touching water,
 * all probes below it (e.g., 25%) MUST also be touching water.
 * If a lower probe wire is broken or the probe is oxidized, it might
 * report "Dry" even when the tank is half full.
 *
 * This function handles two critical tasks:
 * 1. PROBE FAULT DETECTION: It checks if there's a "gap" in readings (e.g.,
 *    100% is wet but 25% is dry). If a gap is found, it sets g_probe_fault=1
 *    to alert the user about possible broken wires or dirty sensors.
 *
 * 2. BROKEN WIRE FIX: Even if a lower probe is faulty, the function
 *    automatically fills in the gap in software to ensure the displayed
 *    water level remains accurate.
 *
 * Benefit: Prevents "jumpy" or incorrect readings and provides a "Service
 * Required" alert if hardware maintenance is needed.
 *
 * @return uint8_t Current water level (0, 25, 50, 75, or 100)
 */
uint8_t read_internal_probes(void) {
  uint8_t p25 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_25) == 0);
  uint8_t p50 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_50) == 0);
  uint8_t p75 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_75) == 0);
  uint8_t p100 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_100) == 0);

  // --- FAULT DETECTION LOGIC ---
  // If a higher probe is wet, all lower ones MUST be wet.
  // If not, we have a probe fault (broken wire or oxidation).
  uint8_t current_fault = 0;
  if (p100 && (!p75 || !p50 || !p25))
    current_fault = 1;
  else if (p75 && (!p50 || !p25))
    current_fault = 1;
  else if (p50 && !p25)
    current_fault = 1;

  if (current_fault) {
    g_probe_fault = 1;
    DEBUG_PRINT("[ALERT] Probe Fault Detected! (Gaps in readings)\r\n");
  } else {
    g_probe_fault = 0;
  }

  // --- HARDWARE PATCH: Broken Wire Fix ---
  // If a higher probe is wet, all lower ones MUST be wet in the return value.
  if (p100) {
    p75 = 1;
    p50 = 1;
    p25 = 1;
  } else if (p75) {
    p50 = 1;
    p25 = 1;
  } else if (p50) {
    p25 = 1;
  }

  // DEBUG: Raw probe state after patch
  DEBUG_PRINT("[DEBUG] Probes: 100:%d 75:%d 50:%d 25:%d\r\n", p100, p75, p50,
              p25);

  if (p100)
    return 100;
  if (p75)
    return 75;
  if (p50)
    return 50;
  if (p25)
    return 25;
  return 0;
}

uint8_t read_tank_level(void) {
  uint8_t r1 = read_internal_probes();
  uint8_t r2 = read_internal_probes();
  uint8_t r3 = read_internal_probes();

  // Aggressive Detection: Highest level found in any sample
  uint8_t best = r1;
  if (r2 > best)
    best = r2;
  if (r3 > best)
    best = r3;

  if (best > 0)
    DEBUG_PRINT("[DEBUG] Tank Level Detected: %d%%\r\n", best);
  return best;
}

/* ========== ADC for Battery Monitoring (PA1) ========== */
void adc_init(void) {
  ADC_InitTypeDef ADC_InitStructure = {0};
  GPIO_InitTypeDef GPIO_InitStructure = {0};

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
  RCC_ADCCLKConfig(RCC_PCLK2_Div8);

  GPIO_InitStructure.GPIO_Pin = BATTERY_ADC_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  ADC_DeInit(ADC1);
  ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_InitStructure.ADC_ScanConvMode = DISABLE;
  ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
  ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
  ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
  ADC_InitStructure.ADC_NbrOfChannel = 1;
  ADC_Init(ADC1, &ADC_InitStructure);

  ADC_Cmd(ADC1, ENABLE);

  // Calibration
  ADC_ResetCalibration(ADC1);
  while (ADC_GetResetCalibrationStatus(ADC1))
    ;
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1))
    ;
}

uint16_t get_adc_val(uint8_t ch) {
  ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_241Cycles);
  ADC_SoftwareStartConvCmd(ADC1, ENABLE);
  while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC))
    ;
  return ADC_GetConversionValue(ADC1);
}

uint8_t read_battery_level(void) {
  // 1. Get Internal Reference (1.2V) to calculate actual VDD
  uint32_t vref_sum = 0;
  for (int i = 0; i < 16; i++)
    vref_sum += get_adc_val(ADC_Channel_Vrefint);
  uint16_t vref_raw = vref_sum / 16;

  if (vref_raw == 0)
    return 0; // Guard against division by zero

  // 2. Get Battery Pin Voltage (PA1)
  uint32_t bat_sum = 0;
  for (int i = 0; i < 16; i++)
    bat_sum += get_adc_val(BATTERY_ADC_CHANNEL);
  uint16_t bat_raw = bat_sum / 16;

  // 3. Calculate Battery Voltage using the Universal Formula:
  // V_bat = (ADC_bat * 1.2V * (R_up + R_down)) / (ADC_vref * R_down)
  // We use floats for precision during calibration phase
  float ratio = (BAT_RESISTOR_UP + BAT_RESISTOR_DOWN) / BAT_RESISTOR_DOWN;
  float v_bat = ((float)bat_raw * 1.2f * ratio) / (float)vref_raw;

  DEBUG_PRINT("[BAT] Actual Voltage: %d.%02dV\r\n", (int)v_bat,
              (int)(v_bat * 100) % 100);

  // 4. Map to Percentage
  if (v_bat >= BAT_MAX_VOLTS)
    return 100;
  if (v_bat <= BAT_MIN_VOLTS)
    return 0;

  return (uint8_t)((v_bat - BAT_MIN_VOLTS) * 100.0f /
                   (BAT_MAX_VOLTS - BAT_MIN_VOLTS));
}

/* ========== Auto-Generate Tank ID ========== */
uint16_t generate_new_tank_id(void) {
  // Generate a pseudo-random 16-bit ID using current millis
  // This ensures each reset produces a different ID
  uint32_t seed = millis();

  // Simple pseudo-random algorithm using bit manipulation
  seed = (seed ^ (seed << 13));
  seed = (seed ^ (seed >> 17));
  seed = (seed ^ (seed << 5));

  // Ensure ID is never 0x0000 (reserved for invalid)
  uint16_t new_id = (uint16_t)(seed & 0xFFFF);
  if (new_id == 0x0000) {
    new_id = 0x0001;
  }

  return new_id;
}

/* ========== Protocol Checksum (CRC-8) ========== */
uint8_t calculate_checksum(uint8_t *data, uint8_t length) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < length - 1; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x07; // Polynomial 0x07
      else
        crc <<= 1;
    }
  }
  return crc;
}

void run_pairing(void) {
  nrf24_init();
  Delay_Ms(100);

  uint16_t new_id = generate_new_tank_id();
  g_settings.tank_id = new_id;
  g_settings.pairing_status = 1;
  flash_save_settings();
  DEBUG_PRINT("\r\n[PAIR] New Tank ID: 0x%04X\r\n", new_id);

  uint8_t packet[32] = {0};
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x01; // PKT_TYPE_PAIRING_REQ
  packet[3] = 32;
  packet[4] = (new_id & 0xFF);
  packet[5] = (new_id >> 8);
  packet[6] = read_tank_level();    // Level at index 6
  packet[7] = read_battery_level(); // Battery at index 7
  packet[8] = 0x10;                 // Version at index 8
  packet[31] = calculate_checksum(packet, 32);

  DEBUG_PRINT("[PAIR] Broadcasting for %d min...\r\n", PAIRING_TIME_MINS);
  nrf24_power_up_tx();
  nrf24_set_tx_addr(PAIRING_ADDR);

  for (int i = 0; i < PAIRING_BURST_COUNT; i++) {
    if ((i % 10) < 5)
      GPIO_ResetBits(GPIOD, LED_PIN);
    else
      GPIO_SetBits(GPIOD, LED_PIN);
    nrf24_send(packet, 32);
    Delay_Ms(PAIRING_INTERVAL_MS_DELAY);
  }

  GPIO_SetBits(GPIOD, LED_PIN);
  DEBUG_PRINT(
      "[PAIR] Broadcast complete. Status SAVED. Data will now start.\r\n");
}

/* ========== Main Loop ========== */
int main(void) {
  SystemCoreClockUpdate();
  Delay_Init();
  USART_Printf_Init(115200);

  DEBUG_PRINT("\r\n\r\n=== TANK SENSOR BOOT ===\r\n");
  DEBUG_PRINT("Platform: CH32V003 @ %lu Hz\r\n",
              (unsigned long)SystemCoreClock);

  timer_init();
  gpio_init();
  adc_init();
  pwr_sleep_init();

  // SAFETY: Delay at boot to allow WCH-Link to connect before MCU sleeps.
  // This is the most reliable way to prevent "Lockout" during development.
  DEBUG_PRINT("[SYSTEM] Safety Delay (5s) for Flashing... ");
  Delay_Ms(5000);
  DEBUG_PRINT("Ready.\r\n");

  __enable_irq();

  DEBUG_PRINT("Initializing Radio Hardware...\r\n");
  nrf24_init();
  nrf24_power_down(); // Start in low power mode

  // Load settings from Flash
  flash_read_settings();

  if (g_settings.pairing_status == 1) {
    DEBUG_PRINT("[SYSTEM] Paired Sensor. Loaded ID: 0x%04X\r\n",
                g_settings.tank_id);
    nrf24_set_tx_addr(PAIRING_ADDR); // Ensure address is set
  } else {
    DEBUG_PRINT(
        "[SYSTEM] NOT PAIRED. Waiting for user to trigger pairing...\r\n");
  }

  DEBUG_PRINT("[SYSTEM] Clock Validation: 1000ms delay...");
  uint32_t t_start = millis();
  Delay_Ms(1000);
  DEBUG_PRINT(" DONE. Millis Diff: %lu ms\r\n",
              (unsigned long)(millis() - t_start));

  // Boot UI: Triple blink (500ms ON, 500ms OFF)
  for (int i = 0; i < 3; i++) {
    GPIO_ResetBits(GPIOD, LED_PIN); // ON
    Delay_Ms(500);
    GPIO_SetBits(GPIOD, LED_PIN); // OFF
    Delay_Ms(500);
  }
  GPIO_SetBits(GPIOD, LED_PIN); // Extra safety: ensure LED is OFF

  DEBUG_PRINT("[SYSTEM] Entering Idle Mode. (Hold button for pairing)\r\n");

  uint32_t button_press_start = 0;
  bool pairing_triggered = false;
  uint16_t sleep_cycle_count = 0;

  // Trackers (Must be reset on pairing)
  uint32_t seq_num = 0;
  uint8_t last_sent_level = 0xFF;
  uint8_t filtered_level = 0;
  uint8_t stable_counter = 0;
  bool first_reading_done = false;
  uint8_t current_battery = 0;

  while (1) {
    // 1. Button Handling
    if (GPIO_ReadInputDataBit(GPIOD, BUTTON_PIN) == 0) {
      if (button_press_start == 0) {
        button_press_start = millis();
        DEBUG_PRINT("[SYSTEM] Button Pressed...\r\n");
      }

      uint32_t held_ms = millis() - button_press_start;

      // --- FACTORY RESET (Long Press >= 5s) ---
      if (!pairing_triggered && held_ms > RESET_PRESS_TIME_MS) {
        DEBUG_PRINT(
            "[SYSTEM] *** FACTORY RESET TRIGGERED (Long Press) ***\r\n");
        pairing_triggered = true;

        // Sync Unpair with Receiver before reset
        if (g_settings.pairing_status == 1 && g_settings.tank_id != 0) {
          DEBUG_PRINT(
              "[PWR] Sending UNPAIR packet to receiver before reset...\r\n");
          uint8_t unpair_pkt[32] = {0};
          unpair_pkt[0] = 0xAA;
          unpair_pkt[1] = 0x55;
          unpair_pkt[2] = 0x06; // PKT_TYPE_UNPAIR
          unpair_pkt[3] = 32;
          unpair_pkt[4] = (uint8_t)(g_settings.tank_id & 0xFF);
          unpair_pkt[5] = (uint8_t)(g_settings.tank_id >> 8);
          unpair_pkt[31] = calculate_checksum(unpair_pkt, 32);

          nrf24_power_up_tx();
          nrf24_set_tx_addr(PAIRING_ADDR);
          for (int i = 0; i < 20; i++) {
            nrf24_send(unpair_pkt, 32);
            Delay_Ms(30);
          }
          nrf24_power_down();
        }

        g_settings.tank_id = 0;
        g_settings.pairing_status = 0;
        flash_save_settings();

        // Reset local trackers
        seq_num = 0;
        last_sent_level = 0xFF;
        first_reading_done = false;

        run_pairing(); // Enters pairing mode broadcast for 30s
      }
      Delay_Ms(10);
      continue; // Don't sleep while button is held
    } else {
      // Button Released
      if (button_press_start != 0) {
        uint32_t held_ms = millis() - button_press_start;

        // --- RESTART (Single Click < 5s) ---
        if (!pairing_triggered && held_ms > 10 &&
            held_ms < RESET_PRESS_TIME_MS) {
          DEBUG_PRINT(
              "[SYSTEM] Single Click (%lu ms) -> Restarting Device...\r\n",
              (unsigned long)held_ms);

          Delay_Ms(100);

          // Software System Reset
          NVIC->SCTLR |= (1 << 31); // SYSRESETREQ (Bit 31)
        }

        button_press_start = 0;
        pairing_triggered = false;
      }
    }

    // 2. Data Logic (Only if paired)
    if (g_settings.pairing_status == 1) {
      uint8_t raw_level = read_tank_level();

      // --- Water Ripple Filter (Stable Level) ---
      if (!first_reading_done) {
        // First time after boot: Bypass filter to show instant result
        filtered_level = raw_level;
        first_reading_done = true;
        stable_counter = 0;
        DEBUG_PRINT("[SYSTEM] Initial Level Detected: %d%%\r\n",
                    filtered_level);
      } else {
        // In Sleep Mode, we want faster updates.
        // If level changed, we only wait 1 cycle (approx 10s)
        if (raw_level == filtered_level) {
          stable_counter = 0; // Already stable
        } else {
          stable_counter++;
          if (stable_counter >= 1) { // INSTANT UPDATE (1 cycle only)
            filtered_level = raw_level;
            stable_counter = 0;
            DEBUG_PRINT("[SYSTEM] Level Changed & Confirmed: %d%%\r\n",
                        filtered_level);
          }
        }
      }

      uint8_t current_level = filtered_level;
      bool level_changed = (current_level != last_sent_level);
      bool heartbeat_due = (sleep_cycle_count >= HEARTBEAT_CYCLES);

      // --- Battery Internal Logic ---
      if (last_sent_level == 0xFF || heartbeat_due) {
        current_battery = read_battery_level();
        DEBUG_PRINT("[BAT] Battery level: %d%%\r\n", current_battery);
        Delay_Ms(100); // stabilize battery reading
      }

      // Send if: First time OR Level changed OR 4 hours passed
      if (last_sent_level == 0xFF || level_changed || heartbeat_due) {
        uint8_t packet[32];
        for (int i = 0; i < 32; i++)
          packet[i] = 0; // Absolute Zero-Fill

        packet[0] = 0xAA;
        packet[1] = 0x55;
        packet[2] = 0x02;
        packet[3] = 32;
        packet[4] = (uint8_t)(g_settings.tank_id & 0xFF);
        packet[5] = (uint8_t)(g_settings.tank_id >> 8);
        packet[6] = current_level;
        packet[7] = (uint8_t)current_battery;

        packet[8] = 0;
        if (current_battery <= BATTERY_LOW_THRESHOLD)
          packet[8] |= 0x01; // Low Battery Flag

        packet[9] = g_probe_fault; // Probe Fault status (1 or 0)

        packet[12] = (uint8_t)(seq_num & 0xFF);
        packet[13] = (uint8_t)((seq_num >> 8) & 0xFF);
        packet[14] = (uint8_t)((seq_num >> 16) & 0xFF);
        packet[15] = (uint8_t)((seq_num >> 24) & 0xFF);
        packet[31] = calculate_checksum(packet, 32);

        DEBUG_PRINT("[DATA] Reason: %s | Seq:%lu | Lvl:%d%% | Bat:%d%% | "
                    "g_probe_fault: %d -> "
                    "SENDING...\r\n",
                    level_changed ? "CHANGE" : "HEARTBEAT",
                    (unsigned long)seq_num, current_level, current_battery,
                    g_probe_fault);

        // --- POWERFUL RADIO RE-INIT ---
        nrf24_init(); // Reset state
        nrf24_power_up_tx();
        nrf24_set_tx_addr(PAIRING_ADDR);

        Delay_Ms(
            200); // Wait for frequency to be rock solid (Crucial for Low Bat)

        // Burst send 10 times to ensure delivery
        for (int i = 0; i < 10; i++) {
          nrf24_send(packet, 32);
          Delay_Ms(30);
        }

        Delay_Ms(50);

        last_sent_level = current_level;
        sleep_cycle_count = 0; // Reset heartbeat counter
        seq_num++;

        nrf24_power_down();

        // Flash LED to confirm data was SENT
        GPIO_ResetBits(GPIOD, LED_PIN);
        Delay_Ms(100);
        GPIO_SetBits(GPIOD, LED_PIN);
      }
    } else {
      DEBUG_PRINT("[SYSTEM] Not paired, sleeping...\r\n");
      Delay_Ms(100);
    }

    // 3. Enter Deep Sleep
    if (g_settings.pairing_status == 1) {
      DEBUG_PRINT("[SYSTEM] Task Done. Going to Deep Sleep...\r\n");
      DEBUG_PRINT("------------------------------------------\r\n");
    }
    enter_deep_sleep();
    sleep_cycle_count++; // Increment cycles on each AWU wake
  }
}
