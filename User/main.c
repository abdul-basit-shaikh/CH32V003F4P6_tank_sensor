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
#include <stdint.h>
#include <string.h>

#if USE_WIRELESS_LORA
#include "lora_sx1278.h"
#define radio_init() lora_init()
#define radio_send(buf, len) lora_send(buf, len)
#define radio_available() lora_available()
#define radio_read(buf, len) lora_read(buf, len)
#define radio_power_up_tx() lora_power_up_tx()
#define radio_power_up_rx() lora_power_up_rx()
#define radio_power_down() lora_power_down()
#define radio_flush_rx() ((void)0)
#define radio_set_tx_addr(a) ((void)0)
#define radio_take_rpd() (1)
#else
#include "nrf24_simple.h"
#define radio_init() nrf24_init()
#define radio_send(buf, len) nrf24_send(buf, len)
#define radio_available() nrf24_available()
#define radio_read(buf, len) nrf24_read(buf, len)
#define radio_power_up_tx() nrf24_power_up_tx()
#define radio_power_up_rx() nrf24_power_up_rx()
#define radio_power_down() nrf24_power_down()
#define radio_flush_rx() nrf24_flush_rx()
#define radio_set_tx_addr(a) nrf24_set_tx_addr(a)
#define radio_take_rpd() nrf24_take_rpd_latched()
#endif

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
uint32_t millis(void);
uint8_t calculate_checksum(const uint8_t *data, uint8_t length);

typedef struct {
  uint32_t button_press_start;
  bool pairing_triggered;
  uint16_t sleep_cycle_count;
  uint32_t seq_num;
  uint8_t last_sent_level;
  uint8_t filtered_level;
  uint8_t stable_counter;
  bool first_reading_done;
  uint8_t current_battery;
  uint8_t retry_cooldown;
  uint8_t link_quality;
  uint8_t last_rpd_seen;
  bool link_quality_valid;
} runtime_state_t;

typedef enum {
  BUTTON_ACTION_NONE = 0,
  BUTTON_ACTION_RESET,
  BUTTON_ACTION_FACTORY_RESET,
} button_action_t;

typedef enum {
  TX_WAIT_TIMEOUT = 0,
  TX_WAIT_ACK_RECEIVED,
  TX_WAIT_LEVEL_CHANGED,
  TX_WAIT_ABORTED,
} tx_wait_result_t;

typedef enum {
  TX_SEND_NO_ACK = 0,
  TX_SEND_ACKED,
  TX_SEND_RESTART_LEVEL,
  TX_SEND_ABORTED,
} tx_send_result_t;

static volatile uint8_t g_sensor_event_pending = 0;
static volatile uint8_t g_button_event_pending = 0;
static runtime_state_t g_runtime;

/* ========== Power Management & AWU ========== */
void pwr_sleep_init(void) {
  // Enable PWR clock
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

  // Configure LSI for AWU
  RCC_LSICmd(ENABLE);
  while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET)
    ;

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
  EXTI_ClearITPendingBit(EXTI_Line9);
  NVIC_ClearPendingIRQ(AWU_IRQn);
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

  // Auto Wake-up (AWU) Event on EXTI Line 9 (Crucial for AWU wakeup!)
  EXTI_InitStructure.EXTI_Line = EXTI_Line9;
  EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
  EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
  EXTI_InitStructure.EXTI_LineCmd = ENABLE;
  EXTI_Init(&EXTI_InitStructure);

  // Enable NVIC for EXTI Line 7-0 and AWU
  NVIC_InitTypeDef NVIC_InitStructure = {0};
  NVIC_InitStructure.NVIC_IRQChannel = EXTI7_0_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
  if (EXTI_GetITStatus(EXTI_Line0) != RESET ||
      EXTI_GetITStatus(EXTI_Line1) != RESET ||
      EXTI_GetITStatus(EXTI_Line2) != RESET ||
      EXTI_GetITStatus(EXTI_Line4) != RESET) {
    g_sensor_event_pending = 1;
  }

  if (EXTI_GetITStatus(EXTI_Line6) != RESET) {
    g_button_event_pending = 1;
  }

  // Clear all pending flags for sensor pins and button
  EXTI_ClearITPendingBit(EXTI_Line0 | EXTI_Line1 | EXTI_Line2 | EXTI_Line4 |
                         EXTI_Line6);
}

