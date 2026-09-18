// ============================================================================
//  moodclock_main.c —— 心情时钟主程序（NuttX/openVela 版）
//
//  由 Arduino 版 mood_clock.ino 移植。结构对应关系：
//    setup()  -> moodclock_main() 开头的初始化段
//    loop()   -> while(1) 主循环（末尾 nxsig_usleep(2000) 等价 delay(2)）
//    Serial   -> printf（输出）+ stdin poll（输入，nsh 控制台）
//
//  与 Arduino 版的已知差异（有意为之）：
//    1. 蜂鸣（LEDC PWM）暂缺，只有马达反馈；震动反馈仍默认关闭
//       （与 LCD 共 3V 轨的电流尖峰问题不变，接三极管后 VIBON 打开）；
//    2. WiFi/NTP/天气为离线骨架（见 mc_net.c TODO）：
//       联网失败 3 轮后自动进离线运行态，时钟页显示 --:--（不卡在
//       “连接网络”状态页）；
//    3. Arduino 专用的硬件基准台命令（SB/INV/BGR/SLOW/PF/VCOM/VRH/VDV）
//       不随移植，只保留日常命令。
// ============================================================================

#include <nuttx/config.h>

#include <sys/poll.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "mc_compat.h"
#include "mc_gpio.h"
#include "tds3_st7789.h"
#include "mc_gfx.h"
#include "mc_theme.h"
#include "mc_net.h"

/* ---------------------------------------------------------------------------
 *  配置（与 Arduino 版一致）
 * ------------------------------------------------------------------------- */
static const char* WIFI_SSID = "3124";
static const char* WIFI_PASS = "rczzzhl312";
static const char* LOC_CITY_CODE = "101250508";   /* 湖南汝城，中国天气网 */

#define WX_REFRESH_MS  (10UL * 60UL * 1000UL)
#define WIFI_RETRY_MS  (30UL * 1000UL)
#define WIFI_MAX_FAILS 3            /* 离线骨架：3 轮失败后放弃联网 */

/* 外设引脚（沿用实测接线） */
#define PIN_SHAKE    2     /* SEN0289 晃动传感器（空闲高，摇动出脉冲） */
#define PIN_MOTOR    1     /* 震动马达（高电平震动） */
#define PIN_BTN      0     /* BOOT 键（按下=低，翻页） */

#define SHAKE_DEBOUNCE_MS 100UL
#define ACK_VIB_ON_MS  80UL
#define ACK_VIB_OFF_MS 20UL
#define ACK_VIB_N_HOME 40        /* 主页摇晃确认 ≈4s */
#define ACK_VIB_N_MOOD  4        /* 心情页换心情 ≈0.45s */

/* 布局（横屏 320 x 170） */
#define TIME_X     14
#define TIME_Y     16
#define DATE_X     16
#define DATE_Y     88
#define RULE_Y     118
#define WX_X       16
#define WX_Y       126
#define WX_RIGHT   306
#define MOOD_CX    282.0f
#define MOOD_CY    50.0f
#define MOOD_R     32.0f

enum MoodKind {
  MOOD_SUN = 0, MOOD_MOON, MOOD_PARTLY, MOOD_CLOUD,
  MOOD_RAIN, MOOD_SNOW, MOOD_STORM, MOOD_FOG
};

enum Page { PAGE_HOME = 0, PAGE_FEEL };
enum Feel { FEEL_HAPPY = 0, FEEL_SAD, FEEL_ANGRY, FEEL_CALM };

static const char* const kFeelName[4] = {
  "\xE5\xBC\x80\xE5\xBF\x83",       /* 开心 */
  "\xE4\xBC\xA4\xE5\xBF\x83",       /* 伤心 */
  "\xE7\x94\x9F\xE6\xB0\x94",       /* 生气 */
  "\xE5\xB9\xB3\xE9\x9D\x99"        /* 平静 */
};
static const char* const kWk[7] = {
  "\xE6\x97\xA5", "\xE4\xB8\x80", "\xE4\xBA\x8C", "\xE4\xB8\x89",
  "\xE5\x9B\x9B", "\xE4\xBA\x94", "\xE5\x85\xAD"   /* 日一二三四五六 */
};

/* ---------------------------------------------------------------------------
 *  全局状态
 * ------------------------------------------------------------------------- */
enum Stage { ST_IDLE = 0, ST_WIFI, ST_TIME, ST_RUN };

static Stage     s_stage        = ST_IDLE;
static uint32_t  s_wifiDeferUntil = 0;
static uint32_t  s_wifiFailCnt  = 0;

static int       s_themeIdx   = -1;
static Weather   s_wx;
static uint32_t  s_lastWxMs   = 0;
static uint32_t  s_lastWifiTry = 0;
static int       s_lastMinKey = -1;
static int       s_statusTick = 0;
static uint8_t   s_forceMood  = 255;    /* 255 = 自动 */
static int       s_forceTheme = -1;
static bool      s_testHold   = false;

static Page      s_page       = PAGE_HOME;
static Feel      s_feel       = FEEL_HAPPY;

static int       s_hrBpm       = 72;
static uint32_t  s_lastShakeMs = 0;

