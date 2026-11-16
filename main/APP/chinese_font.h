#ifndef CHINESE_FONT_H
#define CHINESE_FONT_H

#include "lvgl.h"

/* 16×16 汉字点阵字体（仅含项目界面用到的 80 个汉字，英文/数字回退到 Montserrat） */
extern const lv_font_t lv_font_chinese_16;

/* 16×16 符号点阵字体（仅 · 和 °，作为 Montserrat 字体的 fallback） */
extern const lv_font_t lv_font_symbols_16;

#endif /* CHINESE_FONT_H */
