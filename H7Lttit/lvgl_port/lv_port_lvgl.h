#ifndef LV_PORT_LVGL_H
#define LV_PORT_LVGL_H

#include "main.h"

void LvPort_Init(void);
void LvPort_SetTitle(const char *text);
void LvPort_SetLine(uint32_t line, const char *text);
void LvPort_TerminalPushLine(const char *text);
void LvPort_SetResourceUsage(uint32_t rom_percent, uint32_t ram_percent, uint32_t sd_percent);
void LvPort_Process(void);

#endif
