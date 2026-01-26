#include "ch32v00x.h"

#define HSI_VALUE ((uint32_t)24000000)
#define HSE_VALUE ((uint32_t)24000000) // Adjust if using external crystal

/* System Clock Frequency (Core Clock) */
uint32_t SystemCoreClock = 48000000;

/*********************************************************************
 * @fn      SystemInit
 *
 * @brief   Setup the microcontroller system Initialize the Embedded Flash
 * Interface, the PLL and update the SystemCoreClock variable.
 *
 * @return  none
 */
void SystemInit(void) {
  /* HSI is enabled and used as system clock source */
  RCC->CTLR |= (uint32_t)0x00000001;

  /* Reset SW, HPRE, PPRE1, PPRE2, ADCPRE and MCO bits */
  RCC->CFGR0 &= (uint32_t)0xF8FF0000;

  /* Reset HSEON, CSSON and PLLON bits */
  RCC->CTLR &= (uint32_t)0xFEF6FFFF;

  /* Reset HSEBYP bit */
  RCC->CTLR &= (uint32_t)0xFFFBFFFF;

  /* Reset PLLSRC, PLLXTPRE, PLLMUL and USBPRE bits */
  RCC->CFGR0 &= (uint32_t)0xFF3FFFFF;

  /* Disable all interrupts and clear pending bits  */
  RCC->INTR = 0x009F0000;
}

/*********************************************************************
 * @fn      SystemCoreClockUpdate
 *
 * @brief   Update SystemCoreClock variable according to Clock Register Values.
 *
 * @return  none
 */
void SystemCoreClockUpdate(void) {
  uint32_t tmp = 0, pllsource = 0;

  tmp = RCC->CFGR0 & 0x0C; // SWS bits: 0x0C mask

  switch (tmp) {
  case 0x00: /* HSI used as system clock */
    SystemCoreClock = HSI_VALUE;
    break;
  case 0x04: /* HSE used as system clock */
    SystemCoreClock = HSE_VALUE;
    break;
  case 0x08:                             /* PLL used as system clock */
    pllsource = RCC->CFGR0 & 0x00010000; // PLLSRC bit
    if (pllsource == 0x00) {
      /* HSI oscillator clock divided by 2 selected as PLL clock entry */
      SystemCoreClock = (HSI_VALUE >> 1) * 2;
    } else {
      /* HSE selected as PLL clock entry */
      SystemCoreClock = HSE_VALUE * 2;
    }
    break;
  default:
    SystemCoreClock = HSI_VALUE;
    break;
  }
}
