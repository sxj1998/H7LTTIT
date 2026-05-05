# LVGL 详细移植过程

## 1. 工程基础

- MCU：`STM32H750VBTx`
- 内核：`Cortex-M7`
- 工具链：Keil MDK-ARM 5.35，ARMCC 5.06 update 7
- 工程文件：`H7Lttit/MDK-ARM/H7Lttit.uvprojx`
- 显示屏：ST7735 SPI LCD，当前工程使用 `TFT96`
- 显示接口：`SPI4`
- RTOS：FreeRTOS
- LVGL 版本：工程内置 `Middlewares/Third_Party/lvgl`
- LVGL 端口文件：`H7Lttit/lvgl_port/lv_port_lvgl.c`

当前移植目标：

- 在 ST7735 LCD 上运行 LVGL。
- 使用 RGB565 部分刷新。
- 在 LVGL 页面中显示 ROM、RAM、SD 卡资源占用环形图标。
- 保留 COM3 串口 shell 调试能力。

## 2. 添加 LVGL 源码

将 LVGL 源码放入工程目录：

```text
H7Lttit/Middlewares/Third_Party/lvgl
```

当前提交中保留了编译需要的主要目录：

```text
Middlewares/Third_Party/lvgl/src
Middlewares/Third_Party/lvgl/include
Middlewares/Third_Party/lvgl/lvgl.h
Middlewares/Third_Party/lvgl/lvgl_private.h
Middlewares/Third_Party/lvgl/lv_version.h
Middlewares/Third_Party/lvgl/LICENCE.txt
Middlewares/Third_Party/lvgl/COPYRIGHTS.md
Middlewares/Third_Party/lvgl/README.md
```

没有纳入固件编译的上游目录，例如 `docs`、`tests`、`demos`、`.github` 等，不是当前固件运行必需项。

## 3. Keil 工程配置

在 `H7Lttit.uvprojx` 中增加宏：

```text
LV_CONF_INCLUDE_SIMPLE
LV_DISABLE_API_MAPPING
```

当前完整宏定义包含：

```text
USE_HAL_DRIVER,STM32H750xx,TFT96,LFS_NO_ASSERT,LFS_NO_ERROR,W25Qxx,LV_CONF_INCLUDE_SIMPLE,LV_DISABLE_API_MAPPING
```

增加 include path：

```text
../lvgl_port
../Middlewares/Third_Party/lvgl
../Middlewares/Third_Party/lvgl/src
../Middlewares/Third_Party/lvgl/include
```

新增 Keil 分组：

```text
LVGL
```

该分组中加入：

- `lv_port_lvgl.c`
- LVGL core、display、draw、misc、stdlib、theme、tick 源文件
- `lv_label.c`
- `lv_arc.c`

本工程资源页面只使用 label 和 arc 控件，所以 `lv_arc.c` 是资源环显示的关键新增文件。

## 4. 添加 `lv_conf.h`

新增配置文件：

```text
H7Lttit/Inc/lv_conf.h
```

关键配置：

```c
#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_MEM_SIZE (48U * 1024U)
#define LV_DEF_REFR_PERIOD  33
#define LV_USE_OS   LV_OS_NONE

#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_USE_DRAW_SW 1
#define LV_USE_DRAW_SW_COMPLEX 1
```

说明：

- `LV_COLOR_DEPTH = 16` 对应 ST7735 的 RGB565。
- 当前 LVGL 没有直接接入 FreeRTOS OSAL，使用 `LV_OS_NONE`，由应用任务周期调用 `lv_tick_inc()` 和 `lv_timer_handler()`。
- `LV_DRAW_SW_SUPPORT_RGB565_SWAPPED = 1` 用于匹配 ST7735 刷新时的字节顺序。
- `LV_MEM_SIZE = 48 KB` 是 LVGL 内部堆，后续 UI 变复杂时可继续调整。

控件裁剪：

```c
#define LV_USE_LABEL 1
#define LV_USE_ARC 1
#define LV_USE_FLEX 1
```

大部分未使用控件保持关闭，例如 button、chart、table、image、textarea 等，以减少编译体积。

## 5. LVGL 显示端口

端口文件：

```text
H7Lttit/lvgl_port/lv_port_lvgl.c
H7Lttit/lvgl_port/lv_port_lvgl.h
```

初始化函数：

```c
void LvPort_Init(void)
{
  lv_init();

  s_display = lv_display_create((int32_t)width, (int32_t)height);
  lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_flush_cb(s_display, LvPort_Flush);
  lv_display_set_buffers(s_display,
                         s_draw_buffer,
                         NULL,
                         sizeof(s_draw_buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
}
```

当前使用部分刷新模式：

```c
#define LV_PORT_DRAW_ROWS 20U
static uint8_t s_draw_buffer[160U * LV_PORT_DRAW_ROWS * 2U] __attribute__((aligned(4)));
```

说明：