static bool      s_shakePrev   = true;
static uint32_t  s_lastCntMs   = 0;
static bool      s_shakePending  = false;
static uint32_t  s_shakeSettleAt = 0;
static uint16_t  s_shakePulses   = 0;

static bool      s_ackEnable = false;  /* ★ 默认关：马达与 LCD 共 3V 轨 */
static bool      s_vibOn     = false;
static uint32_t  s_vibUntil  = 0;
static uint32_t  s_vibToggle = 0;

static bool      s_btnPrev   = true;
static uint32_t  s_btnMs     = 0;

/* ---------------------------------------------------------------------------
 *  前向声明
 * ------------------------------------------------------------------------- */
static void drawClockPage(const struct tm* t);
static void drawFeelPage(void);
static void drawStatusPage(const char* title, const char* sub, int phase);
static void applyShake(void);

/* ---------------------------------------------------------------------------
 *  时间
 * ------------------------------------------------------------------------- */
static bool mc_get_localtime(struct tm* t)
{
  if (!mc_net_time_ready()) return false;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  time_t sec = (time_t)ts.tv_sec;
  return localtime_r(&sec, t) != NULL;
}

/* ---------------------------------------------------------------------------
 *  背景：垂直渐变 + 一团淡柔光
 * ------------------------------------------------------------------------- */
static void softGlow(float cx, float cy, float r, uint16_t color, uint8_t peak)
{
  const int N = 10;
  const uint8_t per = (uint8_t)(peak / N ? peak / N : 1);
  for (int i = N; i >= 1; i--) {
    const float rr = r * (float)i / (float)N;
    const float rr2 = rr * rr;
    const int x0 = (int)floorf(cx - rr), x1 = (int)ceilf(cx + rr);
    const int y0 = (int)floorf(cy - rr), y1 = (int)ceilf(cy + rr);
    for (int y = y0; y <= y1; y++) {
      if (y < 0 || y >= GFX_H) continue;
      const float dy = (float)y + 0.5f - cy;
      for (int x = x0; x <= x1; x++) {
        const float dx = (float)x + 0.5f - cx;
        if (dx * dx + dy * dy <= rr2) gfx_blend(x, y, color, per);
      }
    }
  }
}

static void drawBackground(const Theme* th)
{
  gfx_vGrad(0, 0, GFX_W, GFX_H, th->bgTop, th->bgBot);
  const uint16_t glowCol = th->dark ? gfx_mix(th->bgTop, th->icon, 110)
                                    : gfx_mix(th->bgBot, th->iconSoft, 120);
  softGlow(MOOD_CX, MOOD_CY, 74.0f, glowCol, th->dark ? 52 : 64);
}

/* ---------------------------------------------------------------------------
 *  简笔心情
 * ------------------------------------------------------------------------- */
static void drawFaceEyes(float cx, float cy, float s, uint16_t col, bool happy)
{
  if (happy) {
    gfx_arc(cx - 7.0f * s, cy, 3.4f * s, 3.6f, 5.9f, 1.9f * s, col);
    gfx_arc(cx + 7.0f * s, cy, 3.4f * s, 3.6f, 5.9f, 1.9f * s, col);
  } else {
    gfx_disc(cx - 6.5f * s, cy, 1.9f * s, col);
    gfx_disc(cx + 6.5f * s, cy, 1.9f * s, col);
  }
}

static void drawSunIcon(float cx, float cy, float r, const Theme* th, bool smiling)
{
  for (int i = 0; i < 8; i++) {
    const float a = (float)i * 0.7853982f;
    const float c = cosf(a), s = sinf(a);
    gfx_capsule(cx + c * r * 1.32f, cy + s * r * 1.32f,
                cx + c * r * 1.78f, cy + s * r * 1.78f, r * 0.17f, th->icon);
  }
  gfx_disc(cx, cy, r, th->icon);
  const uint16_t feat = th->dark ? th->bgBot : th->text;
  drawFaceEyes(cx, cy - r * 0.10f, r / 22.0f, feat, smiling);
  if (smiling)
    gfx_arc(cx, cy + r * 0.06f, r * 0.56f, 0.55f, 2.59f, r * 0.14f, feat);
  else
    gfx_capsule(cx - r * 0.30f, cy + r * 0.40f, cx + r * 0.30f, cy + r * 0.40f,
                r * 0.13f, feat);
}

static void drawMoonIcon(float cx, float cy, float r, const Theme* th)
{
  const uint16_t bgc = gfx_mix(th->bgTop, th->bgBot,
                               (uint8_t)((int)(cy * 255.0f) / (GFX_H - 1)));
  gfx_disc(cx, cy, r, th->icon);
  gfx_disc(cx + r * 0.52f, cy - r * 0.20f, r * 0.92f, bgc);
  gfx_disc(cx - r * 1.15f, cy - r * 0.95f, r * 0.16f, th->icon);
  gfx_disc(cx - r * 0.55f, cy - r * 1.35f, r * 0.11f, th->icon);
  gfx_disc(cx + r * 1.05f, cy - r * 1.10f, r * 0.13f, th->icon);
  gfx_arc(cx - r * 0.18f, cy + r * 0.05f, r * 0.30f, 3.5f, 5.9f, r * 0.13f, th->iconSoft);
  gfx_arc(cx + r * 0.30f, cy + r * 0.05f, r * 0.30f, 3.5f, 5.9f, r * 0.13f, th->iconSoft);
}

