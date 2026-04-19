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

/* ========== Function Prototypes ========== */
void gpio_init(void);
void adc_init(void);
void exti_init(void);
void pwr_sleep_init(void);
void timer_init(void);
void enter_deep_sleep(void);
void flash_read_settings(void);
void flash_save_settings(void);
uint8_t read_tank_level(void);
uint8_t read_battery_level(void);
void run_pairing(void);

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
  DEBUG_PRINT("[PWR] Sleep\r\n");
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

  DEBUG_PRINT("[PWR] Wake\r\n");

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
  DEBUG_PRINT("[FL] Saved ID:0x%04X S:%d\r\n",
              g_settings.tank_id, g_settings.pairing_status);
}

/* ========== Global Variables ========== */
const uint8_t PAIRING_ADDR[3] = {0xE7, 0xE7,
                                 0xE7}; // Address for pairing packets
volatile uint32_t g_millis = 0;         // Current system time in ms
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
uint8_t read_internal_probes(uint8_t *fault_out) {
  uint8_t p25 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_25) == 0);
  uint8_t p50 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_50) == 0);
  uint8_t p75 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_75) == 0);
  uint8_t p100 = (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_100) == 0);

  // --- FAULT DETECTION LOGIC: Bitmask Mode ---
  uint8_t fault_mask = 0;
  if (p100) {
    if (!p75)
      fault_mask |= (1 << 2);
    if (!p50)
      fault_mask |= (1 << 1);
    if (!p25)
      fault_mask |= (1 << 0);
  } else if (p75) {
    if (!p50)
      fault_mask |= (1 << 1);
    if (!p25)
      fault_mask |= (1 << 0);
  } else if (p50) {
    if (!p25)
      fault_mask |= (1 << 0);
  }

  if (fault_out)
    *fault_out = fault_mask;

  // --- HARDWARE PATCH: Broken Wire Fix ---
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
  uint8_t f1, f2, f3;
  uint8_t r1 = read_internal_probes(&f1);
  Delay_Ms(2);
  uint8_t r2 = read_internal_probes(&f2);
  Delay_Ms(2);
  uint8_t r3 = read_internal_probes(&f3);

  // Combine ALL faults found in any sample
  g_probe_fault = f1 | f2 | f3;

  // Use median-of-3 instead of max-of-3 to avoid sticky high readings
  // during fast draining (e.g. 25% -> 0%).
  uint8_t min = r1;
  uint8_t max = r1;
  if (r2 < min)
    min = r2;
  if (r2 > max)
    max = r2;
  if (r3 < min)
    min = r3;
  if (r3 > max)
    max = r3;
  uint8_t best = (uint8_t)((uint16_t)r1 + (uint16_t)r2 + (uint16_t)r3 - min -
                           max);

  if (best > 0 || g_probe_fault > 0)
    DEBUG_PRINT("[DATA] Level: %d%% | Fault Mask: 0x%02X\r\n", best,
                g_probe_fault);
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
  volatile uint32_t timeout = 50000;
  while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) && --timeout)
    ;
  if (timeout == 0) {
    DEBUG_PRINT("[ADC] TIMEOUT ch%d!\r\n", ch);
    return 0;
  }
  return ADC_GetConversionValue(ADC1);
}