void enter_deep_sleep(void) {
  DEBUG_PRINT("[PWR] Sleep (5s AWU)...\r\n");
  Delay_Ms(30); // Flush UART

  // 1. Re-arm Auto Wakeup (AWU) timer before EVERY sleep
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
  PWR_AWU_SetPrescaler(PWR_AWU_Prescaler_61440);
  PWR_AWU_SetWindowValue(10); // ~5 seconds periodic wake-up
  PWR_AutoWakeUpCmd(ENABLE);  // Arm the downcounter

  // 2. Clear all previous pending interrupt flags
  EXTI->INTFR = 0xFFFFFFFF;
  EXTI_ClearITPendingBit(EXTI_Line9);
  NVIC_ClearPendingIRQ(AWU_IRQn);
  NVIC_ClearPendingIRQ(EXTI7_0_IRQn);

  // 3. Enter Stop mode (SLEEPDEEP=1, PDDS=0)
  PWR->CTLR &= ~PWR_CTLR_PDDS; // PDDS = 0
  NVIC->SCTLR |= (1 << 2);     // Set SLEEPDEEP

  __WFI(); // Wait for Interrupt (AWU timer fires in 5s OR user presses button)

  // 4. Cleanup sleep state
  NVIC->SCTLR &= ~(1 << 2); // Clear SLEEPDEEP
  PWR_AutoWakeUpCmd(DISABLE);
  EXTI_ClearITPendingBit(EXTI_Line9);
  NVIC_ClearPendingIRQ(AWU_IRQn);

  // 5. System resumes here after wake up
  SystemCoreClockUpdate();
  EXTI->INTFR = 0xFFFFFFFF;

  DEBUG_PRINT("[PWR] Wake\r\n");

  // Visual confirmation of wakeup (brief 20ms LED blink)
  GPIO_ResetBits(GPIOD, LED_PIN); // Blink ON
  Delay_Ms(20);
  GPIO_SetBits(GPIOD, LED_PIN); // Blink OFF

  // Essential re-init after deep sleep
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOC |
                             RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1 |
                             RCC_APB2Periph_AFIO,
                         ENABLE);
  gpio_init();
  adc_init();

  Delay_Ms(50); // Short stabilization delay
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
  DEBUG_PRINT("[FL] Saved ID:0x%04X S:%d\r\n", g_settings.tank_id,
              g_settings.pairing_status);
}

/* ========== Global Variables ========== */
const uint8_t PAIRING_ADDR[3] = {0xE7, 0xE7,
                                 0xE7}; // Address for pairing packets
volatile uint32_t g_millis = 0;         // Current system time in ms
uint8_t g_probe_fault = 0; // 1 = Fault detected (e.g., gap in readings)

static inline bool is_button_pressed(void) {
  return GPIO_ReadInputDataBit(GPIOD, BUTTON_PIN) == 0;
}

static void reset_runtime_tracking(runtime_state_t *runtime) {
  runtime->button_press_start = 0;
  runtime->pairing_triggered = false;
  runtime->sleep_cycle_count = 0;
  runtime->seq_num = 0;
  runtime->last_sent_level = 0xFF;
  runtime->filtered_level = 0;
  runtime->stable_counter = 0;
  runtime->first_reading_done = false;
  runtime->current_battery = 0;
  runtime->retry_cooldown = 0;
  runtime->link_quality = 0;
  runtime->last_rpd_seen = 0;
  runtime->link_quality_valid = false;
  g_sensor_event_pending = 0;
  g_button_event_pending = 0;
}

static uint8_t sample_filtered_level(runtime_state_t *runtime) {
  uint8_t raw_level = read_tank_level();

  if (!runtime->first_reading_done) {
    runtime->filtered_level = raw_level;
    runtime->first_reading_done = true;
    runtime->stable_counter = 0;
    DEBUG_PRINT("[SYSTEM] Init Level: %d%%\r\n", runtime->filtered_level);
  } else if (raw_level == runtime->filtered_level) {
    runtime->stable_counter = 0;
  } else {
    runtime->stable_counter++;
    if (runtime->stable_counter >= 1) {
      runtime->filtered_level = raw_level;
      runtime->stable_counter = 0;
      DEBUG_PRINT("[SYS] LvlChg: %d%%\r\n", runtime->filtered_level);
    }
  }

  return runtime->filtered_level;
}