static void drawCloudShape(float cx, float cy, float s, uint16_t col)
{
  gfx_disc(cx - s * 0.62f, cy + s * 0.10f, s * 0.46f, col);
  gfx_disc(cx + s * 0.02f, cy - s * 0.20f, s * 0.56f, col);
  gfx_disc(cx + s * 0.66f, cy + s * 0.06f, s * 0.42f, col);
  gfx_capsule(cx - s * 0.62f, cy + s * 0.34f, cx + s * 0.66f, cy + s * 0.34f,
              s * 0.62f, col);
}

static void drawMood(float cx, float cy, float r, int kind, const Theme* th, bool night)
{
  (void)night;
  const float s = r / 22.0f;
  const uint16_t feat = th->dark ? th->bgBot : th->text;
  switch (kind) {
    case MOOD_SUN:
      drawSunIcon(cx, cy, r, th, true);
      break;

    case MOOD_MOON:
      drawMoonIcon(cx, cy, r * 0.95f, th);
      break;

    case MOOD_PARTLY: {
      const float sx = cx - r * 0.38f, sy = cy - r * 0.42f;
      for (int i = 0; i < 8; i++) {
        const float a = (float)i * 0.7853982f;
        const float c = cosf(a), sn = sinf(a);
        gfx_capsule(sx + c * r * 0.90f, sy + sn * r * 0.90f,
                    sx + c * r * 1.24f, sy + sn * r * 1.24f, r * 0.14f, th->icon);
      }
      gfx_disc(sx, sy, r * 0.68f, th->icon);
      drawCloudShape(cx + r * 0.14f, cy + r * 0.30f, r * 0.86f, th->iconSoft);
      break;
    }

    case MOOD_CLOUD:
      drawCloudShape(cx, cy, r * 1.20f, th->iconSoft);
      drawFaceEyes(cx, cy - r * 0.06f, s * 0.92f, feat, false);
      gfx_capsule(cx - r * 0.26f, cy + r * 0.36f, cx + r * 0.26f, cy + r * 0.36f,
                  r * 0.11f, feat);
      break;

    case MOOD_RAIN:
      drawCloudShape(cx, cy - r * 0.30f, r * 1.12f, th->iconSoft);
      for (int i = -1; i <= 1; i++) {
        const float x = cx + (float)i * r * 0.46f;
        gfx_capsule(x + r * 0.10f, cy + r * 0.48f, x - r * 0.06f, cy + r * 0.96f,
                    r * 0.14f, th->icon);
      }
      break;

    case MOOD_SNOW:
      drawCloudShape(cx, cy - r * 0.30f, r * 1.12f, th->iconSoft);
      for (int i = -1; i <= 1; i++) {
        const float x = cx + (float)i * r * 0.50f;
        const float y = cy + r * 0.66f + ((i == 0) ? r * 0.22f : 0.0f);
        gfx_disc(x, y, r * 0.13f, th->icon);
      }
      break;

    case MOOD_STORM:
      drawCloudShape(cx, cy - r * 0.34f, r * 1.10f, th->iconSoft);
      gfx_capsule(cx + r * 0.10f, cy + r * 0.34f, cx - r * 0.18f, cy + r * 0.66f,
                  r * 0.16f, th->icon);
      gfx_capsule(cx - r * 0.18f, cy + r * 0.62f, cx + r * 0.06f, cy + r * 0.62f,
                  r * 0.14f, th->icon);
      gfx_capsule(cx + r * 0.06f, cy + r * 0.62f, cx - r * 0.20f, cy + r * 0.98f,
                  r * 0.16f, th->icon);
      break;

    case MOOD_FOG:
      drawCloudShape(cx, cy - r * 0.44f, r * 1.02f, th->iconSoft);
      for (int i = 0; i < 3; i++) {
        const float w = r * (0.90f - 0.14f * i);
        gfx_capsule(cx - w + r * 0.10f * i, cy + r * 0.26f + (float)i * r * 0.32f,
                    cx + w - r * 0.10f * i, cy + r * 0.26f + (float)i * r * 0.32f,
                    r * 0.13f, th->icon);
      }
      break;

    default:
      drawSunIcon(cx, cy, r, th, true);
      break;
  }
}

static int moodForWxCode(const char* code, bool night)
{
  if (!code || !code[0]) return night ? MOOD_MOON : MOOD_SUN;

  const char* p = code;
  if (*p == 'd' || *p == 'n' || *p == 'D' || *p == 'N') p++;
  const int n = atoi(p);

  switch (n) {
    case 0:  return night ? MOOD_MOON : MOOD_SUN;
    case 1:  return MOOD_PARTLY;
    case 2:  return MOOD_CLOUD;
    case 3:  return MOOD_RAIN;
    case 4:  case 5:  return MOOD_STORM;
    case 6:  return MOOD_SNOW;
    case 7:  case 8:  case 9:
    case 10: case 11: case 12:
    case 19:
    case 21: case 22: case 23: case 24: case 25:
      return MOOD_RAIN;
    case 13: case 14: case 15: case 16: case 17:
    case 26: case 27: case 28:
      return MOOD_SNOW;
    case 18: case 20:
    case 29: case 30: case 31:
    case 53:
      return MOOD_FOG;
    default: return MOOD_CLOUD;
  }
}

