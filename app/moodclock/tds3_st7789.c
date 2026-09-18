// ============================================================================
//  tds3_st7789.c —— ST7789V 8080 并口驱动实现（NuttX/openVela 版）
//
//  逐拍移植自 Arduino 版 v24（真机验证：热启动稳定、写路径为
//  “每字节翻转 CS + 全 GPIO 慢速”的 lcd_min 同款协议）。
// ============================================================================

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mc_compat.h"
#include "mc_gpio.h"
#include "tds3_st7789.h"

/* ---------------- 引脚映射（T-Display-S3 官方 8080 并口） ---------------- */
static const uint8_t kDataPins[8] = { 39, 40, 41, 42, 45, 46, 47, 48 };  /* D0..D7 */
#define TDS3_PIN_LCD_WR   8
#define TDS3_PIN_LCD_RD   9
#define TDS3_PIN_LCD_DC   7
#define TDS3_PIN_LCD_CS   6
#define TDS3_PIN_LCD_RST  5
#define TDS3_PIN_LCD_BL   38   /* 背光 */
#define TDS3_PIN_POWER_ON 15   /* 外设供电使能（必须拉高） */

/* ---------------- 面板状态 ---------------- */
static bool    s_ok = false;
static uint8_t s_bootAttempts = 0;
static int     s_w = TDS3_NATIVE_H;   /* 横屏 320x170（旋转固定为 1） */
static int     s_h = TDS3_NATIVE_W;
static int     s_colOff = 0;
static int     s_rowOff = 35;
static uint8_t s_bl = 255;

/* ---------------------------------------------------------------------------
 *  字节级时序（v24 定案：全慢速 + 每字节翻转 CS）
 * ------------------------------------------------------------------------- */
static void lmWr(uint8_t b)
{
  for (int i = 0; i < 8; i++)
    mc_gpio_write(kDataPins[i], (b >> i) & 1);
  mc_delay_us(1);                     /* 数据建立 */
  mc_gpio_write(TDS3_PIN_LCD_WR, false);
  mc_delay_us(5);                     /* WR 低电平宽度 */
  mc_gpio_write(TDS3_PIN_LCD_WR, true);
  mc_delay_us(5);                     /* WR 高电平建立 */
}

static void lmCmd(uint8_t c)
{
  mc_gpio_write(TDS3_PIN_LCD_DC, false);
  mc_gpio_write(TDS3_PIN_LCD_CS, false);
  lmWr(c);
  mc_gpio_write(TDS3_PIN_LCD_CS, true);
  mc_gpio_write(TDS3_PIN_LCD_DC, true);
}

static void lmData(uint8_t d)
{
  mc_gpio_write(TDS3_PIN_LCD_DC, true);
  mc_gpio_write(TDS3_PIN_LCD_CS, false);
  lmWr(d);
  mc_gpio_write(TDS3_PIN_LCD_CS, true);
}

static void data16(uint16_t v)
{
  lmData((uint8_t)(v >> 8));
  lmData((uint8_t)(v & 0xFF));
}

/* ---------------------------------------------------------------------------
 *  最小初始化序列（lcd_min 逐拍同款，冷插干净的唯一实现）
 * ------------------------------------------------------------------------- */
static void lcdMinPanelInit(void)
{
  /* 硬复位：完整脉冲 H20 -> L20 -> H120 */
  mc_gpio_write(TDS3_PIN_LCD_RST, true);  mc_delay_ms(20);
  mc_gpio_write(TDS3_PIN_LCD_RST, false); mc_delay_ms(20);
  mc_gpio_write(TDS3_PIN_LCD_RST, true);  mc_delay_ms(120);

  lmCmd(0x01); mc_delay_ms(150);   /* SWRESET */
  lmCmd(0x11); mc_delay_ms(120);   /* SLPOUT */
  lmCmd(0x3A); lmData(0x55);       /* COLMOD RGB565 */
  lmCmd(0x36); lmData(0x00);       /* MADCTL 先写 0x00（lcd_min 同款） */
  lmCmd(0xC2); lmData(0x01);       /* VDVVRHEN */
  lmCmd(0xC3); lmData(0x02);       /* VRHS（本面板实测 0x02，0x12 过驱动全白） */
  lmCmd(0xC4); lmData(0x20);       /* VDVS */
  lmCmd(0x21);                     /* INVON（IPS 反显） */
  lmCmd(0x13); mc_delay_ms(10);    /* NORON */
  lmCmd(0x29); mc_delay_ms(10);    /* DISPON */
  /* 应用需要横屏：初始化完再切 MADCTL 0x60（热启动反复验证过，安全） */
  lmCmd(0x36); lmData(0x60);
}

/* ---------------------------------------------------------------------------
 *  真断电重启：供电拉低 + 全部信号脚转高阻（杜绝倒灌假断电）+ 放电
 * ------------------------------------------------------------------------- */