static uint8_t build_link_quality_sample(uint8_t attempt, bool ack_received,
                                         bool rpd_seen) {
  if (!ack_received) {
    return rpd_seen ? 10 : 0;
  }

  uint8_t max_attempt = (DATA_MAX_RETRIES > 0) ? (DATA_MAX_RETRIES - 1) : 0;
  if (attempt > max_attempt) {
    attempt = max_attempt;
  }

  uint8_t penalty = 0;
  if (max_attempt > 0) {
    penalty = (uint8_t)(((uint16_t)attempt * 80U) / (uint16_t)max_attempt);
  }

  uint8_t score = (uint8_t)(100U - penalty);
  if (score < 20U) {
    score = 20U;
  }

  if (rpd_seen && score < 100U) {
    uint8_t boosted = (uint8_t)(score + 8U);
    score = (boosted > 100U) ? 100U : boosted;
  }

  return score;
}

static void update_link_quality(runtime_state_t *runtime, uint8_t attempt,
                                bool ack_received, bool rpd_seen) {
  uint8_t sample = build_link_quality_sample(attempt, ack_received, rpd_seen);
  runtime->last_rpd_seen = rpd_seen ? 1 : 0;

  if (!runtime->link_quality_valid) {
    runtime->link_quality = sample;
    runtime->link_quality_valid = true;
    return;
  }

  runtime->link_quality =
      (uint8_t)((((uint16_t)runtime->link_quality * 3U) + sample + 2U) / 4U);
}

static button_action_t poll_button_action(runtime_state_t *runtime) {
  bool button_pressed = is_button_pressed();

  if (button_pressed) {
    uint32_t now = millis();
    if (runtime->button_press_start == 0) {
      runtime->button_press_start = now;
      DEBUG_PRINT("[SYSTEM] Button Pressed\r\n");
    }

    if (!runtime->pairing_triggered &&
        (now - runtime->button_press_start) > RESET_PRESS_TIME_MS) {
      runtime->pairing_triggered = true;
      return BUTTON_ACTION_FACTORY_RESET;
    }

    return BUTTON_ACTION_NONE;
  }

  if (runtime->button_press_start == 0) {
    return BUTTON_ACTION_NONE;
  }

  uint32_t held_ms = millis() - runtime->button_press_start;
  runtime->button_press_start = 0;

  if (!runtime->pairing_triggered && held_ms > 10 &&
      held_ms < RESET_PRESS_TIME_MS) {
    runtime->pairing_triggered = false;
    if (g_settings.pairing_status == 1) {
      return BUTTON_ACTION_RESET;
    }

    return BUTTON_ACTION_NONE;
  }

  runtime->pairing_triggered = false;
  return BUTTON_ACTION_NONE;
}

static void send_unpair_before_reset(void) {
  DEBUG_PRINT("Sending UNPAIR before reset\r\n");

  uint8_t unpair_pkt[32] = {0};
  unpair_pkt[0] = 0xAA;
  unpair_pkt[1] = 0x55;
  unpair_pkt[2] = 0x06; // PKT_TYPE_UNPAIR
  unpair_pkt[3] = 32;
  unpair_pkt[4] = (uint8_t)(g_settings.tank_id & 0xFF);
  unpair_pkt[5] = (uint8_t)(g_settings.tank_id >> 8);
  unpair_pkt[31] = calculate_checksum(unpair_pkt, 32);

  radio_power_up_tx();
  radio_set_tx_addr(PAIRING_ADDR);
  for (int i = 0; i < 20; i++) {
    radio_send(unpair_pkt, 32);
    Delay_Ms(30);
  }
  radio_power_down();
}

