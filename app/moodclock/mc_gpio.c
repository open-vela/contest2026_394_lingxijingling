// ============================================================================
//  mc_gpio.c —— GPIO HAL 薄封装（NuttX/openVela ESP32-S3 私有 HAL）
//
//  ★★ 平台适配点：下面 4 处调用的签名以你手上 openVela 源码树里的
//      kernel/arch/xtensa/src/esp32s3/esp32s3_gpio.h
//  为准。NuttX 各版本该 HAL 签名有差异，但语义恒定为：
//      配置引脚（功能/方向/上下拉）→ esp32s3_gpiowrite / esp32s3_gpioread
//  若你的树里叫别的名字（如 esp32s3_gpio_write），全文件替换即可。
// ============================================================================

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/* openVela / NuttX ESP32-S3 GPIO 私有 HAL（路径以你的源码树为准） */
#include "esp32s3_gpio.h"

void mc_gpio_output(int pin)
{
  /* 方向输出；Arduino 里 pinMode OUTPUT 后引脚默认低电平 */
  esp32s3_gpio_config(pin, FUNCTION_2, OUTPUT, FLOAT);
  esp32s3_gpiowrite(pin, false);
}

void mc_gpio_output_init_high(int pin)
{
  /* ★ 先写寄存器再切方向 —— 保证引脚“一出生就是高”，不留低电平毛刺
   * （Arduino digitalWrite->pinMode 顺序的 NuttX 等价写法） */
  esp32s3_gpiowrite(pin, true);
  esp32s3_gpio_config(pin, FUNCTION_2, OUTPUT, FLOAT);
}

void mc_gpio_input(int pin)
{
  esp32s3_gpio_config(pin, FUNCTION_2, INPUT, FLOAT);
}

void mc_gpio_input_pullup(int pin)
{
  esp32s3_gpio_config(pin, FUNCTION_2, INPUT, PULLUP);
}

void mc_gpio_write(int pin, bool v)
{
  esp32s3_gpiowrite(pin, v);
}

bool mc_gpio_read(int pin)
{
  return esp32s3_gpioread(pin);
}
