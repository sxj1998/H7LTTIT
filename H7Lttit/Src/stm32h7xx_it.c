#include "main.h"
#include "stm32h7xx_it.h"

#include "FreeRTOS.h"
#include "task.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern SD_HandleTypeDef hsd1;

extern void xPortSysTickHandler(void);

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void DebugMon_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();

  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    xPortSysTickHandler();
  }
}

void OTG_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

void SDMMC1_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd1);
}