static void handle_button_action(runtime_state_t *runtime,
                                 button_action_t action) {
  runtime->button_press_start = 0;
  runtime->pairing_triggered = false;
  g_button_event_pending = 0;

  if (action == BUTTON_ACTION_NONE) {
    return;
  }

  if (action == BUTTON_ACTION_FACTORY_RESET) {
    DEBUG_PRINT("[SYS] FACTORY RESET\r\n");

    if (g_settings.pairing_status == 1 && g_settings.tank_id != 0) {
      send_unpair_before_reset();
    }

    g_settings.tank_id = 0;
    g_settings.pairing_status = 0;
    flash_save_settings();

    reset_runtime_tracking(runtime);
    run_pairing();
    return;
  }

  DEBUG_PRINT("[SYS] Click -> Reset\r\n");
  Delay_Ms(100);
  NVIC->SCTLR |= (1 << 31); // SYSRESETREQ
}

static bool packet_matches_data_ack(uint8_t *packet, uint32_t seq_num) {
  if (packet[0] != 0xAA || packet[1] != 0x55 ||
      packet[2] != PKT_TYPE_DATA_ACK) {
    return false;
  }

  uint16_t ack_id = (uint16_t)packet[4] | ((uint16_t)packet[5] << 8);
  uint32_t ack_seq = (uint32_t)packet[6] | ((uint32_t)packet[7] << 8) |
                     ((uint32_t)packet[8] << 16) | ((uint32_t)packet[9] << 24);

  return (ack_id == g_settings.tank_id && ack_seq == seq_num);
}

static tx_wait_result_t
service_runtime_window(runtime_state_t *runtime, uint32_t wait_ms,
                       bool listen_for_ack, uint8_t attempt, uint32_t seq_num,
                       uint8_t sent_level, uint8_t *latest_level) {
  uint32_t start = millis();

  while (1) {
    uint32_t now = millis();
    if ((now - start) >= wait_ms) {
      break;
    }

    if (listen_for_ack && radio_available()) {
      uint8_t rx[32];
      radio_read(rx, 32);

      if (packet_matches_data_ack(rx, seq_num)) {
        DEBUG_PRINT("[ACK] OK try:%d\r\n", attempt);
        return TX_WAIT_ACK_RECEIVED;
      }

      for (uint8_t j = 0; j < 32; j++) {
        rx[j] = (uint8_t)~rx[j];
      }

      if (packet_matches_data_ack(rx, seq_num)) {
        DEBUG_PRINT("[ACK] OK(inv) try:%d\r\n", attempt);
        return TX_WAIT_ACK_RECEIVED;
      }
    }

    if (g_sensor_event_pending) {
      g_sensor_event_pending = 0;
      *latest_level = sample_filtered_level(runtime);
      if (*latest_level != sent_level) {
        DEBUG_PRINT("[TX] Lvl update during TX: %d%%\r\n", *latest_level);
        return TX_WAIT_LEVEL_CHANGED;
      }
    }

    bool button_pressed = is_button_pressed();
    if (g_button_event_pending || runtime->button_press_start != 0 ||
        button_pressed) {
      g_button_event_pending = 0;
      button_action_t action = poll_button_action(runtime);
      if (action != BUTTON_ACTION_NONE) {
        handle_button_action(runtime, action);
        return TX_WAIT_ABORTED;
      }
    }

    Delay_Ms(1);
  }

  return TX_WAIT_TIMEOUT;
}

static void prepare_data_packet(uint8_t *packet, uint8_t current_level) {
  memset(packet, 0, 32);

  packet[0] = 0xAA;
  packet[1] = 0x55;
  packet[2] = 0x02;
  packet[3] = 32;
  packet[4] = (uint8_t)(g_settings.tank_id & 0xFF);
  packet[5] = (uint8_t)(g_settings.tank_id >> 8);
  packet[6] = current_level;
  packet[7] = g_runtime.current_battery;

  packet[8] = 0;
  if (g_runtime.current_battery <= BATTERY_LOW_THRESHOLD) {
    packet[8] |= 0x01; // Low Battery Flag
  }

  packet[9] = g_probe_fault;
  packet[10] = g_runtime.link_quality_valid ? g_runtime.link_quality : 0xFF;
  packet[11] = g_runtime.last_rpd_seen;
  packet[12] = (uint8_t)(g_runtime.seq_num & 0xFF);
  packet[13] = (uint8_t)((g_runtime.seq_num >> 8) & 0xFF);
  packet[14] = (uint8_t)((g_runtime.seq_num >> 16) & 0xFF);
  packet[15] = (uint8_t)((g_runtime.seq_num >> 24) & 0xFF);
  packet[31] = calculate_checksum(packet, 32);
}

