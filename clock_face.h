// Analog desk clock face for the 240x240 round ONX2424G013.
// Drawn with LVGL draw calls on two full-screen objects: the dial (ticks) underneath,
// the hands on top. Only the area a hand sweeps through is redrawn.
#pragma once
#include "lvgl.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <sys/time.h>

namespace desk_clock {

static const uint32_t BG = 0x0A0A0C, HOUR_TICK = 0xE9E5DF, MINUTE_TICK = 0x45454D, HAND = 0xF4F1EC,
                      ACCENT = 0xFF5A36, DATE = 0x8C8C94;

constexpr int CX = 120, CY = 120;

struct Hand {
  float len, tail;
  int width;
  uint32_t color;
};
static const Hand HANDS[3] = {
    {56, 12, 8, HAND},     // hour
    {92, 14, 5, HAND},     // minute
    {104, 24, 2, ACCENT},  // second
};

enum Seconds : uint8_t { SWEEP, TICK, HIDDEN };

struct State {
  lv_obj_t *dial = nullptr, *hands = nullptr, *date = nullptr;
  int rotation = 0;  // degrees clockwise, to suit how the knob is mounted
  Seconds seconds = SWEEP;
  bool show_date = true, feedback = true;
  bool valid = false;
  float angle[3] = {-999, -999, -999};  // degrees clockwise from 12
  lv_area_t area[3];
  int mday = -1;
  // Knob feedback: a dot on the rim that steps with the dial and fades, the cap flashes on press
  int fb_pos = 0;
  float fb_level = 0, press_level = 0;
  uint32_t fb_ms = 0, press_ms = 0;
  lv_area_t fb_area, cap_area;
};
static State S;

static inline void polar(float deg, float r, int32_t &x, int32_t &y) {
  float a = (deg + S.rotation) * (float) M_PI / 180.0f;
  x = CX + (int32_t) lroundf(sinf(a) * r);
  y = CY - (int32_t) lroundf(cosf(a) * r);
}

static void hand_area(int k, float deg, lv_area_t &a) {
  int32_t x1, y1, x2, y2;
  polar(deg + 180, HANDS[k].tail, x1, y1);
  polar(deg, HANDS[k].len, x2, y2);
  int pad = HANDS[k].width / 2 + 4;  // round caps, anti-aliasing and the shadow
  a.x1 = std::min(x1, x2) - pad;
  a.y1 = std::min(y1, y2) - pad;
  a.x2 = std::max(x1, x2) + pad;
  a.y2 = std::max(y1, y2) + pad;
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int w, uint32_t color,
                 lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(color);
  d.width = w;
  d.opa = opa;
  d.round_start = d.round_end = 1;
  d.p1.x = x1;
  d.p1.y = y1;
  d.p2.x = x2;
  d.p2.y = y2;
  lv_draw_line(layer, &d);
}

static void dot(lv_layer_t *layer, int32_t x, int32_t y, int r, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(color);
  d.bg_opa = opa;
  d.radius = LV_RADIUS_CIRCLE;
  lv_area_t a{x - r, y - r, x + r, y + r};
  lv_draw_rect(layer, &d, &a);
}

static void draw_dial(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  for (int i = 0; i < 60; i++) {
    int32_t x1, y1, x2, y2;
    if (i % 5 == 0) {
      polar(i * 6, i % 15 == 0 ? 97 : 101, x1, y1);
      polar(i * 6, 112, x2, y2);
      line(layer, x1, y1, x2, y2, i % 15 == 0 ? 5 : 4, HOUR_TICK);
    } else {
      polar(i * 6, 110, x1, y1);
      polar(i * 6, 112, x2, y2);
      line(layer, x1, y1, x2, y2, 2, MINUTE_TICK);
    }
  }
}

static void draw_hand(lv_layer_t *layer, int k, bool shadow) {
  int32_t x1, y1, x2, y2;
  polar(S.angle[k] + 180, HANDS[k].tail, x1, y1);
  polar(S.angle[k], HANDS[k].len, x2, y2);
  // Soft shadow down and to the right gives the hands a little lift off the dial
  if (shadow)
    line(layer, x1 + 1, y1 + 2, x2 + 1, y2 + 2, HANDS[k].width + 2, 0x000000, LV_OPA_50);
  else
    line(layer, x1, y1, x2, y2, HANDS[k].width, HANDS[k].color);
}

static void draw_hands(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  if (S.fb_level > 0.01f) {
    int32_t x, y;
    polar(S.fb_pos * 6, 111, x, y);
    dot(layer, x, y, 4, ACCENT, (lv_opa_t) (S.fb_level * 255));
  }
  if (!S.valid)
    return;
  int n = S.seconds != HIDDEN ? 3 : 2;
  // All shadows first so none of them falls on another hand
  for (int k = 0; k < n; k++)
    draw_hand(layer, k, true);
  for (int k = 0; k < n; k++)
    draw_hand(layer, k, false);
  uint32_t cap = S.seconds != HIDDEN ? ACCENT : HAND;
  dot(layer, CX, CY, 6 + (int) lroundf(S.press_level * 3), cap);
  dot(layer, CX, CY, 2, BG);
}

static void place_date() {
  if (!S.date)
    return;
  int32_t x, y;
  polar(180, 54, x, y);
  lv_obj_align(S.date, LV_ALIGN_CENTER, x - CX, y - CY);
  lv_obj_set_style_transform_rotation(S.date, S.rotation * 10, 0);
  if (S.show_date && S.valid)
    lv_obj_remove_flag(S.date, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(S.date, LV_OBJ_FLAG_HIDDEN);
}

static void move_hand(int k, float deg) {
  lv_obj_invalidate_area(S.hands, &S.area[k]);
  S.angle[k] = deg;
  hand_area(k, deg, S.area[k]);
  lv_obj_invalidate_area(S.hands, &S.area[k]);
}

static void redraw_all() {
  if (!S.dial)
    return;
  for (int k = 0; k < 3; k++)
    hand_area(k, S.angle[k], S.area[k]);
  place_date();
  lv_obj_invalidate(S.dial);
}

static void set_area(lv_area_t &a, int32_t x, int32_t y, int r) {
  a = {x - r, y - r, x + r, y + r};
}

static void tick(lv_timer_t *) {
  timeval tv;
  gettimeofday(&tv, nullptr);
  bool valid = tv.tv_sec > 1600000000;  // clock set from Home Assistant / NTP
  if (valid != S.valid) {
    S.valid = valid;
    redraw_all();
  }
  uint32_t now = lv_tick_get();

  // Feedback fades
  if (S.fb_level > 0) {
    float age = (now - S.fb_ms) / 1000.0f;
    float lvl = age < 0.8f ? 1.0f : std::max(0.0f, 1.0f - (age - 0.8f) / 0.5f);
    if (lvl != S.fb_level) {
      S.fb_level = lvl;
      lv_obj_invalidate_area(S.hands, &S.fb_area);
    }
  }
  if (S.press_level > 0) {
    float lvl = std::max(0.0f, 1.0f - (now - S.press_ms) / 250.0f);
    if (lvl != S.press_level) {
      S.press_level = lvl;
      lv_obj_invalidate_area(S.hands, &S.cap_area);
    }
  }

  if (!valid)
    return;
  tm t;
  localtime_r(&tv.tv_sec, &t);
  float sec = t.tm_sec + (S.seconds == SWEEP ? tv.tv_usec / 1e6f : 0.0f);
  float min = t.tm_min + sec / 60.0f;
  float hr = (t.tm_hour % 12) + min / 60.0f;
  float target[3] = {hr * 30, min * 6, sec * 6};
  for (int k = 0; k < 3; k++) {
    // Only redraw once the tip has moved about a third of a pixel
    float step = 0.35f / HANDS[k].len * 180.0f / (float) M_PI;
    if (fabsf(target[k] - S.angle[k]) >= step)
      move_hand(k, target[k]);
  }

  if (t.tm_mday != S.mday && S.date) {
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    S.mday = t.tm_mday;
    lv_label_set_text_fmt(S.date, "%s %d", DAYS[t.tm_wday], t.tm_mday);
  }
}

static void build(lv_obj_t *scr, const lv_font_t *date_font) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  auto layer = [&](lv_event_cb_t cb) {
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 240, 240);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_DRAW_MAIN, nullptr);
    return o;
  };
  S.dial = layer(draw_dial);

  S.date = lv_label_create(scr);
  lv_obj_set_style_text_font(S.date, date_font, 0);
  lv_obj_set_style_text_color(S.date, lv_color_hex(DATE), 0);
  lv_obj_set_style_text_letter_space(S.date, 2, 0);
  lv_label_set_text(S.date, "");
  lv_obj_set_style_transform_pivot_x(S.date, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(S.date, LV_PCT(50), 0);

  S.hands = layer(draw_hands);
  set_area(S.cap_area, CX, CY, 12);
  redraw_all();
  tick(nullptr);
  lv_timer_create(tick, 33, nullptr);
}

