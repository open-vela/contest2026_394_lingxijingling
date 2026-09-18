// ============================================================================
//  mc_net.h —— WiFi / NTP 对时 / 天气（NuttX/openVela 版）
//
//  原 Arduino 版用 WiFi.h + SNTP + HTTP 明文拉中国天气网。
//  在 NuttX/openVela 里对应：
//    WiFi   -> WAPI（netutils/wapi，wifi_start 等板级 WiFi 服务，
//                ESP32-S3 上 openVela/NuttX 的 WiFi 驱动可用性视内核配置而定）
//    NTP    -> netutils 的 SNTP 客户端（ntpc_start）
//    天气   -> netutils webclient（HTTP 明文）拉
//              http://d1.weather.com.cn/sk_2d/<城市代码>.html
//
//  ★ 本文件当前为“离线骨架”：接口与状态机完整，联网函数返回失败并留
//    TODO。UI 层对失败完全免疫（占位时间 + “正在获取天气”），接上
//    openVela 的 WiFi/网络栈后把 TODO 填掉即可恢复联网能力。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
  bool  valid;
  float tempC;          /* 气温 */
  int   humidity;       /* 相对湿度 % */
  int   aqi;            /* 空气质量指数 */
  char  text[28];       /* 天气现象 "多云" */
  char  wind[28];       /* 风向风力 "东南风1级" */
  char  code[8];        /* 天气代码 "d01" / "n01" */
  char  obsTime[12];    /* 观测时间 "15:05" */
} Weather;

void mc_net_start_wifi(const char* ssid, const char* pass);
int  mc_net_wifi_state(void);      /* 0=连接中 1=已连上 2=超时失败 */
bool mc_net_wifi_ok(void);

void mc_net_start_ntp(void);
bool mc_net_time_ready(void);      /* 系统时钟已被 SNTP 对上 */

bool mc_net_fetch_weather(const char* cityCode, Weather* out);
