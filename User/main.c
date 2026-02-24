#include "ch32v00x.h"
#include "ch32v00x_flash.h"
#include "ch32v00x_gpio.h"
#include "ch32v00x_rcc.h"
#include "ch32v00x_tim.h"
#include "config.h"
#include "debug.h"
#include "nrf24_simple.h"
#include <stdint.h>
#include <string.h>

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
  printf("[FLASH] Settings Saved. ID: 0x%04X, Status: %d\r\n",
         g_settings.tank_id, g_settings.pairing_status);
}

/* ========== Global Variables ========== */
const uint8_t PAIRING_ADDR[3] = {0xE7, 0xE7, 0xE7}; // 3-byte pairing address
volatile uint32_t g_millis = 0;
uint32_t g_last_send = 0;

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
}

/* ========== Sensor Reading ========== */
uint8_t read_tank_level(void) {
  // Logic: Probes are pulled HIGH by internal resistors.
  // When water touches a probe, it shorts to GND (Common), pulling the pin LOW.

  if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_100) == 0)
    return 100;
  if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_75) == 0)
    return 75;
  if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_50) == 0)
    return 50;
  if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_25) == 0)
    return 25;

  return 0; // Empty
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
  // CH32V003 has a 10-bit ADC (0-1023)
  uint32_t val = get_adc_val(BATTERY_ADC_CHANNEL);
  printf("Battery ADC: %lu\r\n", (unsigned long)val);

  // Adjusted mapping based on observed values (~187-200)
  // Let's assume 150 is "Empty" and 250 is "Full" for your current hardware
  // setup
  if (val > 250)
    return 100;
  if (val < 150)
    return 0;
  return (uint8_t)((val - 150) * 100 / (250 - 150)); // Divide by 100 (range)
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

/* ========== Protocol Checksum ========== */
uint8_t calculate_checksum(uint8_t *data, uint8_t length) {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < length - 1; i++) {
    checksum ^= data[i];
  }
  return checksum;
}

void run_pairing(void) {
  // Re-initialize radio to ensure we are in a clean state (same as boot)
  nrf24_init();
  Delay_Ms(100);

  // Generate a temporary tank ID for this pairing session
  uint16_t new_id = generate_new_tank_id();
  // Pairing burst complete, save settings
  g_settings.tank_id = new_id;
  g_settings.pairing_status = 1;
  flash_save_settings();
  printf("\r\n[PAIR] New Tank ID generated: 0x%04X\r\n", new_id);

  uint8_t packet[32] = {0};
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x01; // PKT_TYPE_PAIRING_REQ
  packet[3] = 32;
  packet[4] = (new_id & 0xFF);      // LSB first
  packet[5] = (new_id >> 8);        // MSB second
  packet[6] = 0x10;                 // Firmware version
  packet[7] = read_battery_level(); // Battery level
  packet[8] = read_tank_level();    // Current level
  packet[31] = calculate_checksum(packet, 32);

  printf("\r\n[PAIR] Starting DISCOVERY BROADCAST for %d min (Ch:99)...\r\n",
         PAIRING_TIME_MINS);
  printf("[PAIR] Checksum: 0x%02X\r\n", packet[31]);
  nrf24_power_up_tx();
  nrf24_set_tx_addr(PAIRING_ADDR);

  for (int i = 0; i < PAIRING_BURST_COUNT; i++) {
    // LED blink during pairing
    if ((i % 20) < 10)
      GPIO_ResetBits(GPIOD, LED_PIN); // ON
    else
      GPIO_SetBits(GPIOD, LED_PIN); // OFF

    if (i % 50 == 0) {
      printf("[PAIR] Sending Req burst %d/%d...\r\n", i + 1,
             PAIRING_BURST_COUNT);
    }

    nrf24_send(packet, 32);
    Delay_Ms(50);
  }

  GPIO_SetBits(GPIOD, LED_PIN); // LED off
  printf("[PAIR] Broadcast complete. Status SAVED. Data will now start.\r\n");
}

