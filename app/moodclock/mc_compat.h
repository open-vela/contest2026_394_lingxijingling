// ============================================================================
//  mc_compat.h —— Arduino API -> NuttX/openVela 兼容层
//
//  移植自 mood_clock（Arduino/ESP32）到 openVela（NuttX 内核）。
//  本文件把 Arduino 惯用的时间/类型 API 映射到 NuttX 等价物，
//  让上层逻辑代码（页面调度/去抖/非阻塞状态机）几乎原样搬过来。
//
//  ★ 平台适配点集中在 mc_gpio.c，本文件只做时间与类型。
// ============================================================================
#pragma once

#include <nuttx/config.h>
#include <nuttx/arch.h>          /* up_udelay() */
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* millis() —— Arduino 的毫秒时钟。NuttX 用 CLOCK_MONOTONIC 等价实现 */
static inline uint32_t mc_millis(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000UL + (uint64_t)ts.tv_nsec / 1000000UL);
}

/* delay(ms) —— NuttX 休眠毫秒 */
static inline void mc_delay_ms(uint32_t ms)
{
  nxsig_usleep((useconds_t)ms * 1000UL);
}

/* delayMicroseconds(us) —— NuttX 忙等微秒（LCD 时序关键路径用，
 * 不能用 nxsig_usleep：其精度受系统 tick 限制，会破坏 5µs 级 WR 时序） */
static inline void mc_delay_us(uint32_t us)
{
  up_udelay((unsigned int)us);
}

/* 串口输出：NuttX 任务里 printf 直接走控制台（nsh 串口），
 * 与 Arduino Serial.print 等价。F() 宏在 C 里无意义，直接去掉。 */
#define F(x) (x)