static tx_send_result_t send_data_with_retry(runtime_state_t *runtime,
                                             uint8_t *packet,
                                             uint8_t *current_level) {
  for (uint8_t attempt = 0; attempt < DATA_MAX_RETRIES; attempt++) {
    radio_power_up_tx();
    radio_set_tx_addr(PAIRING_ADDR);

#if USE_WIRELESS_LORA
    // LoRa: 1 clean packet per attempt so sensor enters RX before Controller
    // sends ACK
    radio_send(packet, 32);
#else
    for (int burst = 0; burst < 3; burst++) {
      radio_send(packet, 32);

      tx_wait_result_t gap_result =
          service_runtime_window(runtime, 10, false, attempt, runtime->seq_num,
                                 *current_level, current_level);
      if (gap_result == TX_WAIT_LEVEL_CHANGED) {
        radio_power_down();
        return TX_SEND_RESTART_LEVEL;
      }
      if (gap_result == TX_WAIT_ABORTED) {
        radio_power_down();
        return TX_SEND_ABORTED;
      }
    }
#endif

    if (attempt == 0) {
      DEBUG_PRINT("[TX] Seq:%lu try:%d\r\n", (unsigned long)runtime->seq_num,
                  attempt);
    }

    radio_flush_rx();
    radio_power_up_rx();

    tx_wait_result_t ack_result =
        service_runtime_window(runtime, DATA_ACK_WAIT_MS, true, attempt,
                               runtime->seq_num, *current_level, current_level);
    bool rpd_seen = radio_take_rpd();

    if (ack_result == TX_WAIT_ACK_RECEIVED) {
      update_link_quality(runtime, attempt, true, rpd_seen);
      radio_power_down();
      return TX_SEND_ACKED;
    }
    if (ack_result == TX_WAIT_LEVEL_CHANGED) {
      radio_power_down();
      return TX_SEND_RESTART_LEVEL;
    }
    if (ack_result == TX_WAIT_ABORTED) {
      radio_power_down();
      return TX_SEND_ABORTED;
    }

    update_link_quality(runtime, attempt, false, rpd_seen);

    if (attempt < DATA_MAX_RETRIES - 1) {
      tx_wait_result_t retry_result = service_runtime_window(
          runtime, DATA_RETRY_DELAY_MS, false, attempt, runtime->seq_num,
          *current_level, current_level);
      if (retry_result == TX_WAIT_LEVEL_CHANGED) {
        radio_power_down();
        return TX_SEND_RESTART_LEVEL;
      }
      if (retry_result == TX_WAIT_ABORTED) {
        radio_power_down();
        return TX_SEND_ABORTED;
      }
    }
  }

  radio_power_down();
  return TX_SEND_NO_ACK;
}

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

  // Sensor Common Power: PD3 (Output Push-pull, initially 0V / OFF)
  GPIO_InitStructure.GPIO_Pin = SENSOR_POWER_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOD, &GPIO_InitStructure);
  GPIO_ResetBits(GPIOD, SENSOR_POWER_PIN); // Keep OFF during sleep

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
 */
