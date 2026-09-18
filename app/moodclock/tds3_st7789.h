// ============================================================================
//  tds3_st7789.h —— LILYGO T-Display-S3 ST7789V 170x320 驱动（NuttX/openVela 版）
//
//  从 Arduino 版 TDisplayS3_ST7789.cpp v24 逐拍移植。
//  保留 v24 定案的全部关键结论（都是真机上换来的）：
//    1. 8 位 8080 并口，纯 GPIO 位蹦（digitalWrite 等价的 HAL 写），
//       WR 低 5µs / 高 5µs，禁止寄存器快路径；
//    2. ★ 每发一个字节翻转一次 CS（CS 常低 = 冷插雪花的元凶，v24 定罪）；
//    3. 冷启动：先 delay(300) 等电源稳 → 真断电 1s（全部信号脚转高阻，
//       杜绝经 IO 保护二极管倒灌的“假断电”）→ 供电先行 → 最小初始化序列；
//    4. 偏移：竖屏列偏移 35 / 横屏行偏移 35（缺了会留 35 像素宽固定雪花条）；
//    5. 本面板实测偏压：VRHS(0xC3)=0x02、VDVS(0xC4)=0x20，IPS 反相 0x21。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TDS3_NATIVE_W 170
#define TDS3_NATIVE_H 320

#define TDS3_BLACK  0x0000
#define TDS3_WHITE  0xFFFF
#define TDS3_RED    0xF800
#define TDS3_GREEN  0x07E0
#define TDS3_BLUE   0x001F

bool    tds3_begin(void);
void    tds3_reinit(void);
void    tds3_setPower(bool on);
void    tds3_setBacklight(uint8_t level);

void    tds3_fillRect(int x, int y, int w, int h, uint16_t color);
void    tds3_fillScreen(uint16_t color);
void    tds3_pushImage(int x, int y, int w, int h, const uint16_t* data);

int     tds3_width(void);
int     tds3_height(void);
bool    tds3_ready(void);
uint8_t tds3_bootAttempts(void);
