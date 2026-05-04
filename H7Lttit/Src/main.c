#include "main.h"
#include "debug.h"
#include "gpio.h"
#include "lcd.h"
#include "littlefs_flash.h"
#include "littlefs_sd.h"
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
#define ENABLE_STRESS_TEST 0U
#define ENABLE_PERIODIC_STATUS_PRINT 0U
#define ENABLE_LITTLEFS_STRESS_TEST 1U
#define LITTLEFS_STRESS_MAX_ITERATIONS 200U
#define LITTLEFS_STRESS_REPORT_INTERVAL 10U
#define LITTLEFS_STRESS_DATA_SIZE 512U

static void MPU_Config(void);
static void CPU_CACHE_Enable(void);
static void USART1_Init(void);
static void USART1_WriteChar(uint8_t ch);
static void StartDefaultTask(void *argument);
#if (ENABLE_STRESS_TEST != 0U)
static void StartStressTask(void *argument);
#endif
static void StartBoardPeripheralsTask(void *argument);
static void BoardLcdShowLine(uint16_t y, const char *text);
static void BoardLcdShowResources(void);
static void BoardFlashTest(void);
static void BoardLittleFsTest(void);
static void BoardSdLittleFsTest(void);
static void BoardLittleFsStressTest(void);
static int BoardLittleFsWriteVerify(lfs_t *fs, const char *path, uint32_t iteration);

void SystemClock_Config(void);

static uint8_t s_board_flash_sample[16];
static uint8_t s_lfs_stress_write[LITTLEFS_STRESS_DATA_SIZE];
static uint8_t s_lfs_stress_read[LITTLEFS_STRESS_DATA_SIZE];

extern uint8_t Image$$ER_IROM1$$Length[];
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
                  3072U,
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
  BoardLittleFsTest();

#if (ENABLE_LITTLEFS_STRESS_TEST != 0U)
  BoardLittleFsStressTest();
#endif

  while (1)
  {
    (void)line;
    BoardLcdShowResources();
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

static void BoardLcdShowLine(uint16_t y, const char *text)
{
  ST7735_LCD_Driver.FillRect(&st7735_pObj, 0, y, ST7735Ctx.Width, 16, BLACK);
  LCD_ShowString(0, y, ST7735Ctx.Width, 16, 16, (uint8_t *)text);
}

static void BoardLcdShowResources(void)
{
  static const uint32_t kInternalFlashSize = 128UL * 1024UL;
  static const uint32_t kAxiSramSize = 512UL * 1024UL;
  const uint32_t flash_used = (uint32_t)Image$$ER_IROM1$$Length;
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
          (unsigned long)(kInternalFlashSize / 1024UL),
          (unsigned long)((flash_used * 100UL) / kInternalFlashSize));
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
  BoardLcdShowLine(32U, line);
}

