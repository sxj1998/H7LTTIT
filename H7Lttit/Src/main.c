#include "main.h"
#include "comm.h"
#include "debug.h"
#include "fs_port.h"
#include "gpio.h"
#include "heap.h"
#include "lcd.h"
#include "littlefs_sd.h"
#include "quadspi.h"
#include "rtc.h"
#include "sdmmc.h"
#include "shell.h"
#include "spi.h"
#include "tim.h"
#include "usb_device.h"
#include "usb_rndis_lwip.h"
#include "vfs_port.h"
#include "w25q_layout.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

static const uint32_t kUartBaudrate = 115200U;
#define ENABLE_STRESS_TEST 0U
#define ENABLE_PERIODIC_STATUS_PRINT 0U
#define ENABLE_SERIAL_SHELL 1U

static void MPU_Config(void);
static void CPU_CACHE_Enable(void);
static void USART1_Init(void);
static void USART1_WriteChar(uint8_t ch);
static void StartDefaultTask(void *argument);
#if (ENABLE_SERIAL_SHELL != 0U)
static void StartShellTask(void *argument);
#endif
#if (ENABLE_STRESS_TEST != 0U)
static void StartStressTask(void *argument);
#endif
static void StartBoardPeripheralsTask(void *argument);
static void BoardLcdShowLine(uint16_t y, const char *text);
#if (ENABLE_SERIAL_SHELL == 0U)
static void BoardLcdShowResources(void);
#endif
static void BoardFlashTest(void);
static void BoardSdLittleFsTest(void);

void SystemClock_Config(void);

static uint8_t s_board_flash_sample[16];
#if (ENABLE_SERIAL_SHELL != 0U)
static struct superblock s_shell_fs_sb;
static volatile uint8_t s_shell_ready;
#endif

#ifdef W25Qxx
extern uint8_t Image$$ER_IROM2$$Length[];
#else
extern uint8_t Image$$ER_IROM1$$Length[];
#endif
extern uint8_t Image$$RW_IRAM2$$Length[];

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
  heap_init();
  comm_init_uart(NULL);
  HAL_PWREx_EnableUSBVoltageDetector();
  MX_USB_DEVICE_Init();
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);

  printf("H7Lttit basic framework started\r\n");
  printf("Peripherals: GPIO, USART1, USB RNDIS + LwIP, SPI4 LCD, QSPI Flash, SDMMC1, TIM1 PWM, RTC stub\r\n");
  printf("RNDIS static IP: 192.168.7.1/24, MAC: 02:12:34:56:78:9A\r\n");
  printf("W25Q XIP code capacity: %lu bytes (%lu KB)\r\n",
         (unsigned long)W25Q_CODE_SIZE,
         (unsigned long)(W25Q_CODE_SIZE / 1024UL));

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
                  3072U,
                  NULL,
                  tskIDLE_PRIORITY + 1U,
                  NULL) != pdPASS)
  {
    Error_Handler();
  }

#if (ENABLE_SERIAL_SHELL != 0U)
  if (xTaskCreate(StartShellTask,
                  "shell",
                  1024U,
                  NULL,
                  tskIDLE_PRIORITY + 1U,
                  NULL) != pdPASS)
  {
    Error_Handler();
  }
#endif

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
  MPU_InitStruct.BaseAddress = QSPI_BASE;
  MPU_InitStruct.Size = MPU_REGION_SIZE_16MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_PRIV_RO;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
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
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_FIFOEN |
                USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;

  HAL_NVIC_SetPriority(USART1_IRQn, 15U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
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

#if (ENABLE_PERIODIC_STATUS_PRINT != 0U)
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
#else
    if (++print_ticks >= 100U)
    {
      print_ticks = 0U;
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
#endif

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
  }
}

#if (ENABLE_SERIAL_SHELL != 0U)
static void StartShellTask(void *argument)
{
  (void)argument;

  while (s_shell_ready == 0U)
  {
    vTaskDelay(pdMS_TO_TICKS(20U));
  }

  while (1)
  {
    shell_poll();
    vTaskDelay(pdMS_TO_TICKS(2U));
  }
}
#endif

