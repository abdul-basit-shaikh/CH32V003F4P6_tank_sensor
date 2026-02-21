#include "ch32v00x.h"
#include "ch32v00x_gpio.h"
#include "ch32v00x_rcc.h"
#include "ch32v00x_tim.h"
#include "config.h"
#include "debug.h"
#include "nrf24_simple.h"
#include <string.h>

/* ========== Global Variables ========== */
const uint8_t PAIRING_ADDR[3] = {0xE7, 0xE7, 0xE7}; // 3-byte pairing address
volatile uint32_t g_millis = 0;
uint16_t g_tank_id;
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

  // Generate new tank ID on every pairing/reset
  g_tank_id = generate_new_tank_id();

  printf("\r\n[PAIR] Tank ID changed: 0x%04X\r\n", g_tank_id);

  uint8_t packet[32] = {0};
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x01; // PKT_TYPE_PAIRING_REQ
  packet[3] = 32;
  packet[4] = (g_tank_id & 0xFF); // LSB first
  packet[5] = (g_tank_id >> 8);   // MSB second
  packet[31] = calculate_checksum(packet, 32);

  printf("\r\n[PAIR] Starting DISCOVERY BROADCAST for %d min (Ch:99)...\r\n",
         PAIRING_TIME_MINS);
  printf("[PAIR] Checksum: 0x%02X\r\n", packet[31]);
  nrf24_power_up_tx();
  nrf24_set_tx_addr(PAIRING_ADDR);

  for (int i = 0; i < PAIRING_BURST_COUNT; i++) {
    // LED blink: 500ms ON (10 loops), 500ms OFF (10 loops)
    if ((i % 20) < 10)
      GPIO_ResetBits(GPIOD, LED_PIN); // ON
    else
      GPIO_SetBits(GPIOD, LED_PIN); // OFF

    if (i % 50 == 0) {
      printf("[PAIR] Sending Req burst %d/%d...\r\n", i + 1,
             PAIRING_BURST_COUNT);
    }

    nrf24_send(packet, 32);
    Delay_Ms(50); // Fast burst for better pairing (20Hz)
  }

  GPIO_SetBits(GPIOD, LED_PIN); // LED off after pairing
  printf("[PAIR] Broadcast complete. Returning to normal mode.\r\n");
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
  __enable_irq();

  printf("Initializing Radio Hardware...\r\n");
  nrf24_init();

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
    // 1. Button Handling (Pairing Trigger)
    if (GPIO_ReadInputDataBit(GPIOD, BUTTON_PIN) == 0) {
      if (button_press_start == 0)
        button_press_start = millis();

      // Long press detection (3 seconds)
      if (!pairing_triggered &&
          (millis() - button_press_start > LONG_PRESS_TIME_MS)) {
        printf("[SYSTEM] User trigger: PAIRING MODE\r\n");
        run_pairing();
        pairing_triggered = true;
      }
    } else {
      button_press_start = 0;
      pairing_triggered = false;
    }

    // 2. Periodic Data Transmission (Every 10 seconds)
    static uint32_t last_dispatch = 0;
    static uint32_t seq_num = 0;
    if (millis() - last_dispatch > 10000) {
      uint8_t level = 50; // temp level
      uint8_t battery = 85;

      uint8_t packet[32] = {0};
      packet[0] = 0xAA; // Sync 0
      packet[1] = 0x55; // Sync 1
      packet[2] = 0x03; // PKT_TYPE_TANK_DATA
      packet[3] = 32;   // Length
      packet[4] = (uint8_t)(g_tank_id & 0xFF);
      packet[5] = (uint8_t)(g_tank_id >> 8);
      packet[6] = level;
      packet[7] = battery;

      // Add Sequence Number at index 12 (as per tank_data_packet_t)
      packet[12] = (uint8_t)(seq_num & 0xFF);
      packet[13] = (uint8_t)((seq_num >> 8) & 0xFF);
      packet[14] = (uint8_t)((seq_num >> 16) & 0xFF);
      packet[15] = (uint8_t)((seq_num >> 24) & 0xFF);

      packet[31] = calculate_checksum(packet, 32);

      printf("[DATA] Seq:%lu | Level:%d%% | CRC:0x%02X -> Dispatching BURST "
             "(3x)...\r\n",
             (unsigned long)seq_num, level, packet[31]);

      GPIO_ResetBits(GPIOD, LED_PIN); // ON
      nrf24_power_up_tx();

      // BURST: Send 3 times with small gap
      for (int i = 0; i < 3; i++) {
        nrf24_send(packet, 32);
        Delay_Ms(10);
      }

      // GPIO_SetBits(GPIOD, LED_PIN); // OFF when data send to hub 

      last_dispatch = millis();
      seq_num++;
    }

    Delay_Ms(20);
  }
}
