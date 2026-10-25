#pragma once
// Standalone LVGL 7 configuration shared by the compact UI builds.
#include <stdint.h>
typedef int16_t lv_coord_t;
typedef void* lv_anim_user_data_t;
typedef void* lv_group_user_data_t;
typedef void* lv_fs_drv_user_data_t;
typedef void* lv_img_decoder_user_data_t;
typedef void* lv_disp_drv_user_data_t;
typedef void* lv_indev_drv_user_data_t;
typedef void* lv_font_user_data_t;
typedef void* lv_obj_user_data_t;
#define LV_HOR_RES_MAX 135
#define LV_VER_RES_MAX 240
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_SIZE (128U * 1024U)
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_USE_LOG 0
#define LV_USE_USER_DATA 1
#define LV_TICK_CUSTOM 0
#define LV_DISP_DEF_REFR_PERIOD 20
