// ============================================================================
//  mc_gpio.h —— GPIO HAL 薄封装（Arduino digitalWrite/pinMode 的 NuttX 等价物）
//
//  ★★ 全移植包唯一需要按 openVela 内核版本微调的文件 ★★
//  NuttX 各版本的 ESP32-S3 GPIO 私有 HAL 签名有差异（esp32s3_gpio_config 的
//  参数个数/枚举名随内核版本变过）。本文件把所有底层调用集中在 4 个函数里，
//  移植时只需对照你手上 openVela 源码树里的
//      kernel/arch/xtensa/src/esp32s3/esp32s3_gpio.h
//  改这几行，其余代码一概不碰。
// ============================================================================
#pragma once
#include <stdbool.h>
#include <stdint.h>

void mc_gpio_output(int pin);                       /* 输出，初始低 */
void mc_gpio_output_init_high(int pin);             /* 输出，初始高 */
void mc_gpio_input(int pin);                        /* 浮空输入 */
void mc_gpio_input_pullup(int pin);                 /* 上拉输入 */
void mc_gpio_write(int pin, bool v);                /* digitalWrite */
bool mc_gpio_read(int pin);                         /* digitalRead */