#if (ENABLE_STRESS_TEST != 0U)
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
#endif

static void StartBoardPeripheralsTask(void *argument)
{
  char line[32];

  (void)argument;

  printf("board_io: lcd init\r\n");
  LCD_InitSimple();
  BoardLcdShowLine(0U, "H7Lttit RTOS");
  BoardLcdShowLine(16U, "RNDIS running");

  BoardFlashTest();
  BoardSdLittleFsTest();

#if (ENABLE_SERIAL_SHELL != 0U)
  fs_port_init();
  if (fs_port_mount(&s_shell_fs_sb) == 0)
  {
    vfs_port_init();
    shell_init();
    s_shell_ready = 1U;
    printf("shell ready: help | ls | cat | touch | write | mkdir | cd | vfs | vim\r\n");
    BoardLcdShowLine(64U, "Shell ready");
  }
  else
  {
    printf("shell fs mount failed\r\n");
    BoardLcdShowLine(64U, "Shell fs err");
  }
#endif

  while (1)
  {
    (void)line;
#if (ENABLE_SERIAL_SHELL != 0U)
    BoardLcdShowLine(80U, "UART shell COM3");
#else
    BoardLcdShowResources();
#endif
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

static void BoardLcdShowLine(uint16_t y, const char *text)
{
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, y, ST7735Ctx.Width, 16, BLACK);
  LCD_ShowString(0, y, ST7735Ctx.Width, 16, 16, (uint8_t *)text);
}

#if (ENABLE_SERIAL_SHELL == 0U)
static void BoardLcdShowResources(void)
{
#ifdef W25Qxx
  static const uint32_t kCodeFlashSize = W25Q_CODE_SIZE;
#else
  static const uint32_t kCodeFlashSize = 128UL * 1024UL;
#endif
  static const uint32_t kAxiSramSize = 512UL * 1024UL;
#ifdef W25Qxx
  const uint32_t flash_used = (uint32_t)Image$$ER_IROM2$$Length;
#else
  const uint32_t flash_used = (uint32_t)Image$$ER_IROM1$$Length;
#endif
  const uint32_t ram_static = (uint32_t)Image$$RW_IRAM2$$Length;
  const uint32_t heap_free = (uint32_t)xPortGetFreeHeapSize();
  const uint32_t heap_used = (uint32_t)configTOTAL_HEAP_SIZE - heap_free;
  uint32_t sd_total_kb = 0U;
  uint32_t sd_used_kb = 0U;
  uint32_t sd_used_percent = 0U;
  int sd_err = LFS_ERR_OK;
  char line[32];

  sprintf(line,
          "Flash %lu/%luK %lu%%",
          (unsigned long)((flash_used + 1023UL) / 1024UL),
          (unsigned long)(kCodeFlashSize / 1024UL),
          (unsigned long)((flash_used * 100UL) / kCodeFlashSize));
  BoardLcdShowLine(0U, line);

  sprintf(line,
          "RAM %lu/%luK %lu%%",
          (unsigned long)((ram_static + 1023UL) / 1024UL),
          (unsigned long)(kAxiSramSize / 1024UL),
          (unsigned long)((ram_static * 100UL) / kAxiSramSize));
  BoardLcdShowLine(16U, line);

  sprintf(line,
          "Heap %lu/%luK %lu%%",
          (unsigned long)((heap_used + 1023UL) / 1024UL),
          (unsigned long)(configTOTAL_HEAP_SIZE / 1024UL),
          (unsigned long)((heap_used * 100UL) / (uint32_t)configTOTAL_HEAP_SIZE));
  BoardLcdShowLine(32U, line);

  sd_err = LittleFs_SdMount(0U);
  if (sd_err == LFS_ERR_OK)
  {
    lfs_ssize_t used_blocks = lfs_fs_size(LittleFs_SdGetHandle());
    uint32_t total_blocks = LittleFs_SdGetBlockCount();

    if ((used_blocks >= 0) && (total_blocks != 0U))
    {
      sd_total_kb = total_blocks / 2UL;
      sd_used_kb = (uint32_t)used_blocks / 2UL;
      sd_used_percent = ((uint32_t)used_blocks * 100UL) / total_blocks;
    }
    else
    {
      sd_err = (int)used_blocks;
    }
    LittleFs_SdUnmount();
  }

  if (sd_total_kb != 0U)
  {
    sprintf(line,
            "SD %lu/%luM %lu%%",
            (unsigned long)((sd_used_kb + 1023UL) / 1024UL),
            (unsigned long)(sd_total_kb / 1024UL),
            (unsigned long)sd_used_percent);
  }
  else
  {
    sprintf(line, "SD LFS E%d", sd_err);
  }
  BoardLcdShowLine(48U, line);
}
#endif

static void BoardFlashTest(void)
{
  const volatile uint8_t *xip = (const volatile uint8_t *)QSPI_BASE;
  char line[32];

  printf("board_io: qspi xip read\r\n");
  memset(s_board_flash_sample, 0, sizeof(s_board_flash_sample));

  for (uint32_t i = 0U; i < sizeof(s_board_flash_sample); i++)
  {
    s_board_flash_sample[i] = xip[i];
  }

  printf("qspi xip first=%02X %02X %02X %02X\r\n",
         s_board_flash_sample[0],
         s_board_flash_sample[1],
         s_board_flash_sample[2],
         s_board_flash_sample[3]);

  sprintf(line, "W25Q XIP %02X%02X%02X%02X",
          s_board_flash_sample[0],
          s_board_flash_sample[1],
          s_board_flash_sample[2],
          s_board_flash_sample[3]);
  BoardLcdShowLine(32U, line);
}

static void BoardSdLittleFsTest(void)
{
  lfs_file_t file;
  int err;
  int32_t mount_count = 0;
  char line[32];

  printf("board_io: sd littlefs mount lba=%lu\r\n",
         (unsigned long)LITTLEFS_SD_START_BLOCK);

  err = LittleFs_SdRawSelfTest();
  if (err != LFS_ERR_OK)
  {
    printf("sd raw test failed err=%d hal=0x%08lX\r\n",
           err,
           (unsigned long)LittleFs_SdGetLastHalError());
  }

  if (err == LFS_ERR_OK)
  {
    err = LittleFs_SdMount(1U);
  }
  if (err == LFS_ERR_OK)
  {
    err = lfs_file_open(LittleFs_SdGetHandle(),
                        &file,
                        "mount_count",
                        LFS_O_RDWR | LFS_O_CREAT);
  }

  if (err == LFS_ERR_OK)
  {
    lfs_ssize_t read_len = lfs_file_read(LittleFs_SdGetHandle(),
                                         &file,
                                         &mount_count,
                                         sizeof(mount_count));
    if (read_len != (lfs_ssize_t)sizeof(mount_count))
    {
      mount_count = 0;
    }

    mount_count++;
    lfs_file_rewind(LittleFs_SdGetHandle(), &file);
    err = (int)lfs_file_write(LittleFs_SdGetHandle(),
                              &file,
                              &mount_count,
                              sizeof(mount_count));
    if (err == (int)sizeof(mount_count))
    {
      err = lfs_file_sync(LittleFs_SdGetHandle(), &file);
    }
    lfs_file_close(LittleFs_SdGetHandle(), &file);
  }

  LittleFs_SdUnmount();

  if (err == LFS_ERR_OK)
  {
    printf("sd littlefs ok mount_count=%ld\r\n", (long)mount_count);
    sprintf(line, "SD LFS %ld", (long)mount_count);
  }
  else
  {
    printf("sd littlefs failed err=%d hal=0x%08lX\r\n",
           err,
           (unsigned long)LittleFs_SdGetLastHalError());
    sprintf(line, "SD LFS E%d", err);
  }
  BoardLcdShowLine(48U, line);
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
