#include "lv_port_lvgl.h"

#include "FreeRTOS.h"
#include "lcd.h"
#include "lvgl.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define LV_PORT_DRAW_ROWS 20U
#define LV_PORT_MAX_LINES 5U
#define LV_PORT_TERMINAL_LINES 5U
#define LV_PORT_TERMINAL_LINE_CHARS 28U
#define LV_PORT_RESOURCE_COUNT 3U

static void LvPort_Flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map);
static void LvPort_CreateResource(lv_obj_t *screen,
                                  uint32_t index,
                                  const char *name,
                                  lv_color_t color,
                                  int32_t x,
                                  int32_t y);
static uint32_t LvPort_ClampPercent(uint32_t percent);
static void LvPort_SetResourceVisible(uint8_t visible);
static void LvPort_SetTerminalVisible(uint8_t visible);

typedef struct
{
  lv_obj_t *arc;
  lv_obj_t *percent_label;
  lv_obj_t *name_label;
} LvPort_ResourceView;

static lv_display_t *s_display;
static lv_obj_t *s_title_label;
static lv_obj_t *s_line_labels[LV_PORT_MAX_LINES];
static lv_obj_t *s_terminal_labels[LV_PORT_TERMINAL_LINES];
static char s_terminal_lines[LV_PORT_TERMINAL_LINES][LV_PORT_TERMINAL_LINE_CHARS + 1U];
static LvPort_ResourceView s_resources[LV_PORT_RESOURCE_COUNT];
static uint8_t s_draw_buffer[160U * LV_PORT_DRAW_ROWS * 2U] __attribute__((aligned(4)));
static uint8_t s_initialized;
static uint8_t s_terminal_visible;