uint8_t read_battery_level(void) {
  // Integer-only battery calculation (no float library needed)
  uint32_t vref_sum = 0;
  for (int i = 0; i < 8; i++)
    vref_sum += get_adc_val(ADC_Channel_Vrefint);
  uint16_t vref_raw = vref_sum / 8;
  if (vref_raw == 0)
    return 0;

  uint32_t bat_sum = 0;
  for (int i = 0; i < 8; i++)
    bat_sum += get_adc_val(BATTERY_ADC_CHANNEL);
  uint16_t bat_raw = bat_sum / 8;

  // V_bat_mV = bat_raw * 1200 * 2 / vref_raw (for 100k/100k divider)
  uint32_t v_bat_mv = ((uint32_t)bat_raw * 2400) / vref_raw;

  DEBUG_PRINT("[BAT] %lumV\r\n", (unsigned long)v_bat_mv);

  // Map 3000mV-4200mV to 0-100%
  if (v_bat_mv >= 4200) return 100;
  if (v_bat_mv <= 3000) return 0;
  return (uint8_t)((v_bat_mv - 3000) * 100 / 1200);
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
  DEBUG_PRINT("\r\n[PAIR] NewID:0x%04X\r\n", new_id);

  // DO NOT save pairing_status yet - wait for controller ACK first
  uint8_t packet[32] = {0};
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x01; // PKT_TYPE_PAIRING_REQ
  packet[3] = 32;
  packet[4] = (new_id & 0xFF);
  packet[5] = (new_id >> 8);
  packet[6] = read_tank_level();    // Level at index 6
  DEBUG_PRINT("[PAIR] Lvl:%d Reading bat...\r\n", packet[6]);
  packet[7] = read_battery_level(); // Battery at index 7
  DEBUG_PRINT("[PAIR] Bat:%d\r\n", packet[7]);
  packet[8] = 0x10;                 // Version at index 8
  packet[31] = calculate_checksum(packet, 32);

  DEBUG_PRINT("[PAIR] Pkt: ");
  for (int k = 0; k < 8; k++)
    DEBUG_PRINT("%02X ", packet[k]);
  DEBUG_PRINT("CRC:%02X\r\n", packet[31]);

  DEBUG_PRINT("[PAIR] Bcast %dmin bursts:%d ack:%dms\r\n",
              PAIRING_TIME_MINS,
              PAIRING_BURST_COUNT, PAIRING_ACK_WAIT_MS);

  bool paired = false;
  uint8_t ack_status = PAIRING_ACK_STATUS_TIMEOUT;

  for (int i = 0; i < PAIRING_BURST_COUNT && !paired; i++) {
    // LED blink pattern
    if ((i % 10) < 5)
      GPIO_ResetBits(GPIOD, LED_PIN);
    else
      GPIO_SetBits(GPIOD, LED_PIN);

    // Log every 20th burst so user can see progress
    if (i % 20 == 0) {
      DEBUG_PRINT("[PAIR] #%d/%d S:0x%02X\r\n", i,
                  PAIRING_BURST_COUNT, nrf24_get_status());
    }

    // 1. Send pairing request
    nrf24_power_up_tx();
    nrf24_set_tx_addr(PAIRING_ADDR);
    nrf24_send(packet, 32);

    // 2. Flush RX FIFO + switch to RX to listen for controller ACK
    nrf24_flush_rx();
    nrf24_power_up_rx();

    // 3. Wait for ACK from controller
    uint32_t start = millis();
    while ((millis() - start) < PAIRING_ACK_WAIT_MS) {
      if (nrf24_available()) {
        uint8_t rx_buf[32];
        nrf24_read(rx_buf, 32);

        DEBUG_PRINT("[PAIR] RX: S=%02X%02X T=%02X\r\n", rx_buf[0],
                    rx_buf[1], rx_buf[2]);

        // Check normal sync: 0xAA 0x55 + type 0x07 (PAIRING_RESP)
        if (rx_buf[0] == 0xAA && rx_buf[1] == 0x55 &&
            rx_buf[2] == PKT_TYPE_PAIRING_RESP) {
          uint8_t calc_crc = calculate_checksum(rx_buf, 32);
          uint16_t resp_id =
              (uint16_t)rx_buf[4] | ((uint16_t)rx_buf[5] << 8);

          DEBUG_PRINT("[PAIR] NSync RID:0x%04X MY:0x%04X "
                      "CC:%02X PC:%02X\r\n",
                      resp_id, new_id, calc_crc, rx_buf[31]);

          if (calc_crc == rx_buf[31] && resp_id == new_id) {
            ack_status = rx_buf[11]; // Status at offset 11
            DEBUG_PRINT("[PAIR] ACK st:%d\r\n", ack_status);

            if (ack_status == PAIRING_ACK_STATUS_PAIRED) {
              // NOW save - controller confirmed pairing is done
              g_settings.tank_id = new_id;
              g_settings.pairing_status = 1;
              flash_save_settings();
              paired = true;
              DEBUG_PRINT("[PAIR] OK! ID:0x%04X\r\n", new_id);
            }
            break;
          } else {
            DEBUG_PRINT("[PAIR] CRC/ID mismatch\r\n");
          }
        }

        // Check bit-inverted sync (NRF phase alignment workaround)
        {
          uint8_t inv[32];
          for (int j = 0; j < 32; j++)
            inv[j] = ~rx_buf[j];

          if (inv[0] == 0xAA && inv[1] == 0x55 &&
              inv[2] == PKT_TYPE_PAIRING_RESP) {
            uint8_t calc_crc = calculate_checksum(inv, 32);
            uint16_t resp_id =
                (uint16_t)inv[4] | ((uint16_t)inv[5] << 8);

            DEBUG_PRINT("[PAIR] InvSync RID:0x%04X MY:0x%04X "
                        "CC:%02X PC:%02X\r\n",
                        resp_id, new_id, calc_crc, inv[31]);

            if (calc_crc == inv[31] && resp_id == new_id) {
              ack_status = inv[11];
              DEBUG_PRINT("[PAIR] ACK(Inv) st:%d\r\n",
                          ack_status);

              if (ack_status == PAIRING_ACK_STATUS_PAIRED) {
                g_settings.tank_id = new_id;
                g_settings.pairing_status = 1;
                flash_save_settings();
                paired = true;
                DEBUG_PRINT("[PAIR] OK(Inv)! ID:0x%04X\r\n",
                            new_id);
              }
              break;
            } else {
              DEBUG_PRINT("[PAIR] Inv CRC/ID mismatch\r\n");
            }
          }
        }
      }
      Delay_Ms(1);
    }
  }

  GPIO_SetBits(GPIOD, LED_PIN); // LED OFF
  nrf24_power_down();

  if (paired) {
    // Success feedback: Fast 5x blink
    for (int i = 0; i < 5; i++) {
      GPIO_ResetBits(GPIOD, LED_PIN);
      Delay_Ms(100);
      GPIO_SetBits(GPIOD, LED_PIN);
      Delay_Ms(100);
    }
    DEBUG_PRINT("[PAIR] DONE\r\n");
  } else {
    // Failed: ensure NOT marked as paired
    g_settings.tank_id = 0;
    g_settings.pairing_status = 0;
    flash_save_settings();

    // Failure feedback: Slow 3x blink
    for (int i = 0; i < 3; i++) {
      GPIO_ResetBits(GPIOD, LED_PIN);
      Delay_Ms(500);
      GPIO_SetBits(GPIOD, LED_PIN);
      Delay_Ms(500);
    }
    DEBUG_PRINT("[PAIR] FAIL after %d tries\r\n",
                PAIRING_BURST_COUNT);
  }
}

