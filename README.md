# T-Display-S3 心情时钟（moodclock）

## 一、作品简介

基于 LILYGO T-Display-S3（ESP32-S3 + ST7789 1.9" IPS 屏）的一体化心情时钟终端。第一页显示实时时间、模拟心率与实时天气气温；按 BOOT 键翻页进入心情页，摇晃传感器一次切换一个心情——开心（粉/笑脸）、伤心（紫/流泪）、生气（红）、平静（蓝），同时马达脉冲震动、无源喇叭发声反馈。核心亮点：从 8080 并口总线时序到中文字库渲染全部手写，通过逐字节片选翻转写协议 + 真断电复位环，把冷启动雪花屏从概率问题变成确定性问题（连续冷插一次点亮）。

## 二、选题方向

**AI 硬件产品创新**。作品为真实可用的桌面硬件终端（开源开发板 + 自研固件），全流程（驱动攻坚、UI、联网、交互）与 AI 结对完成，AI Coding 日志见 `logs/`。

## 三、目录结构

```text
app/moodclock/          # 作品代码：NuttX/openVela 应用包
├── moodclock_main.c    # 主程序：双页 UI 状态机、摇晃聚合、串口命令
├── tds3_st7789.c/.h    # ST7789 8080 并口驱动（寄存器级，含冷启动修复）
├── mc_gfx.c/.h         # 离屏帧缓冲 + SDF 抗锯齿中文字库渲染
├── mc_gpio.c/.h        # NuttX GPIO 适配层（唯一需按内核版本微调处）
├── mc_net.c/.h         # WiFi/NTP/天气接入骨架
├── mc_glyphs.h 等      # 中文字库与 5x7 点阵数据
└── Kconfig/Makefile/Make.defs  # apps 框架注册（编译后 nsh 输 moodclock 运行）
logs/                   # AI Coding 日志
```

## 四、运行方式

> openVela 官方编译环境为 Ubuntu 22.04 原生系统。

1. 按大赛流程拉取完整工程：
   ```bash
   repo init -u https://github.com/open-vela/<你的专属仓> -b dev-ai-contest-2026 -m <manifest>.xml
   repo sync -c -j8
   ```
2. `app/moodclock/` 经 manifest `<linkfile>` 映射进 openvela 编译树（`packages/demos/` 下）；若未配置映射，将本目录复制到 `packages/demos/moodclock` 并在 apps 的 Kconfig/Makefile 中登记 `CONFIG_MOODCLOCK` 即可。
3. 编译（在 openvela 工作区根目录）：
   ```bash
   ./build.sh <board-config-path> -j8
   ```
4. 产物烧录至 ESP32-S3 开发板后，nsh 控制台输入 `moodclock` 运行。
5. 交互说明：BOOT 键翻页；摇晃传感器切换心情/增加心率；串口命令 `INFO / SUM / PAT / VIBON / VIBOFF / FEEL 0~3` 可现场诊断与控制。

> 硬件接线：LCD 8080 并口（数据 39–48，WR=8/RD=9/DC=7/CS=6/RST=5/BL=38/PWR=15）；摇晃传感器 GPIO2、马达 GPIO1、喇叭 GPIO12、按钮 GPIO0。

## 五、AI Coding 使用说明

本作品全程与 AI 智能体（WorkBuddy / GLM）结对开发：

- **需求拆解与方案设计**：由口头需求（"每次开机都有固定画面"）共同推导出驱动级排查方案，按假设逐项设计对照实验。
- **编码**：AI 直接编写/修改全部约 2200 行固件代码，包括寄存器级并口驱动与中文渲染管线。
- **调试**：AI 通过串口监听脚本自动抓取冷启动日志，以帧缓冲校验和指纹法定位雪花屏根因（CS 常低快时序），7 版迭代修复并双轮冷插复测验证。
- **文档**：本 README、技术报告与解说词均由 AI 依据项目实况生成。

AI 将一次硬件疑难杂症（冷插雪花）的定位从"无头绪"压缩为 6 轮对照实验 + 1 次根因命中，显著提升了调试效率。完整对话与操作日志见 `logs/` 目录。
