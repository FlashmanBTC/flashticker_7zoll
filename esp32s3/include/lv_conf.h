/**
 * lv_conf.h – LVGL 8.3.x Konfiguration für CrowPanel 7.0 (800x480)
 */
#if 1 /* Enable config */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

/*====================
   MEMORY SETTINGS
 *====================*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256 * 1024U)   /* 256KB für LVGL heap */
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC malloc
#define LV_MEM_POOL_FREE free

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 16    /* ~60fps */
#define LV_INDEV_DEF_READ_PERIOD 30

/*====================
   DISPLAY BUFFER
 *====================*/
/* Zwei Zeilen-Buffer reichen für Flush-Modus */
/* Größere Buffer in main.cpp definiert */

/*====================
   FEATURE SETTINGS
 *====================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_LOG 0

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGET SETTINGS
 *====================*/
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0

/*====================
   THEME
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 0

/*====================
   MISC
 *====================*/
#define LV_USE_SPRINTF_CUSTOM 0
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0

/* Animationen aus – nicht nötig für Ticker */
#define LV_USE_ANIMATION 0

#endif /* LV_CONF_H */
#endif /* End enable config */