/* ========== Main Loop ========== */
int main(void) {
  SystemCoreClockUpdate();
  Delay_Init();
  USART_Printf_Init(115200);

  printf("\r\n\r\n=== TANK SENSOR BOOT ===\r\n");
  printf("Platform: CH32V003 @ %lu Hz\r\n", (unsigned long)SystemCoreClock);

  timer_init();
  gpio_init();
  adc_init();
  __enable_irq();

  printf("Initializing Radio Hardware...\r\n");
  nrf24_init();

  // Load settings from Flash
  flash_read_settings();

  if (g_settings.pairing_status == 1) {
    printf("[SYSTEM] Paired Sensor. Loaded ID: 0x%04X\r\n", g_settings.tank_id);
    nrf24_set_tx_addr(PAIRING_ADDR); // Ensure address is set
  } else {
    printf("[SYSTEM] NOT PAIRED. Waiting for user to trigger pairing...\r\n");
  }

  printf("[SYSTEM] Clock Validation: 1000ms delay...");
  uint32_t t_start = millis();
  Delay_Ms(1000);
  printf(" DONE. Millis Diff: %lu ms\r\n", (unsigned long)(millis() - t_start));

  // Boot UI: Triple blink (500ms ON, 500ms OFF)
  for (int i = 0; i < 3; i++) {
    GPIO_ResetBits(GPIOD, LED_PIN); // ON
    Delay_Ms(500);
    GPIO_SetBits(GPIOD, LED_PIN); // OFF
    Delay_Ms(500);
  }
  GPIO_SetBits(GPIOD, LED_PIN); // Extra safety: ensure LED is OFF

  printf("[SYSTEM] Entering Idle Mode. (Hold button for pairing)\r\n");

  uint32_t button_press_start = 0;
  bool pairing_triggered = false;

  while (1) {
    // 1. Button Handling: 5s = Factory Reset + Auto Pairing
    if (GPIO_ReadInputDataBit(GPIOD, BUTTON_PIN) == 0) {
      if (button_press_start == 0)
        button_press_start = millis();

      uint32_t held_ms = millis() - button_press_start;

      // 5s hold -> FACTORY RESET + AUTO PAIRING
      if (!pairing_triggered && held_ms > RESET_PRESS_TIME_MS) {
        printf("[SYSTEM] *** FACTORY RESET TRIGGERED ***\r\n");
        g_settings.tank_id = 0;
        g_settings.pairing_status = 0;
        flash_save_settings();

        printf("[SYSTEM] Reset done. Starting Pairing automatically...\r\n");
        run_pairing(); // Auto start pairing after reset
        pairing_triggered = true;
      }
    } else {
      button_press_start = 0;
      pairing_triggered = false;
    }

    // 2. Periodic Data Transmission (Every 10 seconds)
    static uint32_t last_dispatch = 0;
    static uint32_t seq_num = 0;

    // ONLY send data if pairing_status is 1
    if (g_settings.pairing_status == 1 && (millis() - last_dispatch > 10000)) {
      uint8_t level = 50; // Hardcoded to 50% for testing as requested
      uint8_t battery = read_battery_level();

      uint8_t packet[32] = {0};
      packet[0] = 0xAA; // Sync 0
      packet[1] = 0x55; // Sync 1
      packet[2] = 0x03; // PKT_TYPE_TANK_DATA
      packet[3] = 32;   // Length
      packet[4] = (uint8_t)(g_settings.tank_id & 0xFF);
      packet[5] = (uint8_t)(g_settings.tank_id >> 8);
      packet[6] = 50; // level;
      packet[7] = battery;

      // Add Sequence Number at index 12 (as per tank_data_packet_t)
      packet[12] = (uint8_t)(seq_num & 0xFF);
      packet[13] = (uint8_t)((seq_num >> 8) & 0xFF);
      packet[14] = (uint8_t)((seq_num >> 16) & 0xFF);
      packet[15] = (uint8_t)((seq_num >> 24) & 0xFF);

      packet[31] = calculate_checksum(packet, 32);

      printf("[DATA] Seq:%lu | Level:%d%% | battery:%d%% | CRC:0x%02X -> "
             "Dispatching BURST "
             "(3x)...\r\n",
             (unsigned long)seq_num, level, battery, packet[31]);

      printf("tank_id: 0x%04X\r\n", g_settings.tank_id);
      // GPIO_ResetBits(GPIOD, LED_PIN); // ON
      nrf24_power_up_tx();

      // BURST: Send 3 times with small gap
      for (int i = 0; i < 3; i++) {
        nrf24_send(packet, 32);
        Delay_Ms(10);
      }

      GPIO_SetBits(GPIOD, LED_PIN); // OFF when data send to hub

      last_dispatch = millis();
      seq_num++;
    }

    Delay_Ms(20);
  }
}