/* ---------------------------------------------------------------------------
 *  反馈 / 摇晃 / 心率 / 翻页键
 * ------------------------------------------------------------------------- */
static void triggerAck(uint16_t vibN)
{
  if (!s_ackEnable) return;
  const uint32_t now = mc_millis();
  /* 注：蜂鸣（无源喇叭 PWM）在 NuttX 移植中暂缺，见文件头差异说明 */
  const uint32_t end = now + (uint32_t)vibN * (ACK_VIB_ON_MS + ACK_VIB_OFF_MS) + 50UL;
  if (end > s_vibUntil) s_vibUntil = end;
  if (!s_vibOn) {
    s_vibOn = true;
    mc_gpio_write(PIN_MOTOR, true);
    s_vibToggle = now + ACK_VIB_ON_MS;
  }
}

static void feedbackTick(void)
{
  const uint32_t now = mc_millis();
  if (!s_vibUntil) return;
  if (now >= s_vibUntil) {
    s_vibUntil = 0;
    s_vibOn = false;
    mc_gpio_write(PIN_MOTOR, false);
    return;
  }
  if (now >= s_vibToggle) {
    s_vibOn = !s_vibOn;
    mc_gpio_write(PIN_MOTOR, s_vibOn);
    s_vibToggle = now + (s_vibOn ? ACK_VIB_ON_MS : ACK_VIB_OFF_MS);
  }
}

static void applyShake(void)
{
  const uint32_t now = mc_millis();
  s_lastShakeMs = now;
  if (s_hrBpm < 165) s_hrBpm += 2;
  if (s_page == PAGE_FEEL) {
    s_feel = (Feel)((s_feel + 1) & 3);
    triggerAck(ACK_VIB_N_MOOD);
    drawFeelPage();
    printf("[feel] %s (pulses=%u)\n", kFeelName[s_feel], s_shakePulses);
  } else {
    triggerAck(ACK_VIB_N_HOME);
    s_lastMinKey = -1;             /* 立即重画主页（心率更新） */
  }
  s_shakePulses = 0;
}

static void onShake(void)          /* 串口 SHAKE 命令的模拟入口 */
{
  s_shakePulses++;
  applyShake();
}

static void pollShake(void)
{
  const uint32_t now = mc_millis();
  const bool lvl = (mc_gpio_read(PIN_SHAKE) != false);
  if (lvl != s_shakePrev) {
    s_shakePrev = lvl;
    if (s_lastCntMs == 0 || (now - s_lastCntMs) > SHAKE_DEBOUNCE_MS) {
      s_lastCntMs = now;
      /* 只记脉冲不立即切换：稳定 500ms 后统一应用（摇晃聚合） */
      s_shakePulses++;
      s_shakePending = true;
      s_shakeSettleAt = now + 500;
    }
  }
  if (s_shakePending && (int32_t)(now - s_shakeSettleAt) >= 0) {
    s_shakePending = false;
    applyShake();
  }
}

static void hrTick(void)
{
  static uint32_t last = 0;
  const uint32_t now = mc_millis();
  if (now - last < 1000) return;
  last = now;
  if (s_hrBpm > 72 && (s_lastShakeMs == 0 || now - s_lastShakeMs > 5000UL)) s_hrBpm--;
}

static void pollButton(void)
{
  const uint32_t now = mc_millis();
  const bool pressed = (mc_gpio_read(PIN_BTN) == false);
  if (pressed != s_btnPrev) {
    s_btnPrev = pressed;
    if (pressed && now - s_btnMs > 250) {
      s_btnMs = now;
      s_page = (s_page == PAGE_HOME) ? PAGE_FEEL : PAGE_HOME;
      printf("[page] %s\n", (s_page == PAGE_HOME) ? "home" : "feel");
      if (s_page == PAGE_FEEL) drawFeelPage();
      else                     s_lastMinKey = -1;
    }
  }
}

/* ---------------------------------------------------------------------------
 *  心情页：自己的心情 -> 屏幕颜色 + 卡通表情
 * ------------------------------------------------------------------------- */
