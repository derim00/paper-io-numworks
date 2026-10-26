#ifndef EADK_H
#define EADK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint16_t eadk_color_t;

typedef struct {
  uint16_t x;
  uint16_t y;
} eadk_point_t;

typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} eadk_rect_t;

#define EADK_SCREEN_WIDTH 320
#define EADK_SCREEN_HEIGHT 240

typedef uint64_t eadk_keyboard_state_t;
typedef enum {
  eadk_key_left = 0,
  eadk_key_up = 1,
  eadk_key_down = 2,
  eadk_key_right = 3,
  eadk_key_ok = 4,
  eadk_key_back = 5,
  eadk_key_home = 6
} eadk_key_t;

eadk_keyboard_state_t eadk_keyboard_scan(void);
void eadk_display_push_rect_uniform(eadk_rect_t rect, eadk_color_t color);
void eadk_display_draw_string(const char* text, eadk_point_t point,
                              bool large_font, eadk_color_t text_color,
                              eadk_color_t background_color);
void eadk_timing_msleep(uint32_t ms);
uint32_t eadk_random(void);

void eadk_main(void);

#endif