uint8_t read_internal_probes(uint8_t *fault_out) {
  // 1. Power ON the common probe wire (PD3)
  GPIO_SetBits(GPIOD, SENSOR_POWER_PIN);
  Delay_Ms(15); // Allow water conductivity & transistor junction to stabilize

  // 2. Take 5 multi-samples over 10ms to filter contact resistance & AC ripple
  uint8_t c25 = 0, c50 = 0, c75 = 0, c100 = 0;
  for (int s = 0; s < 5; s++) {
    if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_25) == 0) c25++;
    if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_50) == 0) c50++;
    if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_75) == 0) c75++;
    if (GPIO_ReadInputDataBit(GPIOC, SENSOR_PIN_100) == 0) c100++;
    Delay_Ms(2);
  }

  // 3. Power OFF probe wire immediately (keeps average power < 0.01mW)
  GPIO_ResetBits(GPIOD, SENSOR_POWER_PIN);

  // Majority vote: if at least 2 out of 5 samples detected LOW (transistor ON), probe is wet
  uint8_t p25 = (c25 >= 2);
  uint8_t p50 = (c50 >= 2);
  uint8_t p75 = (c75 >= 2);
  uint8_t p100 = (c100 >= 2);

  DEBUG_PRINT("[PROBES] Raw hits (out of 5): [25%%]:%d [50%%]:%d [75%%]:%d [100%%]:%d\r\n",
              c25, c50, c75, c100);

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
  uint8_t fault = 0;
  uint8_t level = read_internal_probes(&fault);
  g_probe_fault = fault;

  DEBUG_PRINT("[DATA] Tank Level: %d%%\r\n", level);
  return level;
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
  uint32_t accum = 0;
  uint8_t i;

  for (i = 0; i < 8; i++)
    accum += get_adc_val(ADC_Channel_Vrefint);
  uint16_t vref_raw = (uint16_t)(accum / 8);

  if (vref_raw == 0)
    return 0;

  accum = 0;
  for (i = 0; i < 8; i++)
    accum += get_adc_val(BATTERY_ADC_CHANNEL);
  uint16_t bat_raw = (uint16_t)(accum / 8);

  uint32_t v_bat_mv = ((uint32_t)bat_raw * BAT_VOLTAGE_SCALE) / vref_raw;
  DEBUG_PRINT("[BAT] %lumV\r\n", (unsigned long)v_bat_mv);

  if (BAT_MAX_MV <= BAT_MIN_MV)
    return 0;
  if (v_bat_mv >= BAT_MAX_MV)
    return 100;
  if (v_bat_mv <= BAT_MIN_MV)
    return 0;

  return (uint8_t)((v_bat_mv - BAT_MIN_MV) * 100 / (BAT_MAX_MV - BAT_MIN_MV));
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
uint8_t calculate_checksum(const uint8_t *data, uint8_t length) {
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
  radio_init();
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
  packet[6] = read_tank_level(); // Level at index 6
  DEBUG_PRINT("[PAIR] Lvl:%d Reading bat...\r\n", packet[6]);
  packet[7] = read_battery_level(); // Battery at index 7
  DEBUG_PRINT("[PAIR] Bat:%d\r\n", packet[7]);
  packet[8] = 0x10; // Version at index 8
  packet[31] = calculate_checksum(packet, 32);

  DEBUG_PRINT("[PAIR] Pkt: ");
  for (int k = 0; k < 8; k++)
    DEBUG_PRINT("%02X ", packet[k]);
  DEBUG_PRINT("CRC:%02X\r\n", packet[31]);

  DEBUG_PRINT("[PAIR] Bcast %dmin bursts:%d ack:%dms\r\n", PAIRING_TIME_MINS,
              PAIRING_BURST_COUNT, PAIRING_ACK_WAIT_MS);

  bool paired = false;
  uint8_t ack_status = PAIRING_ACK_STATUS_TIMEOUT;

  uint32_t pairing_start_time = millis();
  for (int i = 0; i < PAIRING_BURST_COUNT && !paired; i++) {
    // Check for 1 minute timeout explicitly
    if ((millis() - pairing_start_time) > (PAIRING_TIME_MINS * 60000)) {
      DEBUG_PRINT("[PAIR] Timeout reached after %d mins\r\n",
                  PAIRING_TIME_MINS);
      break;
    }

    // LED blink pattern
    if ((i % 2) < 1)
      GPIO_ResetBits(GPIOD, LED_PIN);
    else
      GPIO_SetBits(GPIOD, LED_PIN);

    // 1. Send pairing request
    radio_power_up_tx();
    radio_set_tx_addr(PAIRING_ADDR);
    radio_send(packet, 32);

    // 2. Flush RX FIFO + switch to RX to listen for controller ACK
    radio_flush_rx();
    radio_power_up_rx();

    // 3. Wait for ACK from controller
    uint32_t start = millis();
    while ((millis() - start) < PAIRING_ACK_WAIT_MS) {
      if (radio_available()) {
        uint8_t rx_buf[32];
        radio_read(rx_buf, 32);

        DEBUG_PRINT("[PAIR] RX: S=%02X%02X T=%02X\r\n", rx_buf[0], rx_buf[1],
                    rx_buf[2]);

        // Check normal sync: 0xAA 0x55 + type 0x07 (PAIRING_RESP)
        if (rx_buf[0] == 0xAA && rx_buf[1] == 0x55 &&
            rx_buf[2] == PKT_TYPE_PAIRING_RESP) {
          uint16_t resp_id = (uint16_t)rx_buf[4] | ((uint16_t)rx_buf[5] << 8);
          ack_status = rx_buf[11];

          DEBUG_PRINT("[PAIR] NSync RID:0x%04X MY:0x%04X Status=%d Slot=%d\r\n",
                      resp_id, new_id, ack_status, rx_buf[6]);

          if (resp_id == new_id &&
              (ack_status == PAIRING_ACK_STATUS_PAIRED || ack_status == 1)) {
            g_settings.tank_id = new_id;
            g_settings.pairing_status = 1;
            flash_save_settings();
            paired = true;
            DEBUG_PRINT("[PAIR] 🎉 MATCHED! Paired with Slot %d, Stopping "
                        "Pairing Loop.\r\n",
                        rx_buf[6]);
            break;
          }
        }

        // Check bit-inverted sync (NRF phase alignment workaround)
        {
          uint8_t inv[32];
          for (int j = 0; j < 32; j++)
            inv[j] = ~rx_buf[j];

          if (inv[0] == 0xAA && inv[1] == 0x55 &&
              inv[2] == PKT_TYPE_PAIRING_RESP) {
            uint16_t resp_id = (uint16_t)inv[4] | ((uint16_t)inv[5] << 8);
            ack_status = inv[11];

            DEBUG_PRINT(
                "[PAIR] InvSync RID:0x%04X MY:0x%04X Status=%d Slot=%d\r\n",
                resp_id, new_id, ack_status, inv[6]);

            if (resp_id == new_id &&
                (ack_status == PAIRING_ACK_STATUS_PAIRED || ack_status == 1)) {
              g_settings.tank_id = new_id;
              g_settings.pairing_status = 1;
              flash_save_settings();
              paired = true;
              DEBUG_PRINT("[PAIR] 🎉 MATCHED(Inv)! Paired with Slot %d, "
                          "Stopping Pairing Loop.\r\n",
                          inv[6]);
              break;
            }
          }
        }
      }
      Delay_Ms(1);
    }
  }

  GPIO_SetBits(GPIOD, LED_PIN); // LED OFF
  radio_power_down();

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
    DEBUG_PRINT("[PAIR] FAIL after %d tries\r\n", PAIRING_BURST_COUNT);
  }
}

