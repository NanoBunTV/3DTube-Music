#pragma once

extern bool var_connect_test_success;
extern bool var_need_refresh;
extern bool var_allow_send_app_info;
extern bool var_hide_pointer;
extern bool var_night_mode;
extern bool var_disable_pull_to_refresh;
extern bool var_eco_mode;
extern bool var_debug_mode;
extern bool var_history_enabled;
extern bool var_oauth_enabled;
extern int var_autoplay_level;
extern bool var_show_fps;
extern bool var_sponsorblock_enabled;
extern int var_player_response;
extern double var_forward_buffer_ratio;
extern u8 var_wifi_state;
extern u8 var_wifi_signal;
extern u8 var_battery_charge;
extern int var_hours;
extern int var_minutes;
extern int var_seconds;
extern int var_days;
extern int var_months;
extern int var_years;
extern int var_battery_level_raw;
extern float var_afk_time;

#define DPAD_SCROLL_SPEED_MIN 1.0
#define DPAD_SCROLL_SPEED0_DEFAULT 6.0
#define DPAD_SCROLL_SPEED1_DEFAULT 9.0
#define DPAD_SCROLL_SPEED_MAX 15.0
extern double var_dpad_scroll_speed0;
extern double var_dpad_scroll_speed1;
#define DPAD_SCROLL_THRESHOLD_MIN 0.3
#define DPAD_SCROLL_THRESHOLD_DEFAULT 2.0
#define DPAD_SCROLL_THRESHOLD_MAX 5.0
extern double var_dpad_scroll_speed1_threshold; // in seconds
extern double var_dpad_scroll_speed1_threshold_legacy;
extern int var_free_ram;
extern int var_free_linear_ram;
extern int var_lcd_brightness;
extern int var_time_to_turn_off_lcd;
extern int var_screen_off_mode;
extern int var_num_of_app_start;
extern int var_system_region;
extern bool var_is_new3ds;
extern bool var_core2_available;
extern bool var_core3_available;
extern bool var_app_suspended; // i.e. home button preessed


extern double var_scroll_speed;
extern double var_battery_voltage;
extern char var_status[128];
extern std::string var_clipboard;
extern std::string var_connected_ssid;
extern std::string var_lang;
extern std::string var_lang_content;
extern u8 var_model;
extern C2D_Image var_square_image[1];
