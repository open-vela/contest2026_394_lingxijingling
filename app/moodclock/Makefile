############################################################################
#  apps/system/moodclock/Makefile —— NuttX/openVela apps 框架
#
#  注册内置命令：nsh 下输入 `moodclock` 即启动。
#  若你的 openVela apps 树没有 Application.mk（新旧框架差异），
#  改用 Make.defs 里 $(call REGISTER,...) 的方式（见同目录 Make.defs 注释）。
############################################################################

MENUDESC = "MoodClock: T-Display-S3 mood clock (openVela port)"

MODULE = $(CONFIG_SYSTEM_MOODCLOCK)

PROGNAME  = $(CONFIG_SYSTEM_MOODCLOCK_PROGNAME)
PRIORITY  = $(CONFIG_SYSTEM_MOODCLOCK_PRIORITY)
STACKSIZE = $(CONFIG_SYSTEM_MOODCLOCK_STACKSIZE)

MAINSRC = moodclock_main.c
CSRCS   = mc_gpio.c tds3_st7789.c mc_gfx.c mc_net.c

CFLAGS += -std=gnu99

include $(APPDIR)/Application.mk
