#include "main.h"
#include "debug.h"
#include "gpio.h"
#include "lcd.h"
#include "quadspi.h"
#include "rtc.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "usb_rndis_lwip.h"
#include "w25qxx_qspi.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

static const uint32_t kUartBaudrate = 115200U;
#define ENABLE_STRESS_TEST 1U

static void MPU_Config(void);
static void CPU_CACHE_Enable(void);
static void USART1_Init(void);
static void USART1_WriteChar(uint8_t ch);
static void StartDefaultTask(void *argument);
static void StartStressTask(void *argument);
static void StartBoardPeripheralsTask(void *argument);
static void BoardLcdShowLine(uint16_t y, const char *text);
static void BoardFlashTest(void);
static void BoardSdTest(void);

void SystemClock_Config(void);

__align(32) static uint8_t s_board_sd_block[512];
static uint8_t s_board_flash_sample[16];

int fputc(int ch, FILE *f)
{
  (void)f;

  if (ch == '\n')
  {
    USART1_WriteChar('\r');
  }
  USART1_WriteChar((uint8_t)ch);
  return ch;
}

int main(void)
{
  MPU_Config();
  CPU_CACHE_Enable();

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DEBUG_Init();
  MX_RTC_Init();
  MX_SPI4_Init();
  MX_TIM1_Init();

  USART1_Init();
  HAL_PWREx_EnableUSBVoltageDetector();
  MX_USB_DEVICE_Init();
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);

  printf("H7Lttit basic framework started\r\n");
  printf("Peripherals: GPIO, USART1, USB RNDIS + LwIP, SPI4 LCD, QSPI Flash, SDMMC1, TIM1 PWM, RTC stub\r\n");
  printf("RNDIS static IP: 192.168.7.1/24, MAC: 02:12:34:56:78:9A\r\n");

  if (xTaskCreate(StartDefaultTask,
                  "default",
                  512U,
                  NULL,
                  tskIDLE_PRIORITY + 1U,
                  NULL) != pdPASS)
  {
    Error_Handler();
  }

  if (xTaskCreate(StartBoardPeripheralsTask,
                  "board_io",
                  1024U,
                  NULL,
                  tskIDLE_PRIORITY + 1U,
                  NULL) != pdPASS)
  {
    Error_Handler();
  }

#if (ENABLE_STRESS_TEST != 0U)
  if (xTaskCreate(StartStressTask,
                  "stress",
                  512U,
                  NULL,
                  tskIDLE_PRIORITY + 2U,
                  NULL) != pdPASS)
  {
    Error_Handler();
  }
#endif

  vTaskStartScheduler();
  Error_Handler();
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 10;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    HAL_Delay(100U);
  }
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = QSPI_BASE;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = D1_AXISRAM_BASE;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void CPU_CACHE_Enable(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();
}

static void USART1_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_USART16_CONFIG(RCC_USART16CLKSOURCE_D2PCLK2);

  GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  USART1->CR1 = 0;
  USART1->CR2 = 0;
  USART1->CR3 = 0;
  USART1->PRESC = 0;
  USART1->BRR = HAL_RCC_GetPCLK2Freq() / kUartBaudrate;
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void USART1_WriteChar(uint8_t ch)
{
  while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U)
  {
  }
  USART1->TDR = ch;
}

static void StartDefaultTask(void *argument)
{
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t print_ticks = 0U;

  (void)argument;

  while (1)
  {
    USB_RNDIS_LWIP_Poll();

    if (++print_ticks >= 100U)
    {
      print_ticks = 0U;
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
      printf("tick=%lu key=%u rndis_rx=%lu rndis_tx=%lu\r\n",
             (unsigned long)HAL_GetTick(),
             (unsigned)HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin),
             (unsigned long)USB_RNDIS_LWIP_GetRxCount(),
             (unsigned long)USB_RNDIS_LWIP_GetTxCount());
    }

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
  }
}

