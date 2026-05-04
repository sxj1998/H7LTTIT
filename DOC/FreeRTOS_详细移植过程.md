# FreeRTOS 详细移植过程

## 1. 工程基础

- MCU：`STM32H750VBTx`
- 内核：Cortex-M7
- 工具链：Keil MDK-ARM 5.35，ARMCC 5.06 update 7
- 工程文件：`H7Lttit/MDK-ARM/H7Lttit.uvprojx`
- FreeRTOS 来源：`STM32Cube_FW_H7_V1.11.2/Middlewares/Third_Party/FreeRTOS`
- 移植方式：使用 FreeRTOS 原生 API，不引入 CMSIS-RTOS 封装层

## 2. 复制 FreeRTOS 源码

把 FreeRTOS 复制到工程内，保证工程自包含：

```text
H7Lttit/Middlewares/Third_Party/FreeRTOS
```

实际加入工程编译的核心文件：

```text
Source/tasks.c
Source/queue.c
Source/list.c
Source/timers.c
Source/event_groups.c
Source/stream_buffer.c
Source/portable/RVDS/ARM_CM7/r0p1/port.c
Source/portable/MemMang/heap_4.c
```

保留的头文件目录：

```text
Source/include
Source/portable/RVDS/ARM_CM7/r0p1
```

## 3. 修改 Keil 工程

在 `H7Lttit.uvprojx` 中增加 include path：

```text
../Middlewares/Third_Party/FreeRTOS/Source/include
../Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1
```

新增 Keil 分组：

```text
Middlewares/FreeRTOS/Core
Middlewares/FreeRTOS/Portable
```

`Middlewares/FreeRTOS/Core` 中加入：

```text
tasks.c
queue.c
list.c
timers.c
event_groups.c
stream_buffer.c
```

`Middlewares/FreeRTOS/Portable` 中加入：

```text
port.c
heap_4.c
```

## 4. 添加 `FreeRTOSConfig.h`

新增文件：

```text
H7Lttit/Inc/FreeRTOSConfig.h
```

关键配置：

```c
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)
#define configTOTAL_HEAP_SIZE                   ((size_t)(24 * 1024))
#define configUSE_TIMERS                        1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
```

中断优先级配置：

```c
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         0xF0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x50
```

说明：

- STM32H7 使用 4 bit 抢占优先级，最低优先级为 15。
- FreeRTOS 内核中断使用最低优先级 `0xF0`。
- 允许调用 FreeRTOS API 的中断优先级不能高于 `5`。
- 当前 USB FS 中断优先级为 `6`，低于 FreeRTOS API 门限，兼容 FreeRTOS。

## 5. 映射异常处理函数

FreeRTOS Cortex-M7 port 需要接管：

- `SVC_Handler`
- `PendSV_Handler`
- `SysTick_Handler`

在 `FreeRTOSConfig.h` 中映射：

```c
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
```

这样启动文件中的弱符号会被 FreeRTOS port 提供的函数覆盖。

## 6. 处理 SysTick

原工程 `SysTick_Handler()` 只调用 `HAL_IncTick()`。移植 FreeRTOS 后修改为：

```c
void SysTick_Handler(void)
{
  HAL_IncTick();

  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    xPortSysTickHandler();
  }
}
```

这样做的原因：

- `HAL_IncTick()` 继续维护 HAL 的毫秒 tick。
- 调度器启动后，`xPortSysTickHandler()` 驱动 FreeRTOS tick。
- 调度器启动前不调用 FreeRTOS tick，避免内核状态未初始化时进入调度逻辑。

## 7. 修改 `main.c`

加入头文件：

```c
#include "FreeRTOS.h"
#include "task.h"
```

把原来的 `while (1)` 拆成任务函数：

```c
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
```

创建任务并启动调度器：

```c
if (xTaskCreate(StartDefaultTask,
                "default",
                512U,
                NULL,
                tskIDLE_PRIORITY + 1U,
                NULL) != pdPASS)
{
  Error_Handler();
}

vTaskStartScheduler();
Error_Handler();
```

## 8. 添加 FreeRTOS hook

`main.c` 中添加：

```c
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
```

作用：

- FreeRTOS heap 分配失败时进入错误处理。
- 任务栈溢出时进入错误处理。
- 后续调试时可以在这两个 hook 中打断点。

## 9. 解决 ARMCC 汇编立即数问题

FreeRTOS CM7 RVDS port 中使用：

```asm
mov r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY
```

ARMCC 汇编器要求这里展开为明确常量。最初如果写成表达式：

```c
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
```

可能触发：

```text
error: A1586E: Bad operand types
```

因此当前配置直接写成常量：

```c
#define configKERNEL_INTERRUPT_PRIORITY      0xF0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50
```

## 10. 编译验证

全量编译命令：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -r 'H7Lttit\MDK-ARM\H7Lttit.uvprojx' -j0
```

编译结果：

```text
0 Error(s), 0 Warning(s)
Program Size: Code=60200 RO-data=1164 RW-data=480 ZI-data=78648
```

资源占用：

```text
Total RO  Size: 61364 bytes, 59.93 KB
Total RW  Size: 79128 bytes, 77.27 KB
Total ROM Size: 61576 bytes, 60.13 KB
```

## 11. 烧录和运行验证

烧录命令：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -f 'H7Lttit\MDK-ARM\H7Lttit.uvprojx' -j0
```

J-Link 读回校验结果：

```text
J-Link S/N: 69606476
VTref=3.325V
Cortex-M7 identified
Reading 61576 bytes data from target memory @ 0x08000000.
Verify successful.
```

运行态辅助验证：

- Windows 能枚举 RNDIS 网卡。
- `ping 192.168.7.1 -n 4` 成功，0% 丢包。
- 说明 FreeRTOS 默认任务、RNDIS 轮询和 USB 中断协同运行正常。

## 12. 后续扩展建议

- 新增任务时，优先估算每个任务栈，并开启 `uxTaskGetStackHighWaterMark()` 观察水位。
- 如果任务数量增加，检查 `configTOTAL_HEAP_SIZE` 是否足够。
- 串口 `printf` 当前是轮询发送；多任务同时打印前应加互斥锁。
- 当前 LwIP 是 `NO_SYS=1`，不要在多个任务里同时直接调用 LwIP raw API。
