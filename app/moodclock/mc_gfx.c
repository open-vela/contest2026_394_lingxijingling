// ============================================================================
//  mc_gfx.c —— 离屏画布实现（NuttX/openVela 版）
//  逐行移植自 Arduino 版 gfx.cpp。差异只有两处：
//    1. heap_caps_malloc -> malloc（NuttX 统一堆，ESP32-S3 上
//       kmm heap 就在内部 SRAM，天然满足“帧缓冲不进 PSRAM”的 v23 结论）；
//    2. Serial.printf -> printf。
// ============================================================================

#include <nuttx/config.h>

#define GLYPHS_IMPL            /* 字库数组实体只在本文件定义一份 */
#include "mc_gfx.h"
#include "mc_glyphs.h"
#include "tds3_st7789.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

uint16_t* gfx_fb = NULL;

bool gfx_begin(void)
{
  if (gfx_fb) return true;
  const size_t bytes = (size_t)GFX_W * GFX_H * 2;   /* 108800 B */
  gfx_fb = (uint16_t*)malloc(bytes);
  if (!gfx_fb) return false;
  memset(gfx_fb, 0, bytes);
  printf("[gfx] fb %u bytes @%p (heap)\n", (unsigned)bytes, (void*)gfx_fb);
  return true;
}

/* ---------------- 基础 ---------------- */
void gfx_blend(int x, int y, uint16_t color, uint8_t a)
{
  if (a == 0) return;
  if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) return;
  uint16_t* p = gfx_fb + (size_t)y * GFX_W + x;
  if (a == 255) { *p = color; return; }
  *p = gfx_mix(*p, color, a);
}

void gfx_clear(uint16_t color)
{
  if (!gfx_fb) return;
  const size_t n = (size_t)GFX_W * GFX_H;
  for (size_t i = 0; i < n; i++) gfx_fb[i] = color;
}

void gfx_fillRect(int x, int y, int w, int h, uint16_t color)
{
  if (!gfx_fb) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > GFX_W) w = GFX_W - x;
  if (y + h > GFX_H) h = GFX_H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* p = gfx_fb + (size_t)(y + j) * GFX_W + x;
    for (int i = 0; i < w; i++) p[i] = color;
  }
}

void gfx_vGrad(int x, int y, int w, int h, uint16_t top, uint16_t bot)
{
  if (!gfx_fb || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > GFX_W) w = GFX_W - x;
  if (w <= 0) return;
  for (int j = 0; j < h; j++) {
    const int py = y + j;
    if (py < 0 || py >= GFX_H) continue;
    const uint16_t c = (h == 1) ? top : gfx_mix(top, bot, (uint8_t)((j * 255) / (h - 1)));
    uint16_t* p = gfx_fb + (size_t)py * GFX_W + x;
    for (int i = 0; i < w; i++) p[i] = c;
  }
}

void gfx_softHLine(int x, int y, int w, uint16_t color, uint8_t aMid)
{
  if (w <= 0) return;
  const int half = w / 2;
  for (int i = 0; i < w; i++) {
    const int d = (i < half) ? i : (w - 1 - i);
    int a = (half <= 0) ? aMid : (aMid * d) / half;
    if (a > aMid) a = aMid;
    gfx_blend(x + i, y, color, (uint8_t)a);
  }
}

/* ---------------- SDF 抗锯齿图形 ---------------- */
static inline uint8_t cov(float d)
{
  float a = 0.5f - d;
  if (a <= 0.0f) return 0;
  if (a >= 1.0f) return 255;
  return (uint8_t)(a * 255.0f + 0.5f);
}

