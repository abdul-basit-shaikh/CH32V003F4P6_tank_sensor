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
uint16_t g_tank_id = 0x1234;
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
  GPIO_InitStructure.GPIO_Pin = BUTTON_PIN; // PD6
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(GPIOD, &GPIO_InitStructure);
}

/* ========== Pairing Task ========== */
void run_pairing(void) {
  uint8_t packet[32] = {0};
  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x01;
  packet[3] = 32;
  packet[4] = (g_tank_id >> 8);
  packet[5] = (g_tank_id & 0xFF);

  printf("\r\n[PAIR] Starting DISCOVERY BROACAST [3-BYTE RAW] (Ch:99)...\r\n");
  nrf24_power_up_tx();
  nrf24_set_tx_addr(PAIRING_ADDR);

  for (int i = 0; i < 500; i++) {
    printf("[PAIR] Req %d/1000: ", i + 1);
    if (nrf24_send(packet, 32)) {
      printf("RAW DISPATCH OK\r\n");
    } else {
      printf("Radio Busy\r\n");
    }
    Delay_Ms(500); // 20Hz burst for perfect scan overlap
  }
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

  run_pairing();

  printf("[SYSTEM] Entering Daily Mode.\r\n");

  while (1) {
    // Every 1000ms (X-RAY MODE)
    if (millis() - g_last_send > 1000) {
      uint8_t packet[32] = {0xAA,
                            0x55,
                            0x03,
                            32,
                            (uint8_t)(g_tank_id >> 8),
                            (uint8_t)(g_tank_id & 0xFF),
                            95,
                            85};

      printf("[DATA] Send Packet Dispatched...");
      nrf24_power_up_tx();
      if (nrf24_send(packet, 32))
        printf(" OK (ACK)\r\n");
      else
        printf(" FAIL (Lost)\r\n");

      g_last_send = millis();
    }

    // Health Check every 10s
    static uint32_t last_check = 0;
    if (millis() - last_check > 10000) {
      if (nrf24_get_setup() != 0x26) {
        printf("[WARN] Radio Config mismatch! Recovering...\r\n");
        nrf24_init();
      }
      last_check = millis();
    }
    Delay_Ms(10);
  }
}