static void drawFeelPage(void)
{
  typedef struct { uint16_t top, bot, feat, cheek; } FeelSkin;
  static const FeelSkin kSkin[4] = {
    { 0xFDAB, 0xF9EF, 0x4204, 0xFCF6 },   /* 开心：亮粉红 */
    { 0xBABA, 0x8010, 0x4204, 0xDE5E },   /* 伤心：紫色 */
    { 0xD8A7, 0x8800, 0x4204, 0xFC4E },   /* 生气：红色 */
    { 0x4416, 0x18CE, 0x4204, 0xBF1E },   /* 平静：蓝色 */
  };
  const FeelSkin* sk = &kSkin[s_feel];

  gfx_vGrad(0, 0, GFX_W, GFX_H, sk->top, sk->bot);

  const float cx = GFX_W / 2.0f, cy = 64.0f, r = 46.0f;
  const float ex = r * 0.34f, ey = cy - r * 0.14f, er = r * 0.09f;

  gfx_disc(cx, cy, r, 0xFFFF);
  gfx_disc(cx - r * 0.62f, cy + r * 0.30f, r * 0.15f, sk->cheek);
  gfx_disc(cx + r * 0.62f, cy + r * 0.30f, r * 0.15f, sk->cheek);

  switch (s_feel) {
    case FEEL_HAPPY:
      gfx_arc(cx - ex, ey, er * 1.7f, 3.6f, 5.9f, er * 0.8f, sk->feat);
      gfx_arc(cx + ex, ey, er * 1.7f, 3.6f, 5.9f, er * 0.8f, sk->feat);
      gfx_arc(cx, cy + r * 0.26f, r * 0.42f, 0.45f, 2.69f, r * 0.11f, sk->feat);
      break;

    case FEEL_SAD:
      gfx_disc(cx - ex, ey, er, sk->feat);
      gfx_disc(cx + ex, ey, er, sk->feat);
      gfx_arc(cx, cy + r * 0.38f, r * 0.26f, 3.45f, 5.97f, r * 0.10f, sk->feat);
      gfx_capsule(cx - ex, cy + r * 0.10f, cx - ex - r * 0.07f, cy + r * 0.46f,
                  r * 0.10f, 0x7E1E);
      gfx_capsule(cx + ex, cy + r * 0.10f, cx + ex + r * 0.07f, cy + r * 0.46f,
                  r * 0.10f, 0x7E1E);
      break;

    case FEEL_ANGRY:
      gfx_capsule(cx - ex - er * 1.2f, ey - er * 2.4f, cx - ex + er * 1.0f,
                  ey - er * 1.2f, er * 0.7f, sk->feat);
      gfx_capsule(cx + ex + er * 1.2f, ey - er * 2.4f, cx + ex - er * 1.0f,
                  ey - er * 1.2f, er * 0.7f, sk->feat);
      gfx_disc(cx - ex, ey + er * 1.1f, er * 0.85f, sk->feat);
      gfx_disc(cx + ex, ey + er * 1.1f, er * 0.85f, sk->feat);
      gfx_arc(cx, cy + r * 0.42f, r * 0.24f, 3.5f, 5.9f, r * 0.10f, sk->feat);
      break;

    default:
      gfx_capsule(cx - ex - er * 0.7f, ey, cx - ex + er * 0.7f, ey,
                  er * 0.55f, sk->feat);
      gfx_capsule(cx + ex - er * 0.7f, ey, cx + ex + er * 0.7f, ey,
                  er * 0.55f, sk->feat);
      gfx_capsule(cx - r * 0.20f, cy + r * 0.36f, cx + r * 0.20f, cy + r * 0.36f,
                  r * 0.08f, sk->feat);
      break;
  }

  char nb[40];
  snprintf(nb, sizeof(nb), "\xE5\xBF\x83\xE6\x83\x85: %s", kFeelName[s_feel]);  /* 心情: xx */
  gfx_textC(GFX_W / 2, 118, nb, 0xFFFF, false);
  gfx_textC(GFX_W / 2, 146,
            "\xE6\x91\x87\xE4\xB8\x80\xE6\x91\x87 \xE6\x8D\xA2\xE4\xB8\xAA"
            "\xE5\xBF\x83\xE6\x83\x85",          /* 摇一摇 换个心情 */
            0xFFFF, false);

  gfx_flushAll();
}

/* ---------------------------------------------------------------------------
 *  主页 / 状态页
 * ------------------------------------------------------------------------- */
static void drawHeart(float cx, float cy, float s, uint16_t col)
{
  gfx_disc(cx - 2.6f * s, cy - 1.7f * s, 2.9f * s, col);
  gfx_disc(cx + 2.6f * s, cy - 1.7f * s, 2.9f * s, col);
  gfx_capsule(cx - 4.8f * s, cy - 0.8f * s, cx, cy + 4.8f * s, 2.0f * s, col);
  gfx_capsule(cx + 4.8f * s, cy - 0.8f * s, cx, cy + 4.8f * s, 2.0f * s, col);
}