static void panelHardRestart(int offMs)
{
  mc_gpio_write(TDS3_PIN_POWER_ON, false);   /* 断电 */
  mc_gpio_input(TDS3_PIN_LCD_RST);           /* 全部信号脚高阻 */
  mc_gpio_input(TDS3_PIN_LCD_CS);
  mc_gpio_input(TDS3_PIN_LCD_DC);
  mc_gpio_input(TDS3_PIN_LCD_WR);
  mc_gpio_input(TDS3_PIN_LCD_RD);
  mc_gpio_input(TDS3_PIN_LCD_BL);
  for (int i = 0; i < 8; i++) mc_gpio_input(kDataPins[i]);
  mc_delay_ms(offMs);                        /* 面板彻底失电、储能放光 */

  /* 供电先行 → 配脚（RST 一出生为低 = 压在复位态，lcd_min 同款） */
  mc_gpio_output(TDS3_PIN_POWER_ON);
  mc_gpio_write(TDS3_PIN_POWER_ON, true);
  mc_delay_ms(50);

  mc_gpio_output(TDS3_PIN_LCD_RST);          /* 输出默认低 → RST 压复位态 */
  mc_gpio_output_init_high(TDS3_PIN_LCD_BL); /* 背光直亮 */
  mc_gpio_output(TDS3_PIN_LCD_WR);
  mc_gpio_output_init_high(TDS3_PIN_LCD_WR);
  mc_gpio_output(TDS3_PIN_LCD_RD);
  mc_gpio_output_init_high(TDS3_PIN_LCD_RD);
  mc_gpio_output(TDS3_PIN_LCD_DC);
  mc_gpio_output_init_high(TDS3_PIN_LCD_DC);
  mc_gpio_output(TDS3_PIN_LCD_CS);
  mc_gpio_output_init_high(TDS3_PIN_LCD_CS);
  for (int i = 0; i < 8; i++) mc_gpio_output(kDataPins[i]);

  lcdMinPanelInit();
  /* v24：cmd/data 每字节翻转 CS，空闲保持高 */
  mc_gpio_write(TDS3_PIN_LCD_CS, true);
}

/* ---------------------------------------------------------------------------
 *  窗口 / 绘制
 * ------------------------------------------------------------------------- */
static void setWindow(int x0, int y0, int x1, int y1)
{
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  lmCmd(0x2A);                                /* CASET */
  data16((uint16_t)(x0 + s_colOff));
  data16((uint16_t)(x1 + s_colOff));
  lmCmd(0x2B);                                /* RASET */
  data16((uint16_t)(y0 + s_rowOff));
  data16((uint16_t)(y1 + s_rowOff));
  lmCmd(0x2C);                                /* RAMWR */
}

void tds3_fillRect(int x, int y, int w, int h, uint16_t color)
{
  if (!s_ok || w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= s_w || y >= s_h) return;
  if (x + w > s_w) w = s_w - x;
  if (y + h > s_h) h = s_h - y;
  if (w <= 0 || h <= 0) return;

  setWindow(x, y, x + w - 1, y + h - 1);
  const uint32_t n = (uint32_t)w * (uint32_t)h;
  /* ★ 像素流走 lmData（每字节翻转 CS），与 lcd_min 逐拍一致 */
  for (uint32_t i = 0; i < n; i++)
    data16(color);
}

void tds3_fillScreen(uint16_t color)
{
  tds3_fillRect(0, 0, s_w, s_h, color);
}

void tds3_pushImage(int x, int y, int w, int h, const uint16_t* data)
{
  if (!s_ok || !data || w <= 0 || h <= 0) return;
  if (x < 0 || y < 0 || x + w > s_w || y + h > s_h) return;

  setWindow(x, y, x + w - 1, y + h - 1);
  const uint32_t n = (uint32_t)w * (uint32_t)h;
  for (uint32_t i = 0; i < n; i++)
    data16(data[i]);
}

/* ---------------------------------------------------------------------------
 *  生命周期
 * ------------------------------------------------------------------------- */
bool tds3_begin(void)
{
  if (s_ok) return true;

  /* 冷插电源稳定等待（lcd_min 同款关键差异） */
  mc_delay_ms(300);

  /* 供电使能脚先配置好 */
  mc_gpio_output(TDS3_PIN_POWER_ON);

  s_ok = true;                 /* fillRect 里有 !s_ok 短路，先置位再清屏 */
  panelHardRestart(1000);      /* 真断电一轮保底 + 最小初始化 */
  s_bootAttempts = 1;
  tds3_fillScreen(TDS3_BLACK);
  return true;
}

void tds3_reinit(void)
{
  if (!s_ok) return;
  panelHardRestart(1000);
  tds3_fillScreen(TDS3_BLACK);
}

void tds3_setPower(bool on)
{
  mc_gpio_output(TDS3_PIN_POWER_ON);
  mc_gpio_write(TDS3_PIN_POWER_ON, on);
}

void tds3_setBacklight(uint8_t level)
{
  s_bl = level;
  mc_gpio_output(TDS3_PIN_LCD_BL);
  mc_gpio_write(TDS3_PIN_LCD_BL, level != 0);
  /* 注：Arduino 版 1~254 走 LEDC PWM 调光；NuttX ESP32-S3 的 LEDC 属于
   * 板级 lower-half 能力，如需调光请接 openVela 的 LEDC/背光驱动。
   * 本应用只用 0/255 两档，数字输出足够。 */
}

int     tds3_width(void)  { return s_w; }
int     tds3_height(void) { return s_h; }
bool    tds3_ready(void)  { return s_ok; }
uint8_t tds3_bootAttempts(void) { return s_bootAttempts; }