static void BoardLittleFsTest(void)
{
  lfs_file_t file;
  int err;
  int32_t boot_count = 0;
  char line[32];

  printf("board_io: littlefs mount base=0x%08lX size=%lu\r\n",
         (unsigned long)LITTLEFS_FLASH_BASE_ADDR,
         (unsigned long)LITTLEFS_FLASH_SIZE);

  err = LittleFs_FlashMount(1U);
  if (err == LFS_ERR_OK)
  {
    err = lfs_file_open(LittleFs_FlashGetHandle(),
                        &file,
                        "boot_count",
                        LFS_O_RDWR | LFS_O_CREAT);
  }

  if (err == LFS_ERR_OK)
  {
    lfs_ssize_t read_len = lfs_file_read(LittleFs_FlashGetHandle(),
                                         &file,
                                         &boot_count,
                                         sizeof(boot_count));
    if (read_len != (lfs_ssize_t)sizeof(boot_count))
    {
      boot_count = 0;
    }

    boot_count++;
    lfs_file_rewind(LittleFs_FlashGetHandle(), &file);
    err = (int)lfs_file_write(LittleFs_FlashGetHandle(),
                              &file,
                              &boot_count,
                              sizeof(boot_count));
    if (err == (int)sizeof(boot_count))
    {
      err = lfs_file_sync(LittleFs_FlashGetHandle(), &file);
    }
    lfs_file_close(LittleFs_FlashGetHandle(), &file);
  }

  LittleFs_FlashUnmount();

  if (err == LFS_ERR_OK)
  {
    printf("littlefs ok boot_count=%ld\r\n", (long)boot_count);
    sprintf(line, "LFS cnt %ld", (long)boot_count);
  }
  else
  {
    printf("littlefs failed err=%d\r\n", err);
    sprintf(line, "LFS ERR %d", err);
  }
  BoardLcdShowLine(48U, line);
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

static void BoardLittleFsStressTest(void)
{
  uint32_t iteration = 0U;
  TickType_t start_ticks = xTaskGetTickCount();
  char line[32];

  printf("littlefs stress: start data=%lu report=%lu\r\n",
         (unsigned long)LITTLEFS_STRESS_DATA_SIZE,
         (unsigned long)LITTLEFS_STRESS_REPORT_INTERVAL);
  BoardLcdShowLine(64U, "LFS stress run");

  while (1)
  {
    int flash_err;
    int sd_err;

    iteration++;

    flash_err = LittleFs_FlashMount(1U);
    if (flash_err == LFS_ERR_OK)
    {
      flash_err = BoardLittleFsWriteVerify(LittleFs_FlashGetHandle(),
                                           "flash_stress.bin",
                                           iteration);
    }
    LittleFs_FlashUnmount();

    if (flash_err != LFS_ERR_OK)
    {
      printf("littlefs stress: flash failed iter=%lu err=%d\r\n",
             (unsigned long)iteration,
             flash_err);
      sprintf(line, "F stress E%d", flash_err);
      BoardLcdShowLine(64U, line);
      break;
    }

    sd_err = LittleFs_SdMount(1U);
    if (sd_err == LFS_ERR_OK)
    {
      sd_err = BoardLittleFsWriteVerify(LittleFs_SdGetHandle(),
                                        "sd_stress.bin",
                                        iteration);
    }
    LittleFs_SdUnmount();

    if (sd_err != LFS_ERR_OK)
    {
      printf("littlefs stress: sd failed iter=%lu err=%d hal=0x%08lX\r\n",
             (unsigned long)iteration,
             sd_err,
             (unsigned long)LittleFs_SdGetLastHalError());
      sprintf(line, "SD stress E%d", sd_err);
      BoardLcdShowLine(64U, line);
      break;
    }

    if ((iteration % LITTLEFS_STRESS_REPORT_INTERVAL) == 0U)
    {
      uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);

      printf("littlefs stress: iter=%lu elapsed=%lums heap_free=%lu heap_min=%lu stack_min=%lu\r\n",
             (unsigned long)iteration,
             (unsigned long)elapsed_ms,
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)xPortGetMinimumEverFreeHeapSize(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL));
      sprintf(line, "LFS stress %lu", (unsigned long)iteration);
      BoardLcdShowLine(64U, line);
    }

    if (iteration >= LITTLEFS_STRESS_MAX_ITERATIONS)
    {
      uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - start_ticks) * portTICK_PERIOD_MS);

      printf("littlefs stress: PASS iter=%lu elapsed=%lums heap_free=%lu heap_min=%lu stack_min=%lu\r\n",
             (unsigned long)iteration,
             (unsigned long)elapsed_ms,
             (unsigned long)xPortGetFreeHeapSize(),
             (unsigned long)xPortGetMinimumEverFreeHeapSize(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL));
      sprintf(line, "LFS PASS %lu", (unsigned long)iteration);
      BoardLcdShowLine(64U, line);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10U));
  }

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

static int BoardLittleFsWriteVerify(lfs_t *fs, const char *path, uint32_t iteration)
{
  lfs_file_t file;
  int err;

  for (uint32_t i = 0U; i < LITTLEFS_STRESS_DATA_SIZE; i++)
  {
    s_lfs_stress_write[i] = (uint8_t)(iteration + (i * 31U) + (i >> 3));
    s_lfs_stress_read[i] = 0U;
  }

  err = lfs_file_open(fs, &file, path, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
  if (err != LFS_ERR_OK)
  {
    return err;
  }

  err = (int)lfs_file_write(fs, &file, s_lfs_stress_write, LITTLEFS_STRESS_DATA_SIZE);
  if (err == (int)LITTLEFS_STRESS_DATA_SIZE)
  {
    err = lfs_file_sync(fs, &file);
  }
  lfs_file_close(fs, &file);
  if (err != LFS_ERR_OK)
  {
    return (err > 0) ? LFS_ERR_IO : err;
  }

  err = lfs_file_open(fs, &file, path, LFS_O_RDONLY);
  if (err != LFS_ERR_OK)
  {
    return err;
  }

  err = (int)lfs_file_read(fs, &file, s_lfs_stress_read, LITTLEFS_STRESS_DATA_SIZE);
  lfs_file_close(fs, &file);
  if (err != (int)LITTLEFS_STRESS_DATA_SIZE)
  {
    return (err >= 0) ? LFS_ERR_IO : err;
  }

  if (memcmp(s_lfs_stress_read, s_lfs_stress_write, LITTLEFS_STRESS_DATA_SIZE) != 0)
  {
    return LFS_ERR_CORRUPT;
  }

  return LFS_ERR_OK;
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
