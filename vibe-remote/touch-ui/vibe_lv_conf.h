#pragma once
// Both builds start from the pinned LilyGO LVGL configuration.
#include <lv_conf.h>
#undef LV_FONT_MONTSERRAT_12
#define LV_FONT_MONTSERRAT_12 1
#undef LV_INDEV_DEF_LONG_PRESS_TIME
#define LV_INDEV_DEF_LONG_PRESS_TIME 900
#undef LV_INDEV_DEF_READ_PERIOD
#define LV_INDEV_DEF_READ_PERIOD 10