static inline int iclamp(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

void gfx_disc(float cx, float cy, float r, uint16_t color)
{
  if (!gfx_fb) return;
  const int x0 = iclamp((int)floorf(cx - r - 1.0f), 0, GFX_W - 1);
  const int x1 = iclamp((int)ceilf (cx + r + 1.0f), 0, GFX_W - 1);
  const int y0 = iclamp((int)floorf(cy - r - 1.0f), 0, GFX_H - 1);
  const int y1 = iclamp((int)ceilf (cy + r + 1.0f), 0, GFX_H - 1);
  for (int y = y0; y <= y1; y++) {
    const float dy = (float)y + 0.5f - cy;
    for (int x = x0; x <= x1; x++) {
      const float dx = (float)x + 0.5f - cx;
      const uint8_t a = cov(sqrtf(dx * dx + dy * dy) - r);
      if (a) gfx_blend(x, y, color, a);
    }
  }
}

void gfx_ring(float cx, float cy, float r, float thick, uint16_t color)
{
  if (!gfx_fb) return;
  const float h = thick * 0.5f;
  const int x0 = iclamp((int)floorf(cx - r - h - 1.0f), 0, GFX_W - 1);
  const int x1 = iclamp((int)ceilf (cx + r + h + 1.0f), 0, GFX_W - 1);
  const int y0 = iclamp((int)floorf(cy - r - h - 1.0f), 0, GFX_H - 1);
  const int y1 = iclamp((int)ceilf (cy + r + h + 1.0f), 0, GFX_H - 1);
  for (int y = y0; y <= y1; y++) {
    const float dy = (float)y + 0.5f - cy;
    for (int x = x0; x <= x1; x++) {
      const float dx = (float)x + 0.5f - cx;
      const uint8_t a = cov(fabsf(sqrtf(dx * dx + dy * dy) - r) - h);
      if (a) gfx_blend(x, y, color, a);
    }
  }
}

void gfx_capsule(float x0f, float y0f, float x1f, float y1f, float thick, uint16_t color)
{
  if (!gfx_fb) return;
  const float h = thick * 0.5f;
  const int bx0 = iclamp((int)floorf(fminf(x0f, x1f) - h - 1.0f), 0, GFX_W - 1);
  const int bx1 = iclamp((int)ceilf (fmaxf(x0f, x1f) + h + 1.0f), 0, GFX_W - 1);
  const int by0 = iclamp((int)floorf(fminf(y0f, y1f) - h - 1.0f), 0, GFX_H - 1);
  const int by1 = iclamp((int)ceilf (fmaxf(y0f, y1f) + h + 1.0f), 0, GFX_H - 1);

  const float vx = x1f - x0f, vy = y1f - y0f;
  const float len2 = vx * vx + vy * vy;

  for (int y = by0; y <= by1; y++) {
    const float py = (float)y + 0.5f;
    for (int x = bx0; x <= bx1; x++) {
      const float px = (float)x + 0.5f;
      float t = 0.0f;
      if (len2 > 1e-6f) {
        t = ((px - x0f) * vx + (py - y0f) * vy) / len2;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      }
      const float dx = px - (x0f + t * vx);
      const float dy = py - (y0f + t * vy);
      const uint8_t a = cov(sqrtf(dx * dx + dy * dy) - h);
      if (a) gfx_blend(x, y, color, a);
    }
  }
}

static inline bool angInRange(float a, float a0, float a1)
{
  const float TAU = 6.2831853f;
  float x = a - a0;
  float y = a1 - a0;
  while (x < 0)    x += TAU;
  while (x >= TAU) x -= TAU;
  while (y < 0)    y += TAU;
  return x <= y;
}

void gfx_arc(float cx, float cy, float r, float a0, float a1, float thick, uint16_t color)
{
  if (!gfx_fb) return;
  const float h = thick * 0.5f;
  const int x0 = iclamp((int)floorf(cx - r - h - 1.0f), 0, GFX_W - 1);
  const int x1 = iclamp((int)ceilf (cx + r + h + 1.0f), 0, GFX_W - 1);
  const int y0 = iclamp((int)floorf(cy - r - h - 1.0f), 0, GFX_H - 1);
  const int y1 = iclamp((int)ceilf (cy + r + h + 1.0f), 0, GFX_H - 1);

  const float e0x = cx + r * cosf(a0), e0y = cy + r * sinf(a0);
  const float e1x = cx + r * cosf(a1), e1y = cy + r * sinf(a1);

  for (int y = y0; y <= y1; y++) {
    const float dy = (float)y + 0.5f - cy;
    for (int x = x0; x <= x1; x++) {
      const float dx = (float)x + 0.5f - cx;
      const float rr = sqrtf(dx * dx + dy * dy);
      float d;
      if (angInRange(atan2f(dy, dx), a0, a1)) {
        d = fabsf(rr - r) - h;
      } else {
        const float d0 = sqrtf((dx - (e0x - cx)) * (dx - (e0x - cx)) +
                               (dy - (e0y - cy)) * (dy - (e0y - cy))) - h;
        const float d1 = sqrtf((dx - (e1x - cx)) * (dx - (e1x - cx)) +
                               (dy - (e1y - cy)) * (dy - (e1y - cy))) - h;
        d = fminf(d0, d1);
      }
      const uint8_t a = cov(d);
      if (a) gfx_blend(x, y, color, a);
    }
  }
}

void gfx_bez2(float x0, float y0, float qx, float qy, float x1, float y1,
              float thick, uint16_t color)
{
  if (!gfx_fb) return;
  const int N = 20;
  float px[21], py[21];
  for (int i = 0; i <= N; i++) {
    const float t = (float)i / (float)N, mt = 1.0f - t;
    px[i] = mt * mt * x0 + 2.0f * mt * t * qx + t * t * x1;
    py[i] = mt * mt * y0 + 2.0f * mt * t * qy + t * t * y1;
  }
  for (int i = 0; i < N; i++) gfx_capsule(px[i], py[i], px[i + 1], py[i + 1], thick, color);
}

/* ---------------- 字库 ---------------- */
static const uint8_t* findGlyph(uint32_t code, bool big, int* pw)
{
  const uint32_t* codes = big ? gf_big_code : gf_sm_code;
  const uint16_t* ws    = big ? gf_big_w    : gf_sm_w;
  const uint32_t* offs  = big ? gf_big_off  : gf_sm_off;
  const int       n     = big ? GF_BIG_CNT  : GF_SM_CNT;
  const uint8_t*  bmp   = big ? gf_big_bmp  : gf_sm_bmp;
  for (int i = 0; i < n; i++) {
    if (codes[i] == code) { *pw = ws[i]; return bmp + offs[i]; }
  }
  *pw = 0;
  return NULL;
}

static uint32_t utf8Next(const char** p)
{
  const uint8_t* s = (const uint8_t*)*p;
  if (s[0] < 0x80) { *p += 1; return s[0]; }
  if ((s[0] & 0xE0) == 0xC0 && s[1]) { *p += 2; return ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); }
  if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
    *p += 3;
    return ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  }
  if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
    *p += 4;
    return ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
           ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
  }
  *p += 1;
  return '?';
}