/* ========== Main Loop ========== */
int main(void) {
  SystemCoreClockUpdate();
  Delay_Init();
  USART_Printf_Init(115200);

  DEBUG_PRINT("\r\n=== TANK BOOT ===\r\n");
  DEBUG_PRINT("CH32V003 @ %luHz\r\n", (unsigned long)SystemCoreClock);

  timer_init();
  gpio_init();
  adc_init();
  pwr_sleep_init();

  // SAFETY: Delay at boot to allow WCH-Link to connect before MCU sleeps.
  // This is the most reliable way to prevent "Lockout" during development.
  DEBUG_PRINT("[SYS] SafeDelay %dms.. ", BOOT_SAFETY_DELAY_MS);
  Delay_Ms(BOOT_SAFETY_DELAY_MS);
  DEBUG_PRINT("OK\r\n");

  __enable_irq();

  DEBUG_PRINT("Init Radio...\r\n");
  radio_init();
  radio_power_down(); // Start in low power mode

  // Load settings from Flash
  flash_read_settings();

  if (g_settings.pairing_status == 1) {
    DEBUG_PRINT("[SYS] Paired ID:0x%04X\r\n", g_settings.tank_id);
    radio_set_tx_addr(PAIRING_ADDR); // Ensure address is set
  } else {
    DEBUG_PRINT("[SYS] NOT PAIRED. Hold button to pair\r\n");
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

  reset_runtime_tracking(&g_runtime);

  while (1) {
    // 1. Button Handling
    bool button_pressed = is_button_pressed();
    if (g_button_event_pending || g_runtime.button_press_start != 0 ||
        button_pressed) {
      g_button_event_pending = 0;

      button_action_t button_action = poll_button_action(&g_runtime);
      if (button_action != BUTTON_ACTION_NONE) {
        handle_button_action(&g_runtime, button_action);
        continue;
      }

      if (button_pressed) {
        Delay_Ms(10);
        continue;
      }
    }

    // 2. Data Logic (Only if paired)
    if (g_settings.pairing_status == 1) {
      uint8_t current_level = sample_filtered_level(&g_runtime);
      bool level_changed = (current_level != g_runtime.last_sent_level);
      bool heartbeat_due = (g_runtime.sleep_cycle_count >= HEARTBEAT_CYCLES);

      // --- Battery Internal Logic ---
      if (g_runtime.last_sent_level == 0xFF || heartbeat_due) {
        g_runtime.current_battery = read_battery_level();
        DEBUG_PRINT("[BAT] Battery level: %d%%\r\n", g_runtime.current_battery);
        Delay_Ms(100); // stabilize battery reading
      }

      // Send if: First time OR Level changed OR 4 hours heartbeat
      // But respect cooldown after failed ACK (wait ~1 min)
      bool need_send =
          (g_runtime.last_sent_level == 0xFF || level_changed || heartbeat_due);

      if (level_changed) {
        // New data! Reset cooldown immediately
        g_runtime.retry_cooldown = 0;
      } else if (need_send && g_runtime.retry_cooldown > 0) {
        g_runtime.retry_cooldown--;
        if (g_runtime.retry_cooldown > 0) {
          need_send = false; // Still cooling down, skip this wake
          DEBUG_PRINT("[TX] Cooldown %d\r\n", g_runtime.retry_cooldown);
        }
      }

      if (need_send) {
        uint8_t packet[32];
        bool ack_received = false;
        bool tx_aborted = false;
        bool send_attempted = false;
        bool show_tx_feedback = false;

        while (1) {
          level_changed = (current_level != g_runtime.last_sent_level);
          if (!(g_runtime.last_sent_level == 0xFF || level_changed ||
                heartbeat_due)) {
            break;
          }

          prepare_data_packet(packet, current_level);
          DEBUG_PRINT("[TX] %s Seq:%lu L:%d%% B:%d%% Q:%u%% R:%u F:%d\r\n",
                      level_changed ? "CHG" : "HB",
                      (unsigned long)g_runtime.seq_num, current_level,
                      g_runtime.current_battery, g_runtime.link_quality,
                      g_runtime.last_rpd_seen, g_probe_fault);

          send_attempted = true;
          tx_send_result_t tx_result =
              send_data_with_retry(&g_runtime, packet, &current_level);

          if (tx_result == TX_SEND_RESTART_LEVEL) {
            g_runtime.retry_cooldown = 0;
            g_runtime.seq_num++;
            continue;
          }

          if (tx_result == TX_SEND_ABORTED) {
            tx_aborted = true;
            break;
          }

          ack_received = (tx_result == TX_SEND_ACKED);
          show_tx_feedback = true;
          if (!ack_received) {
            DEBUG_PRINT("[TX] NO ACK after %d tries\r\n", DATA_MAX_RETRIES);
            g_runtime.retry_cooldown = 4;
          } else {
            g_runtime.last_sent_level = current_level;
            g_runtime.seq_num++;
            g_runtime.retry_cooldown = 0;
          }
          break;
        }

        if (tx_aborted) {
          continue;
        }

        if (send_attempted) {
          g_runtime.sleep_cycle_count = 0;

          if (show_tx_feedback) {
            // LED: fast blink=ACK ok, slow blink=no ACK
            GPIO_ResetBits(GPIOD, LED_PIN);
            Delay_Ms(ack_received ? 50 : 500);
            GPIO_SetBits(GPIOD, LED_PIN);
          }
        }
      }
    } else {
      DEBUG_PRINT("[SYS] Not paired. Hold button to pair.\r\n");
    }

    // 3. Enter Deep Sleep (Saves battery in both paired and un-paired mode)
    DEBUG_PRINT("[SYS] Sleep\r\n");
    DEBUG_PRINT("----------\r\n");
    enter_deep_sleep();
    g_runtime.sleep_cycle_count++; // Increment cycles on each AWU wake
  }
}