- 160 是当前横向屏宽。
- 每像素 2 字节。
- 一次刷新 20 行，减少 RAM 占用。
- buffer 做 4 字节对齐，避免 Cortex-M7 访问和外设传输时出现非对齐风险。

## 6. ST7735 flush 回调

LVGL flush 回调：

```c
static void LvPort_Flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
  int32_t width = area->x2 - area->x1 + 1;
  int32_t height = area->y2 - area->y1 + 1;

  if ((width > 0) && (height > 0))
  {
    (void)ST7735_FillRGBRect(&st7735_pObj,
                             (uint32_t)area->x1,
                             (uint32_t)area->y1,
                             px_map,
                             (uint32_t)width,
                             (uint32_t)height);
  }

  lv_display_flush_ready(display);
}
```

刷新流程：

1. LVGL 计算脏区域 `area`。
2. LVGL 将该区域渲染到 `px_map`。
3. 端口层调用 `ST7735_FillRGBRect()` 写入 LCD。
4. 写完后调用 `lv_display_flush_ready()` 通知 LVGL 本次刷新结束。

当前没有使用 DMA，刷新是阻塞式 SPI 写屏。后续如果要提升刷新效率，可以在 ST7735 驱动中增加 SPI DMA，并在 DMA complete 中调用 `lv_display_flush_ready()`。

## 7. LVGL tick 和任务调度

当前没有使用 LVGL 自带 OS 适配层，而是在 `LvPort_Process()` 中处理 tick：

```c
void LvPort_Process(void)
{
  static TickType_t last_tick;
  TickType_t now = xTaskGetTickCount();

  if (now != last_tick)
  {
    lv_tick_inc((uint32_t)((now - last_tick) * portTICK_PERIOD_MS));
    last_tick = now;
  }

  (void)lv_timer_handler();
}
```

调用位置在 `StartBoardPeripheralsTask()`：

```c
while (1)
{
  BoardLcdShowResources();
  LvPort_Process();
  vTaskDelay(pdMS_TO_TICKS(1000U));
}
```

说明：

- 当前资源页面变化频率低，所以每秒更新一次即可。
- 如果后续增加动画、触摸、复杂 UI，需要把 `LvPort_Process()` 调用周期缩短到 5 ms 到 20 ms。
- LVGL API 当前都在 `board_io` 任务中调用，避免多任务同时操作 LVGL 对象。

## 8. 在 `main.c` 中接入 LVGL

新增头文件：

```c
#include "lv_port_lvgl.h"
```

LCD 初始化后初始化 LVGL：

```c
printf("board_io: lcd init\r\n");
LCD_InitSimple();
LvPort_Init();
```

原先直接调用 ST7735 文本绘制的 `BoardLcdShowLine()` 改为通过 LVGL 更新：

```c
static void BoardLcdShowLine(uint16_t y, const char *text)
{
  LvPort_SetLine((uint32_t)(y / 16U), text);
  LvPort_Process();
}
```

资源页面稳定后，主循环中每秒刷新资源环：

```c
BoardLcdShowResources();
LvPort_Process();
vTaskDelay(pdMS_TO_TICKS(1000U));
```

## 9. ROM、RAM、SD 卡资源统计

资源更新函数：

```c
void LvPort_SetResourceUsage(uint32_t rom_percent,
                             uint32_t ram_percent,
                             uint32_t sd_percent);
```

ROM 使用率：

```c
#ifdef W25Qxx
extern uint8_t Image$$ER_IROM2$$Length[];
#else
extern uint8_t Image$$ER_IROM1$$Length[];
#endif

#ifdef W25Qxx
const uint32_t flash_used = (uint32_t)Image$$ER_IROM2$$Length;
#else
const uint32_t flash_used = (uint32_t)Image$$ER_IROM1$$Length;
#endif

const uint32_t rom_percent = (flash_used * 100UL) / kCodeFlashSize;
```

说明：

- 当前工程支持 W25Q XIP，开启 `W25Qxx` 时，应用代码区域使用 `ER_IROM2`。
- 未开启 `W25Qxx` 时，使用内部 Flash 区域 `ER_IROM1`。

RAM 使用率：

```c
extern uint8_t Image$$RW_IRAM2$$Length[];

const uint32_t ram_static = (uint32_t)Image$$RW_IRAM2$$Length;
const uint32_t heap_free = (uint32_t)xPortGetFreeHeapSize();
const uint32_t heap_used = (uint32_t)configTOTAL_HEAP_SIZE - heap_free;
const uint32_t ram_percent = ((ram_static + heap_used) * 100UL) / kAxiSramSize;
```

说明：

- `Image$$RW_IRAM2$$Length` 统计静态 RAM 占用。
- FreeRTOS heap 用 `configTOTAL_HEAP_SIZE - xPortGetFreeHeapSize()` 估算动态使用量。
- 当前 RAM 总量按 AXI SRAM 512 KB 计算。