static void drawClockPage(const struct tm* t)
{
  const int hour = t ? t->tm_hour : 12;
  const int ti = (s_forceTheme >= 0) ? s_forceTheme : themeIndexForHour(hour);
  s_themeIdx = ti;
  const Theme* th = &kThemes[ti];
  const bool night = (hour >= 20 || hour < 6);

  drawBackground(th);

  /* 时间（未对时显示 --:--） */
  char buf[16];
  if (t) snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  else   snprintf(buf, sizeof(buf), "--:--");
  gfx_text(TIME_X, TIME_Y, buf, th->text, true);

  /* 日期 */
  char dbuf[48];
  if (t) {
    snprintf(dbuf, sizeof(dbuf), "%d\xE6\x9C\x88%d\xE6\x97\xA5 \xE5\x91\xA8%s",
             t->tm_mon + 1, t->tm_mday, kWk[t->tm_wday % 7]);   /* x月x日 周x */
  } else {
    snprintf(dbuf, sizeof(dbuf), "--");
  }
  gfx_text(DATE_X, DATE_Y, dbuf, th->sub, false);

  /* 心率 */
  {
    char hb[24];
    snprintf(hb, sizeof(hb), "\xE5\xBF\x83\xE7\x8E\x87 %d", s_hrBpm);  /* 心率 xx */
    const int tw = gfx_textW(hb, false);
    const int tx = WX_RIGHT - tw;
    drawHeart((float)tx - 15.0f, (float)DATE_Y + 11.0f, 2.0f, th->accent);
    gfx_text(tx, DATE_Y + 1, hb, th->text, false);
  }

  /* 分隔线 */
  gfx_softHLine(DATE_X, RULE_Y, WX_RIGHT - DATE_X, th->accent, th->dark ? 90 : 70);

  /* 天气 */
  if (s_wx.valid) {
    char wbuf[64];
    snprintf(wbuf, sizeof(wbuf), "%.1f\xE2\x84\x83  %s", (double)s_wx.tempC,
             s_wx.text[0] ? s_wx.text : "--");                        /* xx.x°C */
    gfx_text(WX_X, WX_Y, wbuf, th->text, false);

    char hbuf[36];
    snprintf(hbuf, sizeof(hbuf),
             "\xE6\xB9\xBF\xE5\xBA\xA6 %d%%", s_wx.humidity);          /* 湿度 xx% */
    gfx_text(WX_RIGHT - gfx_textW(hbuf, false), WX_Y, hbuf, th->sub, false);
  } else {
    gfx_text(WX_X, WX_Y,
             "\xE6\xAD\xA3\xE5\x9C\xA8\xE8\x8E\xB7\xE5\x8F\x96\xE5\xA4\xA9"
             "\xE6\xB0\x94",                                           /* 正在获取天气 */
             th->sub, false);
  }

  /* 心情图标 */
  const int kind = (s_forceMood <= 7) ? (int)s_forceMood
                                      : moodForWxCode(s_wx.code, night);
  drawMood(MOOD_CX, MOOD_CY, MOOD_R, kind, th, night);

  gfx_flushAll();
}

static void drawStatusPage(const char* title, const char* sub, int phase)
{
  const Theme* th = &kThemes[1];
  gfx_vGrad(0, 0, GFX_W, GFX_H, th->bgTop, th->bgBot);

  gfx_textC(GFX_W / 2, 52, title, th->text, false);
  gfx_textC(GFX_W / 2, 86, sub, th->sub, false);

  const int n = phase % 3;
  for (int i = 0; i < 3; i++) {
    const float a = (i == n) ? 1.0f : 0.35f;
    gfx_disc(GFX_W / 2.0f - 16.0f + (float)i * 16.0f, 122.0f, 3.2f,
             gfx_mix(th->bgBot, th->accent, (uint8_t)(a * 255.0f)));
  }
}

/* ---------------------------------------------------------------------------
 *  控制台命令（nsh 控制台 stdin 非阻塞轮询）
 * ------------------------------------------------------------------------- */
