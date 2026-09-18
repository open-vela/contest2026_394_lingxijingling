// ============================================================================
//  mc_gfx.h —— 离屏画布 + 抗锯齿绘图 + 灰度字库渲染（NuttX/openVela 版）
//  逐行移植自 Arduino 版 gfx.h/gfx.cpp，API 一一对应。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define GFX_W 320
#define GFX_H 170

extern uint16_t* gfx_fb;

bool gfx_begin(void);

/* 颜色 */
static inline uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3));
}

/* t=0 取 a，t=255 取 b */
static inline uint16_t gfx_mix(uint16_t a, uint16_t b, uint8_t t)
{
  const int u = 255 - t;
  const int r = (((a >> 11) & 0x1F) * u + ((b >> 11) & 0x1F) * t) / 255;
  const int g = (((a >>  5) & 0x3F) * u + ((b >>  5) & 0x3F) * t) / 255;
  const int c = ((a & 0x1F) * u + (b & 0x1F) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | c);
}

/* 基础绘制（都在帧缓冲上，不碰面板） */
void gfx_clear(uint16_t color);
void gfx_fillRect(int x, int y, int w, int h, uint16_t color);
void gfx_blend(int x, int y, uint16_t color, uint8_t a);
void gfx_vGrad(int x, int y, int w, int h, uint16_t top, uint16_t bot);
void gfx_softHLine(int x, int y, int w, uint16_t color, uint8_t aMid);

/* 抗锯齿图形（SDF） */
void gfx_disc(float cx, float cy, float r, uint16_t color);
void gfx_ring(float cx, float cy, float r, float thick, uint16_t color);
void gfx_capsule(float x0, float y0, float x1, float y1, float thick, uint16_t color);
void gfx_arc(float cx, float cy, float r, float a0, float a1, float thick, uint16_t color);
void gfx_bez2(float x0, float y0, float qx, float qy, float x1, float y1,
              float thick, uint16_t color);

/* 文字（UTF-8 + 灰度抗锯齿；字库见 mc_glyphs.h） */
int gfx_textW(const char* s, bool big);
int gfx_text(int x, int y, const char* s, uint16_t color, bool big);
int gfx_textC(int cx, int y, const char* s, uint16_t color, bool big);

/* 推送到面板 */
void gfx_flush(int x, int y, int w, int h);
void gfx_flushAll(void);