static void StartStressTask(void *argument)
{
  static const size_t kAllocSizes[] = {32U, 64U, 128U, 256U, 512U};
  volatile uint32_t accumulator = 0U;
  uint32_t loop_count = 0U;
  TickType_t last_report = xTaskGetTickCount();

  (void)argument;

  while (1)
  {
    for (uint32_t i = 0U; i < 5000U; i++)
    {
      accumulator += (i ^ loop_count) + (accumulator << 1);
    }

    for (uint32_t i = 0U; i < (sizeof(kAllocSizes) / sizeof(kAllocSizes[0])); i++)
    {
      uint8_t *buffer = (uint8_t *)pvPortMalloc(kAllocSizes[i]);

      if (buffer != NULL)
      {
        memset(buffer, (int)(loop_count + i), kAllocSizes[i]);
        accumulator += buffer[0];
        vPortFree(buffer);
      }
    }

    loop_count++;

    if ((xTaskGetTickCount() - last_report) >= pdMS_TO_TICKS(5000U))
    {
      last_report = xTaskGetTickCount();
      printf("stress loops=%lu heap_free=%lu heap_min=%lu stress_stack_min=%lu acc=%lu\r\n",
             (unsigned long)loop_count,
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)xPortGetMinimumEverFreeHeapSize(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL),
             (unsigned long)accumulator);
    }

    vTaskDelay(pdMS_TO_TICKS(1U));
  }
}

static void StartBoardPeripheralsTask(void *argument)
{
  char line[32];

  (void)argument;

  printf("board_io: lcd init\r\n");
  LCD_Test();
  BoardLcdShowLine(4U, "H7Lttit RTOS");
  BoardLcdShowLine(22U, "RNDIS running");

  BoardFlashTest();
  BoardSdTest();

  while (1)
  {
    sprintf(line, "tick %lu", (unsigned long)HAL_GetTick());
    BoardLcdShowLine(58U, line);
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

static void BoardLcdShowLine(uint16_t y, const char *text)
{
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, y, ST7735Ctx.Width, 16, BLACK);
  LCD_ShowString(0, y, ST7735Ctx.Width, 16, 16, (uint8_t *)text);
}

static void BoardFlashTest(void)
{
  uint16_t flash_id;
  uint8_t read_result;
  char line[32];

  printf("board_io: qspi flash init\r\n");
  w25qxx_Init();
  flash_id = w25qxx_GetID();
  memset(s_board_flash_sample, 0, sizeof(s_board_flash_sample));
  read_result = W25qxx_Read(s_board_flash_sample, 0U, sizeof(s_board_flash_sample));

  printf("qspi flash id=0x%04X read=%u first=%02X %02X %02X %02X\r\n",
         flash_id,
         read_result,
         s_board_flash_sample[0],
         s_board_flash_sample[1],
         s_board_flash_sample[2],
         s_board_flash_sample[3]);

  sprintf(line, "Flash 0x%04X %s", flash_id, (read_result == HAL_OK) ? "OK" : "ERR");
  BoardLcdShowLine(40U, line);
}

static void BoardSdTest(void)
{
  HAL_SD_CardInfoTypeDef card_info;
  HAL_StatusTypeDef read_result = HAL_ERROR;
  char line[32];

  printf("board_io: sdmmc init\r\n");
  MX_SDMMC1_SD_Init();
  if (g_sdmmc1_init_ok != 0U)
  {
    if (HAL_SD_GetCardInfo(&hsd1, &card_info) == HAL_OK)
    {
      read_result = HAL_SD_ReadBlocks(&hsd1, s_board_sd_block, 0U, 1U, 1000U);
      printf("sd card blocks=%lu block_size=%lu read=%u first=%02X %02X %02X %02X\r\n",
             (unsigned long)card_info.BlockNbr,
             (unsigned long)card_info.BlockSize,
             (unsigned)read_result,
             s_board_sd_block[0],
             s_board_sd_block[1],
             s_board_sd_block[2],
             s_board_sd_block[3]);
    }
    else
    {
      printf("sd card info read failed\r\n");
    }
  }
  else
  {
    printf("sd card init failed or no card inserted\r\n");
  }

  sprintf(line, "SD %s", (read_result == HAL_OK) ? "READ OK" : "NO CARD");
  BoardLcdShowLine(58U, line);
}

void vApplicationMallocFailedHook(void)
{
  Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
  (void)task;
  (void)task_name;
  Error_Handler();
}
