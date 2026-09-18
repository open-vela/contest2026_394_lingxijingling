// ============================================================================
//  mc_net.c —— WiFi / NTP / 天气（NuttX/openVela 版，离线骨架 + TODO）
// ============================================================================

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "mc_net.h"

/* 对时成功的时间下限：早于 2026-01-01 视为“系统时钟还没被 SNTP 对上” */
#define MC_EPOCH_MIN (1767196800L)   /* 2026-01-01 00:00:00 UTC */

static int  s_wifiState = 2;       /* 初始=失败态；联网栈接入后由驱动更新 */
static bool s_ntpStarted = false;

/* ---------------------------------------------------------------------------
 *  WiFi
 *  TODO(openVela)：
 *    1. 板级 defconfig 打开 ESP32-S3 WiFi（视 openVela 版本的
 *       CONFIG_ESP32S3_WIFI / netutils wapi 配置项而定）；
 *    2. 参考 NuttX netutils/wapi：wifi_start -> wapi_set_essid ->
 *       wapi_set_freq/wpa -> 获取 IP（DHCP）；
 *    3. 连接结果回调里更新 s_wifiState。
 * ------------------------------------------------------------------------- */
void mc_net_start_wifi(const char* ssid, const char* pass)
{
  (void)ssid;
  (void)pass;
  printf("[wifi] TODO: openVela WAPI not wired yet (offline mode)\n");
  s_wifiState = 2;
}

int mc_net_wifi_state(void)
{
  return s_wifiState;
}

bool mc_net_wifi_ok(void)
{
  return s_wifiState == 1;
}

/* ---------------------------------------------------------------------------
 *  SNTP 对时
 *  TODO(openVela)：CONFIG_NETUTILS_NTPC=y 后调用 ntpc_start(...)
 *  （netutils/ntpc）。本骨架只检查系统时钟是否已被拨到合理年代。
 * ------------------------------------------------------------------------- */
void mc_net_start_ntp(void)
{
  s_ntpStarted = true;
  printf("[ntp] TODO: start SNTP client (netutils/ntpc)\n");
}

bool mc_net_time_ready(void)
{
  if (!s_ntpStarted) return false;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (long)ts.tv_sec >= MC_EPOCH_MIN;
}

/* ---------------------------------------------------------------------------
 *  天气：中国天气网实况接口（明文 HTTP，国内可达、免 key）
 *    http://d1.weather.com.cn/sk_2d/<城市代码>.html
 *  返回：var dataSK={"temp":"33.6","SD":"57%","weather":"多云",
 *                     "weathercode":"d01",...}
 *
 *  TODO(openVela)：CONFIG_NETUTILS_WEBCLIENT=y 后用 webclient_get()
 *  拉正文，按上面的 JSON 字段名 strstr+atof 解析（Arduino 版 net.cpp
 *  的解析逻辑可原样抄）。城市代码：汝城 = 101250508。
 * ------------------------------------------------------------------------- */
bool mc_net_fetch_weather(const char* cityCode, Weather* out)
{
  (void)cityCode;
  if (out) out->valid = false;
  return false;
}