// Settings from Home Assistant (may arrive before build)
static void set_rotation(int deg) {
  S.rotation = deg;
  redraw_all();
}
static void set_seconds(int mode) {
  S.seconds = (Seconds) mode;
  if (S.hands)
    lv_obj_invalidate(S.hands);
}
static void set_show_date(bool on) {
  S.show_date = on;
  place_date();
}
static void set_feedback(bool on) { S.feedback = on; }

// Knob feedback
static void turned(int direction) {
  if (!S.feedback || !S.hands)
    return;
  if (S.fb_level > 0)
    lv_obj_invalidate_area(S.hands, &S.fb_area);
  else
    S.fb_pos = 0;  // each new turn starts from 12 o'clock
  S.fb_pos = ((S.fb_pos + direction) % 60 + 60) % 60;
  S.fb_level = 1;
  S.fb_ms = lv_tick_get();
  int32_t x, y;
  polar(S.fb_pos * 6, 111, x, y);
  set_area(S.fb_area, x, y, 7);
  lv_obj_invalidate_area(S.hands, &S.fb_area);
}
static void pressed() {
  if (!S.feedback || !S.hands)
    return;
  S.press_level = 1;
  S.press_ms = lv_tick_get();
  lv_obj_invalidate_area(S.hands, &S.cap_area);
}

}  // namespace desk_clock