static void printInfo(void)
{
  printf("--- INFO ---\n");
  printf("[lcd] boot: attempts=%u\n", tds3_bootAttempts());
  printf("stage      : %d\n", (int)s_stage);
  printf("wifi       : %d\n", mc_net_wifi_state());
  printf("time ready : %d\n", (int)mc_net_time_ready());
  struct tm t;
  if (mc_get_localtime(&t))
    printf("now        : %04d-%02d-%02d %02d:%02d:%02d\n",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  printf("theme      : %d (%s)\n", s_themeIdx,
         (s_themeIdx >= 0 && s_themeIdx <= 3) ? kThemes[s_themeIdx].name : "-");
  printf("weather    : valid=%d %d%% code=%s\n",
         (int)s_wx.valid, s_wx.humidity, s_wx.code);
  printf("ack feedback : %s (VIBON/VIBOFF)\n", s_ackEnable ? "ON" : "OFF");
}

static void cmdPat(void)           /* PAT：绕过帧缓冲直写面板 8 色条 */
{
  static const uint16_t bars[8] = {
    0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
  };
  const int bw = tds3_width() / 8;
  for (int i = 0; i < 8; i++)
    tds3_fillRect(i * bw, 0, (i == 7) ? (tds3_width() - 7 * bw) : bw,
                  tds3_height(), bars[i]);
  printf("[PAT] direct test pattern sent (bypass fb)\n");
}

/* 非阻塞读一个控制台字节：nsh 下 stdin 是阻塞的，直接 read 会卡死
 * 主循环，必须先 poll 确认可读。 */
static int stdin_read_byte(void)
{
  struct pollfd pfd;
  pfd.fd      = 0;          /* stdin = nsh 控制台 */
  pfd.events  = POLLIN;
  pfd.revents = 0;
  if (poll(&pfd, 1, 0) <= 0) return -1;
  unsigned char c;
  if (read(0, &c, 1) == 1) return (int)c;
  return -1;
}

static void serviceSerial(void)
{
  static char line[64];
  static int  n = 0;
  int c;

  while ((c = stdin_read_byte()) != -1) {
    char ch = (char)c;
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[n] = 0;
      n = 0;
      if (!line[0]) continue;

      if (!strcmp(line, "INFO")) {
        printInfo();
      } else if (!strcmp(line, "REFRESH")) {
        s_lastMinKey = -1;
        printf("[cmd] redraw\n");
      } else if (!strcmp(line, "TP")) {
        s_testHold = true;
        gfx_clear(0xFFFF);
        gfx_fillRect(0, 0, GFX_W, 22, 0xF800);
        gfx_fillRect(0, GFX_H - 22, GFX_W, 22, 0x07E0);
        gfx_text(14, 28, "88:88", 0x0000, true);
        gfx_disc(282, 62, 28, 0x0000);
        gfx_flushAll();
        printf("[TP] test page shown\n");
      } else if (!strcmp(line, "TP0")) {
        s_testHold = false;
        s_lastMinKey = -1;
        printf("[cmd] test released\n");
      } else if (!strcmp(line, "SUM")) {
        uint32_t sum = 0;
        const uint16_t* p = gfx_fb;
        for (size_t i = 0; i < (size_t)GFX_W * GFX_H; i++) sum = sum * 31 + p[i];
        printf("[SUM] fb checksum=%08X (Arduino base 0BE91584)\n", (unsigned)sum);
      } else if (!strcmp(line, "PAT")) {
        cmdPat();
      } else if (!strncmp(line, "MOOD ", 5)) {
        const int v = atoi(line + 5);
        s_forceMood = (v < 0 || v > 7) ? 255 : (uint8_t)v;
        s_lastMinKey = -1;
        printf("[cmd] mood=%d\n", (int)s_forceMood);
      } else if (!strncmp(line, "THEME ", 6)) {
        const int v = atoi(line + 6);
        s_forceTheme = (v < 0 || v > 3) ? -1 : v;
        s_lastMinKey = -1;
        printf("[cmd] theme=%d\n", s_forceTheme);
      } else if (!strncmp(line, "PAGE ", 5)) {
        const int v = atoi(line + 5);
        s_page = (v == 1) ? PAGE_FEEL : PAGE_HOME;
        if (s_page == PAGE_FEEL) drawFeelPage(); else s_lastMinKey = -1;
        printf("[cmd] page=%s\n", (s_page == PAGE_HOME) ? "home" : "feel");
      } else if (!strncmp(line, "FEEL ", 5)) {
        const int v = atoi(line + 5);
        if (v >= 0 && v <= 3) {
          s_feel = (Feel)v;
          printf("[cmd] feel=%s\n", kFeelName[s_feel]);
          if (s_page == PAGE_FEEL) drawFeelPage();
        } else {
          printf("[cmd] FEEL 0=happy 1=sad 2=angry 3=calm\n");
        }
      } else if (!strcmp(line, "SHAKE")) {
        printf("[cmd] simulate shake\n");
        onShake();
      } else if (!strcmp(line, "REINIT")) {
        tds3_reinit();
        tds3_fillScreen(0xF800);
        printf("[cmd] reinit + red\n");
        s_lastMinKey = -1;
      } else if (!strcmp(line, "VIBOFF") || !strcmp(line, "VIBON")) {
        s_ackEnable = !strcmp(line, "VIBON");
        if (!s_ackEnable) {
          s_vibUntil = 0; s_vibOn = false;
          mc_gpio_write(PIN_MOTOR, false);
        }
        printf("[cmd] ack feedback = %s\n", s_ackEnable ? "ON" : "OFF");
      } else if (!strncmp(line, "BRIGHT ", 7)) {
        const int v = atoi(line + 7);
        tds3_setBacklight((uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)));
        printf("[cmd] backlight=%d\n", v);
      } else {
        printf("cmd: INFO | PAGE 0/1 | FEEL 0-3 | SHAKE | MOOD n | THEME n | BRIGHT n\n");
      }
      fflush(stdout);
    } else {
      if (n < (int)sizeof(line) - 1) line[n++] = ch;
    }
  }
}

/* ---------------------------------------------------------------------------
 *  主循环服务
 * ------------------------------------------------------------------------- */