int gfx_textW(const char* s, bool big)
{
  if (!s) return 0;
  const char* p = s;
  int w = 0, gw = 0;
  while (*p) {
    const uint32_t code = utf8Next(&p);
    if (findGlyph(code, big, &gw)) w += gw;
  }
  return w;
}

int gfx_text(int x, int y, const char* s, uint16_t color, bool big)
{
  if (!gfx_fb || !s) return 0;
  const int gh = big ? GF_BIG_H : GF_SM_H;
  const char* p = s;
  int cx = x;
  while (*p) {
    const uint32_t code = utf8Next(&p);
    int gw = 0;
    const uint8_t* g = findGlyph(code, big, &gw);
    if (!g) continue;
    for (int gy = 0; gy < gh; gy++) {
      const int py = y + gy;
      if (py < 0 || py >= GFX_H) continue;
      const uint8_t* row = g + gy * gw;
      for (int gx = 0; gx < gw; gx++) {
        const uint8_t a = row[gx];
        if (a) gfx_blend(cx + gx, py, color, a);
      }
    }
    cx += gw;
  }
  return cx - x;
}

int gfx_textC(int cx, int y, const char* s, uint16_t color, bool big)
{
  const int w = gfx_textW(s, big);
  return gfx_text(cx - w / 2, y, s, color, big);
}

/* ---------------- 推送 ---------------- */
void gfx_flush(int x, int y, int w, int h)
{
  if (!gfx_fb) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > GFX_W) w = GFX_W - x;
  if (y + h > GFX_H) h = GFX_H - y;
  if (w <= 0 || h <= 0) return;
  tds3_pushImage(x, y, w, h, gfx_fb + (size_t)y * GFX_W + x);
}

void gfx_flushAll(void)
{
  gfx_flush(0, 0, GFX_W, GFX_H);
}