void LvPort_Init(void)
{
  uint32_t width = ST7735Ctx.Width;
  uint32_t height = ST7735Ctx.Height;

  if (s_initialized != 0U)
  {
    return;
  }

  lv_init();

  if ((width == 0U) || (height == 0U))
  {
    width = 160U;
    height = 80U;
  }

  s_display = lv_display_create((int32_t)width, (int32_t)height);
  lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_flush_cb(s_display, LvPort_Flush);
  lv_display_set_buffers(s_display,
                         s_draw_buffer,
                         NULL,
                         sizeof(s_draw_buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  s_title_label = lv_label_create(screen);
  lv_label_set_text(s_title_label, "COM8 -> Screen");
  lv_obj_set_style_text_color(s_title_label, lv_color_hex(0x7FFFD4), 0);
  lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_14, 0);
  lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 1);

  LvPort_CreateResource(screen, 0U, "ROM", lv_color_hex(0x46D9A5), 8, 21);
  LvPort_CreateResource(screen, 1U, "RAM", lv_color_hex(0x4DA3FF), 58, 21);
  LvPort_CreateResource(screen, 2U, "SD", lv_color_hex(0xF2C94C), 108, 21);

  for (uint32_t i = 0U; i < LV_PORT_MAX_LINES; i++)
  {
    s_line_labels[i] = lv_label_create(screen);
    lv_obj_set_width(s_line_labels[i], (int32_t)(width - 8U));
    lv_obj_set_style_text_color(s_line_labels[i], lv_color_hex(0xE8EEF2), 0);
    lv_label_set_long_mode(s_line_labels[i], LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(s_line_labels[i], "");
    lv_obj_add_flag(s_line_labels[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_line_labels[i], LV_ALIGN_BOTTOM_MID, 0, 0);
  }

  for (uint32_t i = 0U; i < LV_PORT_TERMINAL_LINES; i++)
  {
    s_terminal_labels[i] = lv_label_create(screen);
    lv_obj_set_width(s_terminal_labels[i], (int32_t)(width - 8U));
    lv_obj_set_style_text_color(s_terminal_labels[i], lv_color_hex(0xE8EEF2), 0);
    lv_obj_set_style_text_font(s_terminal_labels[i], &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_terminal_labels[i], LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(s_terminal_labels[i], "");
    lv_obj_add_flag(s_terminal_labels[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_terminal_labels[i], 4, (int32_t)(17U + (i * 12U)));
  }

  LvPort_SetResourceUsage(0U, 0U, 0U);

  s_initialized = 1U;
}

void LvPort_SetLine(uint32_t line, const char *text)
{
  if ((s_initialized == 0U) || (line >= LV_PORT_MAX_LINES) || (text == NULL))
  {
    return;
  }

  lv_label_set_text(s_line_labels[line], text);
}

void LvPort_SetTitle(const char *text)
{
  if ((s_initialized == 0U) || (text == NULL))
  {
    return;
  }

  lv_label_set_text(s_title_label, text);
}

void LvPort_TerminalPushLine(const char *text)
{
  if ((s_initialized == 0U) || (text == NULL))
  {
    return;
  }

  if (s_terminal_visible == 0U)
  {
    LvPort_SetResourceVisible(0U);
    LvPort_SetTerminalVisible(1U);
    s_terminal_visible = 1U;
    lv_label_set_text(s_title_label, "COM8 Terminal");
  }

  for (uint32_t i = 0U; i < (LV_PORT_TERMINAL_LINES - 1U); i++)
  {
    memcpy(s_terminal_lines[i], s_terminal_lines[i + 1U], sizeof(s_terminal_lines[i]));
  }

  strncpy(s_terminal_lines[LV_PORT_TERMINAL_LINES - 1U], text, LV_PORT_TERMINAL_LINE_CHARS);
  s_terminal_lines[LV_PORT_TERMINAL_LINES - 1U][LV_PORT_TERMINAL_LINE_CHARS] = '\0';

  for (uint32_t i = 0U; i < LV_PORT_TERMINAL_LINES; i++)
  {
    lv_label_set_text(s_terminal_labels[i], s_terminal_lines[i]);
  }
}

void LvPort_SetResourceUsage(uint32_t rom_percent, uint32_t ram_percent, uint32_t sd_percent)
{
  uint32_t values[LV_PORT_RESOURCE_COUNT];
  char text[8];

  if (s_initialized == 0U)
  {
    return;
  }

  values[0] = LvPort_ClampPercent(rom_percent);
  values[1] = LvPort_ClampPercent(ram_percent);
  values[2] = LvPort_ClampPercent(sd_percent);

  for (uint32_t i = 0U; i < LV_PORT_RESOURCE_COUNT; i++)
  {
    lv_arc_set_value(s_resources[i].arc, (int32_t)values[i]);
    (void)snprintf(text, sizeof(text), "%lu%%", (unsigned long)values[i]);
    lv_label_set_text(s_resources[i].percent_label, text);
  }
}

void LvPort_Process(void)
{
  static TickType_t last_tick;
  TickType_t now = xTaskGetTickCount();

  if (s_initialized == 0U)
  {
    return;
  }

  if (last_tick == 0U)
  {
    last_tick = now;
  }

  if (now != last_tick)
  {
    lv_tick_inc((uint32_t)((now - last_tick) * portTICK_PERIOD_MS));
    last_tick = now;
  }

  (void)lv_timer_handler();
}

static void LvPort_CreateResource(lv_obj_t *screen,
                                  uint32_t index,
                                  const char *name,
                                  lv_color_t color,
                                  int32_t x,
                                  int32_t y)
{
  lv_obj_t *arc;
  lv_obj_t *percent_label;
  lv_obj_t *name_label;

  if (index >= LV_PORT_RESOURCE_COUNT)
  {
    return;
  }

  arc = lv_arc_create(screen);
  lv_obj_set_size(arc, 42, 42);
  lv_obj_set_pos(arc, x, y);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_value(arc, 0);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x2A3440), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

  percent_label = lv_label_create(screen);
  lv_label_set_text(percent_label, "0%");
  lv_obj_set_width(percent_label, 34);
  lv_obj_set_style_text_align(percent_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(percent_label, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(percent_label, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(percent_label, x + 4, y + 13);

  name_label = lv_label_create(screen);
  lv_label_set_text(name_label, name);
  lv_obj_set_width(name_label, 42);
  lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(name_label, color, 0);
  lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(name_label, x, y + 45);

  s_resources[index].arc = arc;
  s_resources[index].percent_label = percent_label;
  s_resources[index].name_label = name_label;
}

static uint32_t LvPort_ClampPercent(uint32_t percent)
{
  return (percent > 100U) ? 100U : percent;
}

static void LvPort_SetResourceVisible(uint8_t visible)
{
  for (uint32_t i = 0U; i < LV_PORT_RESOURCE_COUNT; i++)
  {
    if (visible != 0U)
    {
      lv_obj_remove_flag(s_resources[i].arc, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_resources[i].percent_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_resources[i].name_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
      lv_obj_add_flag(s_resources[i].arc, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_resources[i].percent_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_resources[i].name_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void LvPort_SetTerminalVisible(uint8_t visible)
{
  for (uint32_t i = 0U; i < LV_PORT_TERMINAL_LINES; i++)
  {
    if (visible != 0U)
    {
      lv_obj_remove_flag(s_terminal_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
      lv_obj_add_flag(s_terminal_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

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
