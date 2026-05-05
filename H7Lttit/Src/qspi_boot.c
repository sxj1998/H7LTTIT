#include "stm32h7xx.h"

#define BOOT_QSPI_TIMEOUT 0x100000UL

static void boot_gpio_af(GPIO_TypeDef *gpio, uint32_t pin, uint32_t af)
{
  uint32_t shift = pin * 2UL;
  uint32_t afr_shift = (pin & 0x7UL) * 4UL;

  gpio->MODER = (gpio->MODER & ~(0x3UL << shift)) | (0x2UL << shift);
  gpio->OTYPER &= ~(1UL << pin);
  gpio->OSPEEDR |= (0x3UL << shift);
  gpio->PUPDR &= ~(0x3UL << shift);

  if (pin < 8UL)
  {
    gpio->AFR[0] = (gpio->AFR[0] & ~(0xFUL << afr_shift)) | (af << afr_shift);
  }
  else
  {
    gpio->AFR[1] = (gpio->AFR[1] & ~(0xFUL << afr_shift)) | (af << afr_shift);
  }
}

static void boot_qspi_wait_not_busy(void)
{
  uint32_t timeout = BOOT_QSPI_TIMEOUT;

  while (((QUADSPI->SR & QUADSPI_SR_BUSY) != 0UL) && (timeout != 0UL))
  {
    timeout--;
  }
}

static void boot_qspi_delay(uint32_t cycles)
{
  while (cycles != 0UL)
  {
    __NOP();
    cycles--;
  }
}

static void boot_qspi_cmd(uint32_t instruction, uint32_t instruction_mode)
{
  boot_qspi_wait_not_busy();
  QUADSPI->FCR = QUADSPI_FCR_CTEF | QUADSPI_FCR_CTCF |
                 QUADSPI_FCR_CSMF | QUADSPI_FCR_CTOF;
  QUADSPI->CCR = instruction |
                 (instruction_mode << QUADSPI_CCR_IMODE_Pos);

  for (uint32_t timeout = BOOT_QSPI_TIMEOUT; timeout != 0UL; timeout--)
  {
    if ((QUADSPI->SR & (QUADSPI_SR_TCF | QUADSPI_SR_TEF)) != 0UL)
    {
      break;
    }
  }

  QUADSPI->FCR = QUADSPI_FCR_CTEF | QUADSPI_FCR_CTCF |
                 QUADSPI_FCR_CSMF | QUADSPI_FCR_CTOF;
}

void Boot_QSPI_MemoryMapped(void)
{
  volatile uint32_t tmp;

  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN | RCC_AHB4ENR_GPIOEEN;
  tmp = RCC->AHB4ENR;
  (void)tmp;

  boot_gpio_af(GPIOE, 2UL, 9UL);   /* BK1_IO2 */
  boot_gpio_af(GPIOB, 2UL, 9UL);   /* CLK */
  boot_gpio_af(GPIOD, 11UL, 9UL);  /* BK1_IO0 */
  boot_gpio_af(GPIOD, 12UL, 9UL);  /* BK1_IO1 */
  boot_gpio_af(GPIOD, 13UL, 9UL);  /* BK1_IO3 */
  boot_gpio_af(GPIOB, 6UL, 10UL);  /* NCS */

  RCC->D1CCIPR &= ~RCC_D1CCIPR_QSPISEL;
  RCC->AHB3ENR |= RCC_AHB3ENR_QSPIEN;
  tmp = RCC->AHB3ENR;
  (void)tmp;

  RCC->AHB3RSTR |= RCC_AHB3RSTR_QSPIRST;
  RCC->AHB3RSTR &= ~RCC_AHB3RSTR_QSPIRST;

  boot_qspi_wait_not_busy();
  QUADSPI->CR = 0UL;
  QUADSPI->DCR = QUADSPI_DCR_CKMODE |
                 (7UL << QUADSPI_DCR_CSHT_Pos) |
                 (23UL << QUADSPI_DCR_FSIZE_Pos);
  QUADSPI->CR = QUADSPI_CR_SSHIFT |
                (31UL << QUADSPI_CR_FTHRES_Pos) |
                (1UL << QUADSPI_CR_PRESCALER_Pos) |
                QUADSPI_CR_EN;

  boot_qspi_cmd(0x66UL, 1UL);
  boot_qspi_cmd(0x99UL, 1UL);
  boot_qspi_delay(10000UL);
  boot_qspi_cmd(0x66UL, 3UL);
  boot_qspi_cmd(0x99UL, 3UL);
  boot_qspi_delay(10000UL);

  QUADSPI->FCR = QUADSPI_FCR_CTEF | QUADSPI_FCR_CTCF |
                 QUADSPI_FCR_CSMF | QUADSPI_FCR_CTOF;
  QUADSPI->ABR = 0xEFUL;
  QUADSPI->CCR = 0xEBUL |
                 (1UL << QUADSPI_CCR_IMODE_Pos) |
                 (3UL << QUADSPI_CCR_ADMODE_Pos) |
                 (2UL << QUADSPI_CCR_ADSIZE_Pos) |
                 (3UL << QUADSPI_CCR_ABMODE_Pos) |
                 (0UL << QUADSPI_CCR_ABSIZE_Pos) |
                 (4UL << QUADSPI_CCR_DCYC_Pos) |
                 (3UL << QUADSPI_CCR_DMODE_Pos) |
                 (3UL << QUADSPI_CCR_FMODE_Pos) |
                 QUADSPI_CCR_SIOO;
}