static void serviceBackground(void)
{
  if (!mc_net_wifi_ok()) {
    if (s_stage == ST_WIFI) {
      /* 离线骨架：WiFi 永远连不上 —— 失败计数到上限后放弃联网，
       * 直接进离线运行态（时钟页显示 --:--，不卡在状态页） */
      if (mc_millis() - s_lastWifiTry > WIFI_RETRY_MS) {
        s_lastWifiTry = mc_millis();
        s_wifiFailCnt++;
        if (s_wifiFailCnt >= WIFI_MAX_FAILS) {
          printf("[wifi] give up after %u fails, offline mode\n",
                 (unsigned)s_wifiFailCnt);
          s_stage = ST_RUN;
          s_lastMinKey = -1;
        } else {
          printf("[wifi] retry\n");
          mc_net_start_wifi(WIFI_SSID, WIFI_PASS);
        }
      }
    }
    return;
  }

  if (s_stage == ST_WIFI) {
    mc_net_start_ntp();
    s_stage = ST_TIME;
    s_statusTick = 0;
    drawStatusPage("\xE5\xB7\xB2\xE8\xBF\x9E\xE6\x8E\xA5"    /* 已连接 */
                   "", "", 0);
    gfx_flushAll();
  }

  if (s_stage == ST_TIME) {
    if (mc_net_time_ready()) {
      s_stage = ST_RUN;
      s_themeIdx = -1;
      s_lastMinKey = -1;
    } else if (mc_millis() - s_lastWifiTry > 20000UL) {
      s_stage = ST_RUN;            /* 对时超时也进运行态（--:--） */
      s_lastMinKey = -1;
    }
  }
  else if (s_stage == ST_RUN) {
    if (s_lastWxMs == 0 || mc_millis() - s_lastWxMs > WX_REFRESH_MS) {
      if (mc_net_fetch_weather(LOC_CITY_CODE, &s_wx)) s_lastWxMs = mc_millis();
      else s_lastWxMs = mc_millis() - WX_REFRESH_MS + 60000UL;
      s_lastMinKey = -1;
    }
  }
}

/* ---------------------------------------------------------------------------
 *  main —— NuttX/openVela 内置应用入口
 * ------------------------------------------------------------------------- */
int moodclock_main(int argc, char* argv[])
{
  (void)argc;
  (void)argv;

  printf("\n=== T-Display-S3 MoodClock (openVela/NuttX port) ===\n");

  /* ---- setup：LCD 第一（冷启动时序铁律与 Arduino 版一致） ---- */
  if (!tds3_begin()) printf("[lcd] begin failed\n");
  printf("[boot] lcd first done\n");

  mc_delay_ms(200);

  /* ---- 外设 ---- */
  mc_gpio_input(PIN_SHAKE);
  mc_gpio_input_pullup(PIN_BTN);
  mc_gpio_output(PIN_MOTOR);
  mc_gpio_write(PIN_MOTOR, false);
  s_shakePrev = (mc_gpio_read(PIN_SHAKE) != false);
  s_btnPrev   = (mc_gpio_read(PIN_BTN) != false);
  printf("[io] shake=P2 motor=P1 btn=P0 (speaker beep TODO)\n");

  /* ---- 显存 ---- */
  if (!gfx_begin()) {
    printf("[gfx] frame buffer alloc failed\n");
    while (true) mc_delay_ms(1000);
  }

  /* ---- 开机静默期：先画完整时钟页，画面静止，推迟联网 ---- */
  {
    struct tm t0;
    memset(&t0, 0, sizeof(t0));
    drawClockPage(NULL);          /* 未对时：--:-- 占位 */
    gfx_flushAll();
  }
  s_wifiDeferUntil = mc_millis() + 3000;

  /* ---- loop ---- */
  while (true) {
    serviceSerial();

    if (s_stage == ST_IDLE) {
      pollShake();
      pollButton();
      feedbackTick();
      hrTick();
      if (s_wifiDeferUntil && mc_millis() >= s_wifiDeferUntil) {
        s_wifiDeferUntil = 0;
        printf("[wifi] deferred start\n");
        mc_net_start_wifi(WIFI_SSID, WIFI_PASS);
        s_lastWifiTry = mc_millis();
        s_stage = ST_WIFI;
      }
      nxsig_usleep(2000);
      continue;
    }

    if (s_stage == ST_WIFI || s_stage == ST_TIME) {
      static uint32_t lastAnim = 0;
      if (mc_millis() - lastAnim > 600) {
        lastAnim = mc_millis();
        s_statusTick++;
        /* 连接网络 / 正在接入 */
        drawStatusPage("\xE8\xBF\x9E\xE6\x8E\xA5\xE7\xBD\x91\xE7\xBB\x9C",
                       "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x8E\xA5\xE5\x85\xA5",
                       s_statusTick);
        gfx_flushAll();
      }
    }

    serviceBackground();

    if (s_stage == ST_RUN) {
      pollShake();
      pollButton();
      hrTick();
    }
    feedbackTick();

    /* 屏幕自愈：每 30s 再断言一次供电与背光 */
    static uint32_t s_lastHealMs = 0;
    if (s_stage == ST_RUN && mc_millis() - s_lastHealMs > 30000UL) {
      s_lastHealMs = mc_millis();
      tds3_setPower(true);
      tds3_setBacklight(255);
    }

    /* 每分钟重画时钟页 */
    if (s_stage == ST_RUN && !s_testHold && s_page == PAGE_HOME) {
      struct tm t;
      const bool have = mc_get_localtime(&t);
      const int key = have
        ? (t.tm_year * 1000000) + ((t.tm_mon + 1) * 10000) +
          (t.tm_mday * 100) + t.tm_hour * 60 + t.tm_min
        : 0;
      const int ti = (s_forceTheme >= 0) ? s_forceTheme
                                         : themeIndexForHour(have ? t.tm_hour : 12);
      if (key != s_lastMinKey || ti != s_themeIdx) {
        s_lastMinKey = key;
        drawClockPage(have ? &t : NULL);
      }
    }

    nxsig_usleep(2000);   /* 摇晃轮询节拍 2ms */
  }

  return 0;   /* 不可达 */
}