SD 卡使用率：

```c
lfs_ssize_t used_blocks = lfs_fs_size(LittleFs_SdGetHandle());
uint32_t total_blocks = LittleFs_SdGetBlockCount();

if ((used_blocks >= 0) && (total_blocks != 0U))
{
  sd_used_percent = ((uint32_t)used_blocks * 100UL) / total_blocks;
}
```

说明：

- SD 卡文件系统使用 littlefs。
- shell 初始化阶段已经挂载文件系统。
- 周期统计时直接复用当前 littlefs handle，不再每秒重复 mount/unmount。

## 10. 资源环 UI 实现

当前页面创建 3 个资源环：

```c
LvPort_CreateResource(screen, 0U, "ROM", lv_color_hex(0x46D9A5), 8, 21);
LvPort_CreateResource(screen, 1U, "RAM", lv_color_hex(0x4DA3FF), 58, 21);
LvPort_CreateResource(screen, 2U, "SD",  lv_color_hex(0xF2C94C), 108, 21);
```

每个资源控件由三部分组成：

- `lv_arc`：环形进度。
- `percent_label`：百分比文字。
- `name_label`：资源名称。

arc 样式：

```c
lv_arc_set_range(arc, 0, 100);
lv_arc_set_bg_angles(arc, 0, 360);
lv_arc_set_rotation(arc, 270);
lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A3440), LV_PART_MAIN);
lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
```

更新时做 0 到 100 限幅：

```c
static uint32_t LvPort_ClampPercent(uint32_t percent)
{
  return (percent > 100U) ? 100U : percent;
}
```

然后更新 arc 和文字：

```c
lv_arc_set_value(s_resources[i].arc, (int32_t)values[i]);
snprintf(text, sizeof(text), "%lu%%", (unsigned long)values[i]);
lv_label_set_text(s_resources[i].percent_label, text);
```

## 11. 编译和烧录

编译：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
```

成功输出：

```text
[build] ok
[build] axf: F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit\H7Lttit.axf
[build] hex: F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit\H7Lttit.hex
```

烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_keil.ps1
```

成功输出：

```text
[flash_keil] ok
```

说明：

- `build.ps1` 会先调用 Keil 构建，再使用指定 scatter 重新链接。
- `flash_keil.ps1` 会烧录 Keil 工程产物，并额外写入内部 boot 区。
- 当前 PowerShell 末尾可能出现 Conda 的 GBK 编码报错，这是本机 shell 启动环境钩子问题，不影响固件编译和烧录结果。

## 12. 串口验证

串口参数：

```text
COM3
115200
8N1
```

发送：

```text
help
```

期望返回：

```text
> help
commands: help mem w25q pwd ls cat touch write mkdir cd vfs vim reboot
>
```

串口 shell 正常说明：

- FreeRTOS 调度器已运行。
- `board_io` 初始化阶段没有卡死。
- SD littlefs 挂载和 shell 初始化已完成。

## 13. 显示验证

LCD 上应显示：

```text
H7Lttit Resources
ROM   RAM   SD
xx%   xx%   xx%
```

实际为 3 个环形图标：

- ROM：绿色环。
- RAM：蓝色环。
- SD：黄色环。

如果屏幕无显示，优先检查：

1. `LCD_InitSimple()` 是否执行。
2. `LvPort_Init()` 是否在 LCD 初始化之后调用。
3. `LV_COLOR_FORMAT_RGB565_SWAPPED` 是否与 ST7735 显示颜色一致。
4. `LvPort_Flush()` 是否调用 `lv_display_flush_ready()`。
5. `LvPort_Process()` 是否周期执行。

如果屏幕颜色异常，优先检查：

1. `LV_COLOR_FORMAT_RGB565_SWAPPED`。
2. ST7735 驱动中的 RGB/BGR 配置。
3. `ST7735_FillRGBRect()` 内部是否又做了一次字节交换。

如果显示内容卡住，优先检查：

1. FreeRTOS tick 是否正常。
2. `LvPort_Process()` 是否持续调用。
3. 是否有其他任务并发调用 LVGL API。

## 14. 当前限制和后续优化

当前限制：

- LVGL 刷新为阻塞式 SPI 写屏，没有使用 DMA。
- 没有触摸输入设备。
- `LvPort_Process()` 当前 1 秒调用一次，适合资源页，不适合动画页面。
- LVGL 内存池固定为 48 KB，复杂 UI 可能需要调大。

后续可优化：

- 将 `LvPort_Process()` 移到独立 LVGL task，周期 5 ms 到 20 ms。
- ST7735 刷新改为 SPI DMA。
- 增加 LVGL mutex，统一保护跨任务 LVGL API 调用。
- 增加触摸输入驱动并注册 `lv_indev_t`。
- 根据最终 UI 裁剪 `lv_conf.h` 和 Keil 工程源文件，减少 ROM 占用。