/* ========== Main Loop ========== */
int main(void) {
  SystemCoreClockUpdate();
  Delay_Init();
  USART_Printf_Init(115200);

  DEBUG_PRINT("\r\n=== TANK BOOT ===\r\n");
  DEBUG_PRINT("CH32V003 @ %luHz\r\n",
              (unsigned long)SystemCoreClock);

  timer_init();
  gpio_init();
  adc_init();
  pwr_sleep_init();

  // SAFETY: Delay at boot to allow WCH-Link to connect before MCU sleeps.
  // This is the most reliable way to prevent "Lockout" during development.
  DEBUG_PRINT("[SYS] SafeDelay %dms.. ",
              BOOT_SAFETY_DELAY_MS);
  Delay_Ms(BOOT_SAFETY_DELAY_MS);
  DEBUG_PRINT("OK\r\n");

  __enable_irq();

  DEBUG_PRINT("Init Radio...\r\n");
  nrf24_init();
  nrf24_power_down(); // Start in low power mode

  // Load settings from Flash
  flash_read_settings();

  if (g_settings.pairing_status == 1) {
    DEBUG_PRINT("[SYS] Paired ID:0x%04X\r\n",
                g_settings.tank_id);
    nrf24_set_tx_addr(PAIRING_ADDR); // Ensure address is set
  } else {
    DEBUG_PRINT(
        "[SYS] NOT PAIRED. Hold button to pair\r\n");
  }

  // Boot UI: Triple blink (500ms ON, 500ms OFF)
  for (int i = 0; i < 3; i++) {
    GPIO_ResetBits(GPIOD, LED_PIN); // ON
    Delay_Ms(500);
    GPIO_SetBits(GPIOD, LED_PIN); // OFF
    Delay_Ms(500);
  }
  GPIO_SetBits(GPIOD, LED_PIN); // Extra safety: ensure LED is OFF

  DEBUG_PRINT("[SYS] Idle (Hold btn=pair)\r\n");

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
  uint8_t retry_cooldown = 0; // AWU cycles to wait before retry after ACK fail

  while (1) {
    // 1. Button Handling
    if (GPIO_ReadInputDataBit(GPIOD, BUTTON_PIN) == 0) {
      if (button_press_start == 0) {
        button_press_start = millis();
        DEBUG_PRINT("[SYSTEM] Button Pressed\r\n");
      }

      uint32_t held_ms = millis() - button_press_start;

      // --- FACTORY RESET (Long Press >= 5s) ---
      if (!pairing_triggered && held_ms > RESET_PRESS_TIME_MS) {
        DEBUG_PRINT(
            "[SYS] FACTORY RESET\r\n");
        pairing_triggered = true;

        // Sync Unpair with Receiver before reset
        if (g_settings.pairing_status == 1 && g_settings.tank_id != 0) {
          DEBUG_PRINT(
              "Sending UNPAIR before reset\r\n");
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

        if (!pairing_triggered && held_ms > 10 &&
            held_ms < RESET_PRESS_TIME_MS) {
          if (g_settings.pairing_status == 0) {
            // --- NOT PAIRED: Short click starts pairing ---
            DEBUG_PRINT("[SYS] Click -> Pair\r\n");
            run_pairing();
          } else {
            // --- PAIRED: Short click resets MCU ---
            DEBUG_PRINT("[SYS] Click -> Reset\r\n");
            Delay_Ms(100);
            NVIC->SCTLR |= (1 << 31); // SYSRESETREQ
          }
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
        DEBUG_PRINT("[SYSTEM] Init Level: %d%%\r\n",
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
            DEBUG_PRINT("[SYS] LvlChg: %d%%\r\n",
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

      // Send if: First time OR Level changed OR 4 hours heartbeat
      // But respect cooldown after failed ACK (wait ~1 min)
      bool need_send = (last_sent_level == 0xFF || level_changed || heartbeat_due);

      if (level_changed) {
        // New data! Reset cooldown immediately
        retry_cooldown = 0;
      } else if (need_send && retry_cooldown > 0) {
        retry_cooldown--;
        if (retry_cooldown > 0) {
          need_send = false; // Still cooling down, skip this wake
          DEBUG_PRINT("[TX] Cooldown %d\r\n", retry_cooldown);
        }
      }

      if (need_send) {
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

        DEBUG_PRINT("[TX] %s Seq:%lu L:%d%% B:%d%% F:%d\r\n",
                    level_changed ? "CHG" : "HB",
                    (unsigned long)seq_num, current_level, current_battery,
                    g_probe_fault);

        // --- DATA TX WITH ACK + RETRY ---
        nrf24_init();
        bool ack_received = false;

        for (uint8_t attempt = 0; attempt < DATA_MAX_RETRIES && !ack_received;
             attempt++) {
          // TX: Send 3x burst
          nrf24_power_up_tx();
          nrf24_set_tx_addr(PAIRING_ADDR);
          for (int b = 0; b < 3; b++) {
            nrf24_send(packet, 32);
            Delay_Ms(10);
          }

          if (attempt == 0)
            DEBUG_PRINT("[TX] Seq:%lu try:%d\r\n",
                        (unsigned long)seq_num, attempt);

          // RX: Listen for DATA-ACK from controller
          nrf24_flush_rx();
          nrf24_power_up_rx();

          uint32_t start = millis();
          while ((millis() - start) < DATA_ACK_WAIT_MS) {
            if (nrf24_available()) {
              uint8_t rx[32];
              nrf24_read(rx, 32);

              // Check normal sync + PKT_TYPE_ACK
              if (rx[0] == 0xAA && rx[1] == 0x55 &&
                  rx[2] == PKT_TYPE_DATA_ACK) {
                uint8_t crc = calculate_checksum(rx, 32);
                uint16_t ack_id = (uint16_t)rx[4] | ((uint16_t)rx[5] << 8);
                uint32_t ack_seq = (uint32_t)rx[6] | ((uint32_t)rx[7] << 8) |
                                   ((uint32_t)rx[8] << 16) |
                                   ((uint32_t)rx[9] << 24);

                if (crc == rx[31] && ack_id == g_settings.tank_id &&
                    ack_seq == seq_num) {
                  ack_received = true;
                  DEBUG_PRINT("[ACK] OK try:%d\r\n", attempt);
                  break;
                }
              }

              // Check inverted sync
              {
                uint8_t inv[32];
                for (int j = 0; j < 32; j++)
                  inv[j] = ~rx[j];
                if (inv[0] == 0xAA && inv[1] == 0x55 &&
                    inv[2] == PKT_TYPE_DATA_ACK) {
                  uint8_t crc = calculate_checksum(inv, 32);
                  uint16_t ack_id =
                      (uint16_t)inv[4] | ((uint16_t)inv[5] << 8);
                  uint32_t ack_seq =
                      (uint32_t)inv[6] | ((uint32_t)inv[7] << 8) |
                      ((uint32_t)inv[8] << 16) | ((uint32_t)inv[9] << 24);

                  if (crc == inv[31] && ack_id == g_settings.tank_id &&
                      ack_seq == seq_num) {
                    ack_received = true;
                    DEBUG_PRINT("[ACK] OK(inv) try:%d\r\n", attempt);
                    break;
                  }
                }
              }
            }
            Delay_Ms(1);
          }

          if (!ack_received && attempt < DATA_MAX_RETRIES - 1)
            Delay_Ms(DATA_RETRY_DELAY_MS);
        }

        if (!ack_received)
          DEBUG_PRINT("[TX] NO ACK after %d tries\r\n",
                      DATA_MAX_RETRIES);

        if (ack_received) {
          // ACK received: update state, increment seq
          last_sent_level = current_level;
          seq_num++;
          retry_cooldown = 0;
        } else {
          // No ACK: DON'T update last_sent_level so level_changed stays true
          // Wait ~1 min (4 AWU cycles x 15s) before retrying
          retry_cooldown = 4;
        }
        sleep_cycle_count = 0;

        nrf24_power_down();

        // LED: fast blink=ACK ok, slow blink=no ACK
        GPIO_ResetBits(GPIOD, LED_PIN);
        Delay_Ms(ack_received ? 50 : 500);
        GPIO_SetBits(GPIOD, LED_PIN);
      }
    } else {
      DEBUG_PRINT("[SYS] Not paired\r\n");
      Delay_Ms(100);
    }

    // 3. Enter Deep Sleep
    if (g_settings.pairing_status == 1) {
      DEBUG_PRINT("[SYS] Sleep\r\n");
      DEBUG_PRINT("----------\r\n");
    }
    enter_deep_sleep();
    sleep_cycle_count++; // Increment cycles on each AWU wake
  }
}
