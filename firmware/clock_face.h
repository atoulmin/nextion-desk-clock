// Analog desk clock face for the 240x240 round ONX2424G013.
// The still face (ticks, date) is pre-drawn into images; three full-screen objects draw
// the background image, glowing ticks, then the hands and animations on top. Only the
// areas that change are redrawn: a hand's sweep, glowing ticks, the comet and bands
// around the press rings. Each angled line costs LVGL a fixed ~0.2 ms however short it
// is, so the frame budget is counted in lines more than in pixels.
//
// Knob animations (colour shows direction: clockwise ember orange, anticlockwise cyan):
//  - Turn: a blazing comet rides the bezel with the dial, eased so a fast spin stays
//    fluid. Its tail stretches with speed, and the ticks it passes flare, stretch
//    inwards and cool off behind it.
//  - Press: a white-hot shockwave and an echo ring blast out from the centre; when they
//    hit the bezel every tick flares, the hands flash and the centre cap pops.
#pragma once
#include "lvgl.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <sys/time.h>

namespace desk_clock {

static const char *const TAG = "clock";

static const uint32_t BG = 0x0A0A0C, HOUR_TICK = 0xE9E5DF, MINUTE_TICK = 0x45454D, HAND = 0xF4F1EC,
                      ACCENT = 0xFF5A36, DATE = 0x8C8C94, WHITE = 0xFFFFFF;
// Animation colours: [clockwise, anticlockwise, press]
static const uint32_t FLARE[3] = {0xFF5A36, 0x1EC8FF, 0xFF5A36};
static const uint32_t FLARE_CORE[3] = {0xFFE0C8, 0xD8F6FF, 0xFFFFFF};

constexpr int CX = 120, CY = 120;
constexpr float RIM_R = 111;       // comet path
constexpr float HEAD_EASE = 0.06;  // s, how closely the comet follows the dial
constexpr float HEAD_HOLD = 0.9, HEAD_FADE = 0.6;  // s, comet stays then fades after the last turn
constexpr float GLOW_FADE = 0.38;  // s, tick glow decay time constant
constexpr float TRAIL_MIN = 22, TRAIL_MAX = 150, TRAIL_PER_SPEED = 4.5;  // degrees, degrees per tick/s
constexpr float RING_TIME = 0.7, ECHO_DELAY = 0.1;  // s
constexpr float RING_START = 14, RING_END = 118;
constexpr float PRESS_TIME = 1.6;  // s, whole press animation incl. bezel cool-down margin
constexpr float POP_TIME = 0.45, HAND_FLASH = 0.5;  // s
constexpr int FAST_MS = 16, IDLE_MS = 33;  // frame period while animating / at rest

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

struct Ring {
  float r = 0, w = 0, opa = 0, heat = 0;
  lv_area_t band[8];
  bool drawn = false;
};

struct State {
  lv_obj_t *bg = nullptr, *dial = nullptr, *hands = nullptr;
  lv_obj_t *text_canvas = nullptr;
  lv_obj_t *work = nullptr;  // hidden canvas used to draw into the stage images
  lv_draw_buf_t *stage[8] = {};
  float bezel = 0;  // press flare level 0..1
  int stage_shown = 0;  // scratch canvas for the date before it's rotated onto the background
  const lv_font_t *date_font = nullptr;
  char date_text[16] = "";
  uint32_t hands_ms = 0;
  lv_timer_t *timer = nullptr;
  int rotation = 0;  // degrees clockwise, to suit how the knob is mounted
  Seconds seconds = SWEEP;
  bool show_date = true, feedback = true;
  bool valid = false;
  float angle[3] = {-999, -999, -999};  // degrees clockwise from 12
  lv_area_t area[3];
  int mday = -1;
  uint32_t last_ms = 0;
  bool fast = false;

  // Ticks: glow 0..1, which colour, and the area each covers when fully stretched
  float glow[60] = {};
  uint8_t tint[60] = {};
  lv_area_t tick_area[60];

  // Comet: positions in ticks (6 degrees), unwrapped
  float head = 0, target = 0, origin = 0, head_level = 0, speed = 0, trail = TRAIL_MIN;
  int dir = 1;
  uint32_t turn_ms = 0;
  lv_area_t comet_area[6];
  int comet_areas = 0;

  // Press
  bool pressing = false, flashed = false;
  uint32_t press_ms = 0;
  Ring ring[2];
  float pop = 0, hand_flash = 0;

  // Frame stats while animating
  bool animating = false;
  uint32_t anim_start = 0, frames = 0, worst = 0;
};
static State S;

static inline void polar(float deg, float r, int32_t &x, int32_t &y) {
  float a = (deg + S.rotation) * (float) M_PI / 180.0f;
  x = CX + (int32_t) lroundf(sinf(a) * r);
  y = CY - (int32_t) lroundf(cosf(a) * r);
}

static void set_area(lv_area_t &a, int32_t x, int32_t y, int r) { a = {x - r, y - r, x + r, y + r}; }

static void invalidate(const lv_area_t &a) {
  lv_area_t c = a;
  lv_obj_invalidate_area(S.hands, &c);
}

static void grow(lv_area_t &a, int32_t x, int32_t y) {
  a.x1 = std::min(a.x1, x);
  a.y1 = std::min(a.y1, y);
  a.x2 = std::max(a.x2, x);
  a.y2 = std::max(a.y2, y);
}

// The ring between two radii as 8 sector boxes instead of the whole screen. Sector edges
// are on multiples of 45 degrees, so within each one x and y are monotonic and the
// corner points bound it.
static void band_areas(float r0, float r1, lv_area_t out[8]) {
  r0 = std::max(0.0f, r0);
  for (int s = 0; s < 8; s++) {
    lv_area_t a{9999, 9999, -9999, -9999};
    for (float deg : {s * 45.0f, s * 45.0f + 45.0f})
      for (float r : {r0, r1}) {
        float rad = deg * (float) M_PI / 180.0f;
        grow(a, CX + (int32_t) lroundf(sinf(rad) * r), CY - (int32_t) lroundf(cosf(rad) * r));
      }
    out[s] = {a.x1 - 2, a.y1 - 2, a.x2 + 2, a.y2 + 2};
  }
}

static void invalidate_band(float r0, float r1) {
  lv_area_t b[8];
  band_areas(r0, r1, b);
  for (auto &a : b)
    invalidate(a);
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int w, lv_color_t color,
                 lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = color;
  d.width = w;
  d.opa = opa;
  d.round_start = d.round_end = 1;
  d.p1.x = x1;
  d.p1.y = y1;
  d.p2.x = x2;
  d.p2.y = y2;
  lv_draw_line(layer, &d);
}

static void dot(lv_layer_t *layer, int32_t x, int32_t y, int r, lv_color_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = color;
  d.bg_opa = opa;
  d.radius = LV_RADIUS_CIRCLE;
  lv_area_t a{x - r, y - r, x + r, y + r};
  lv_draw_rect(layer, &d, &a);
}
static void dot(lv_layer_t *layer, int32_t x, int32_t y, int r, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  dot(layer, x, y, r, lv_color_hex(color), opa);
}

// Arc along the face; deg0..deg1 clockwise from 12 (before rotation), r = centre line
static void arc(lv_layer_t *layer, float deg0, float deg1, float r, int w, lv_color_t color, lv_opa_t opa) {
  if (deg1 < deg0)
    std::swap(deg0, deg1);
  if (deg1 - deg0 < 1)
    return;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = color;
  d.opa = opa;
  d.width = w;
  d.radius = (uint16_t) lroundf(r + w / 2.0f);
  d.center = {CX, CY};
  // LVGL wants 0..360 from 3 o'clock; an end below the start wraps through 0
  int start = (int) lroundf(deg0 + S.rotation - 90), len = (int) lroundf(deg1 - deg0);
  start = ((start % 360) + 360) % 360;
  d.start_angle = start;
  d.end_angle = (start + std::min(len, 359)) % 360;
  d.rounded = 1;
  lv_draw_arc(layer, &d);
}

static lv_color_t mix(uint32_t a, uint32_t b, float t) {  // t = 1 gives a
  return lv_color_mix(lv_color_hex(a), lv_color_hex(b), (uint8_t) (std::clamp(t, 0.0f, 1.0f) * 255));
}

// Tick geometry; a glowing tick stretches inwards
static void tick_geom(int i, float g, int32_t &x1, int32_t &y1, int32_t &x2, int32_t &y2, int &w) {
  float inner;
  if (i % 5 == 0) {
    inner = (i % 15 == 0 ? 97 : 101) - 10 * g;
    w = (i % 15 == 0 ? 5 : 4) + (g > 0.5f);
  } else {
    inner = 110 - 18 * g;
    w = 2 + (g > 0.4f);
  }
  polar(i * 6, inner, x1, y1);
  polar(i * 6, 112, x2, y2);
}

static void update_tick_areas() {
  for (int i = 0; i < 60; i++) {
    int32_t x1, y1, x2, y2;
    int w;
    tick_geom(i, 1, x1, y1, x2, y2, w);
    S.tick_area[i] = {std::min(x1, x2) - w - 2, std::min(y1, y2) - w - 2, std::max(x1, x2) + w + 2,
                      std::max(y1, y2) + w + 2};
  }
}

static void draw_dial(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  for (int i = 0; i < 60; i++) {
    const lv_area_t &a = S.tick_area[i], &clip = layer->_clip_area;
    if (a.x2 < clip.x1 || a.x1 > clip.x2 || a.y2 < clip.y1 || a.y1 > clip.y2)
      continue;
    float g = S.glow[i];
    if (g <= 0)
      continue;
    int32_t x1, y1, x2, y2;
    int w;
    tick_geom(i, g, x1, y1, x2, y2, w);
    uint32_t base = i % 5 == 0 ? HOUR_TICK : MINUTE_TICK;
    if (g <= 0)
      continue;  // plain ticks are in the background image
    // Flare: white-hot core cooling to the tint, then back to the plain tick
    int t = S.tint[i];
    lv_color_t hot = lv_color_mix(lv_color_hex(FLARE_CORE[t]), lv_color_hex(FLARE[t]), (uint8_t) (std::max(0.0f, g * 2 - 1) * 255));
    line(layer, x1, y1, x2, y2, w, lv_color_mix(hot, lv_color_hex(base), (uint8_t) (std::min(1.0f, g * 1.6f) * 255)));
  }
}

static void draw_hand(lv_layer_t *layer, int k) {
  int32_t x1, y1, x2, y2;
  polar(S.angle[k] + 180, HANDS[k].tail, x1, y1);
  polar(S.angle[k], HANDS[k].len, x2, y2);
  lv_color_t c = lv_color_hex(HANDS[k].color);
  if (S.hand_flash > 0)
    c = lv_color_mix(lv_color_hex(k == 2 ? WHITE : ACCENT), c, (uint8_t) (S.hand_flash * 255));
  line(layer, x1, y1, x2, y2, HANDS[k].width, c);
}

static void draw_hands(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);

  // Press shockwave rings, under everything else on this layer
  for (auto &r : S.ring) {
    if (r.opa < 0.01f)
      continue;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = mix(FLARE[2], FLARE_CORE[2], r.heat);
    d.opa = (lv_opa_t) (r.opa * 255);
    d.center = {CX, CY};
    d.radius = (uint16_t) lroundf(r.r);
    d.width = std::max(1, (int) lroundf(r.w));
    d.start_angle = 0;
    d.end_angle = 360;
    lv_draw_arc(layer, &d);
  }

  // Comet: layered tail arcs (long and faint to short and bright), then the head
  if (S.head_level > 0.01f) {
    float hd = S.head * 6, back = -S.dir;
    lv_color_t hot = lv_color_hex(FLARE[S.dir > 0 ? 0 : 1]);
    lv_color_t core = lv_color_hex(FLARE_CORE[S.dir > 0 ? 0 : 1]);
    float lvl = S.head_level;
    arc(layer, hd, hd + back * S.trail, RIM_R, 3, hot, (lv_opa_t) (lvl * 110));
    arc(layer, hd, hd + back * S.trail * 0.35f, RIM_R, 7, hot, (lv_opa_t) (lvl * 170));
    int32_t x, y;
    polar(hd, RIM_R, x, y);
    dot(layer, x, y, 12, hot, (lv_opa_t) (lvl * 90));
    dot(layer, x, y, 5, core, (lv_opa_t) (lvl * 255));
  }

  if (!S.valid)
    return;
  int n = S.seconds != HIDDEN ? 3 : 2;
  for (int k = 0; k < n; k++)
    draw_hand(layer, k);
  uint32_t cap = S.seconds != HIDDEN ? ACCENT : HAND;
  if (S.pop > 0.01f)
    dot(layer, CX, CY, 12 + (int) lroundf(S.pop * 10), ACCENT, (lv_opa_t) (S.pop * 90));
  dot(layer, CX, CY, 6 + (int) lroundf(S.pop * 6), S.pop > 0.01f ? mix(WHITE, cap, S.pop) : lv_color_hex(cap));
  dot(layer, CX, CY, 2, BG);
}

static void hand_area(int k, float deg, lv_area_t &a) {
  int32_t x1, y1, x2, y2;
  polar(deg + 180, HANDS[k].tail, x1, y1);
  polar(deg, HANDS[k].len, x2, y2);
  int pad = HANDS[k].width / 2 + 3;  // round caps and anti-aliasing
  a.x1 = std::min(x1, x2) - pad;
  a.y1 = std::min(y1, y2) - pad;
  a.x2 = std::max(x1, x2) + pad;
  a.y2 = std::max(y1, y2) + pad;
}

// The still parts of the face (dial, ticks, date) are drawn once into images, so a frame
// just copies one instead of redrawing 60 lines and a rotated label. There is one image
// per stage of the press flare (stage 0 is the plain face), so fading the flare out is
// a matter of copying a different image rather than blending two every frame.
constexpr int STAGES = 8;

static void draw_date(lv_layer_t *layer) {
  if (!S.show_date || !S.valid || !S.date_text[0])
    return;
  // Draw the text upright on a small canvas, then rotate it into place
  constexpr int W = 100, H = 22;
  if (!S.text_canvas) {
    S.text_canvas = lv_canvas_create(lv_obj_get_parent(S.bg));
    lv_canvas_set_draw_buf(S.text_canvas, lv_draw_buf_create(W, H, LV_COLOR_FORMAT_ARGB8888, 0));
    lv_obj_add_flag(S.text_canvas, LV_OBJ_FLAG_HIDDEN);
  }
  lv_canvas_fill_bg(S.text_canvas, lv_color_black(), LV_OPA_TRANSP);
  lv_layer_t tl;
  lv_canvas_init_layer(S.text_canvas, &tl);
  lv_draw_label_dsc_t ld;
  lv_draw_label_dsc_init(&ld);
  ld.text = S.date_text;
  ld.font = S.date_font;
  ld.color = lv_color_hex(DATE);
  ld.letter_space = 2;
  ld.align = LV_TEXT_ALIGN_CENTER;
  int th = lv_font_get_line_height(S.date_font);
  lv_area_t ta{0, (H - th) / 2, W - 1, (H - th) / 2 + th - 1};
  lv_draw_label(&tl, &ld, &ta);
  lv_canvas_finish_layer(S.text_canvas, &tl);

  int32_t x, y;
  polar(180, 54, x, y);
  lv_draw_image_dsc_t id;
  lv_draw_image_dsc_init(&id);
  id.src = lv_canvas_get_draw_buf(S.text_canvas);
  id.rotation = S.rotation * 10;
  id.pivot = {W / 2, H / 2};
  lv_area_t ia{x - W / 2, y - H / 2, x - W / 2 + W - 1, y - H / 2 + H - 1};
  lv_draw_image(layer, &id, &ia);
}

static void render_background() {
  if (!S.bg)
    return;
  for (int k = 0; k < STAGES; k++) {
    float g = k / (float) (STAGES - 1);
    lv_canvas_set_draw_buf(S.work, S.stage[k]);
    lv_canvas_fill_bg(S.work, lv_color_hex(BG), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(S.work, &layer);
    for (int i = 0; i < 60; i++) {
      int32_t x1, y1, x2, y2;
      int w;
      tick_geom(i, g, x1, y1, x2, y2, w);
      // Flared: hour marks white-hot, minutes ember orange
      uint32_t base = i % 5 == 0 ? HOUR_TICK : MINUTE_TICK, hot = i % 5 == 0 ? FLARE_CORE[2] : FLARE[2];
      line(&layer, x1, y1, x2, y2, w, k ? mix(hot, base, std::min(1.0f, g * 1.4f)) : lv_color_hex(base));
    }
    draw_date(&layer);
    lv_canvas_finish_layer(S.work, &layer);
  }
  lv_obj_invalidate(S.bg);
}

static void draw_bg(lv_event_t *e) {
  int k = std::clamp((int) lroundf(S.bezel * (STAGES - 1)), 0, STAGES - 1);
  lv_draw_image_dsc_t d;
  lv_draw_image_dsc_init(&d);
  d.src = S.stage[k];
  lv_area_t a{0, 0, 239, 239};
  lv_draw_image(lv_event_get_layer(e), &d, &a);
}

static void move_hand(int k, float deg) {
  invalidate(S.area[k]);
  S.angle[k] = deg;
  hand_area(k, deg, S.area[k]);
  invalidate(S.area[k]);
}

static void redraw_all() {
  if (!S.dial)
    return;
  for (int k = 0; k < 3; k++)
    hand_area(k, S.angle[k], S.area[k]);
  update_tick_areas();
  render_background();
}

static float ease_out(float t, float p) { return 1 - powf(1 - t, p); }

// Areas covered by the comet: its tail split into chunks of at most 40 degrees, plus the head
static int comet_areas(lv_area_t out[6]) {
  float hd = S.head * 6, end = hd - S.dir * S.trail;
  float lo = std::min(hd, end), hi = std::max(hd, end);
  int n = 0;
  for (float a0 = lo; a0 < hi && n < 5; a0 += 40) {
    float a1 = std::min(hi, a0 + 40);
    lv_area_t a{9999, 9999, -9999, -9999};
    for (float d = a0;; d = std::min(a1, d + 10)) {
      for (float r : {RIM_R - 7, RIM_R + 7}) {
        int32_t x, y;
        polar(d, r, x, y);
        grow(a, x, y);
      }
      if (d >= a1)
        break;
    }
    out[n++] = {a.x1 - 2, a.y1 - 2, a.x2 + 2, a.y2 + 2};
  }
  int32_t x, y;
  polar(hd, RIM_R, x, y);
  set_area(out[n++], x, y, 16);
  return n;
}

static void invalidate_hands() {
  for (int k = 0; k < 3; k++)
    invalidate(S.area[k]);
}

// One animation frame; returns true while anything is still moving
static bool animate(uint32_t now, float dt) {
  bool busy = false;

  // Comet follows the dial; its tail stretches with speed
  if (S.head_level > 0) {
    float prev = S.head;
    S.head += (S.target - S.head) * (1 - expf(-dt / HEAD_EASE));
    if (fabsf(S.target - S.head) < 0.01f)
      S.head = S.target;
    if (dt > 0) {
      float inst = fabsf(S.head - prev) / dt;
      S.speed += (inst - S.speed) * (1 - expf(-dt / 0.12f));
    }
    float want = std::clamp(TRAIL_MIN + S.speed * TRAIL_PER_SPEED, TRAIL_MIN, TRAIL_MAX);
    S.trail += (want - S.trail) * (1 - expf(-dt / (want > S.trail ? 0.08f : 0.3f)));
    // Never longer than the path travelled, so the tail doesn't reach back past the start
    S.trail = std::min(S.trail, fabsf(S.head - S.origin) * 6 + 6);
    // Flare every tick the head passed over this frame
    int lo = (int) ceilf(std::min(prev, S.head) - 0.5f), hi = (int) floorf(std::max(prev, S.head) + 0.5f);
    for (int n = lo; n <= hi; n++) {
      int i = ((n % 60) + 60) % 60;
      S.glow[i] = 1;
      S.tint[i] = S.dir > 0 ? 0 : 1;
      invalidate(S.tick_area[i]);
    }
    float age = (now - S.turn_ms) / 1000.0f;
    S.head_level = age < HEAD_HOLD ? 1 : std::max(0.0f, 1 - (age - HEAD_HOLD) / HEAD_FADE);
    for (int k = 0; k < S.comet_areas; k++)
      invalidate(S.comet_area[k]);
    S.comet_areas = S.head_level > 0 ? comet_areas(S.comet_area) : 0;
    for (int k = 0; k < S.comet_areas; k++)
      invalidate(S.comet_area[k]);
    busy = true;
  }

  // Press: shockwave + echo ring, bezel flare, hand flash and cap pop
  if (S.pressing) {
    float t_all = (now - S.press_ms) / 1000.0f;
    for (int k = 0; k < 2; k++) {
      Ring &r = S.ring[k];
      if (r.drawn)
        for (auto &a : r.band)
          invalidate(a);
      float t = (t_all - k * ECHO_DELAY) / RING_TIME;
      r.drawn = t >= 0 && t < 1;
      if (!r.drawn) {
        r.opa = 0;
        continue;
      }
      r.r = RING_START + (RING_END - RING_START) * ease_out(t, k == 0 ? 4 : 3);
      r.w = k == 0 ? 2 + 16 * powf(1 - t, 1.5f) : 3 * (1 - t) + 1;
      r.opa = powf(1 - t, 1.3f) * (k == 0 ? 1.0f : 0.7f);
      r.heat = std::min(1.0f, t * 6);  // white-hot at launch, orange as it spreads
      band_areas(r.r - r.w - 2, r.r + 2, r.band);
      for (auto &a : r.band)
        invalidate(a);
    }
    // The shockwave reaching the ticks flares the whole bezel
    if (!S.flashed && S.ring[0].r >= 92) {
      S.flashed = true;
      S.bezel = 1;
    }
    float p = t_all / POP_TIME;
    // Snap up in the first fifth, then ease back down
    float pop = p >= 1 ? 0 : p < 0.2f ? ease_out(p / 0.2f, 2) : powf(1 - (p - 0.2f) / 0.8f, 2);
    if (pop != S.pop) {
      S.pop = pop;
      lv_area_t cap;
      set_area(cap, CX, CY, 26);
      invalidate(cap);
    }
    float hf = std::max(0.0f, 1 - t_all / HAND_FLASH);
    if (hf != S.hand_flash) {
      S.hand_flash = hf;
      invalidate_hands();
    }
    S.pressing = t_all < PRESS_TIME;
    busy = true;
  }

  // Bezel flare fades out
  if (S.bezel > 0) {
    S.bezel *= expf(-dt / (GLOW_FADE * 1.3f));
    if (S.bezel < 0.02f)
      S.bezel = 0;
    busy = true;
  }
  int stage = std::clamp((int) lroundf(S.bezel * (STAGES - 1)), 0, STAGES - 1);
  if (stage != S.stage_shown) {
    S.stage_shown = stage;
    invalidate_band(80, 117);
  }

  // Tick glow decay. Many glowing ticks: invalidate the bezel band instead of each tick.
  int glowing = 0;
  for (float g : S.glow)
    glowing += g > 0;
  if (glowing) {
    float k = expf(-dt / GLOW_FADE);
    for (int i = 0; i < 60; i++) {
      if (S.glow[i] <= 0)
        continue;
      S.glow[i] *= k;
      if (S.glow[i] < 0.02f)
        S.glow[i] = 0;
      if (glowing <= 12)
        invalidate(S.tick_area[i]);
    }
    if (glowing > 12)
      invalidate_band(80, 117);
    busy = true;
  }
  return busy;
}

static void set_fast(bool fast) {
  if (fast == S.fast)
    return;
  S.fast = fast;
  int ms = fast ? FAST_MS : IDLE_MS;
  lv_timer_set_period(S.timer, ms);
  if (lv_timer_t *refr = lv_display_get_refr_timer(lv_display_get_default()))
    lv_timer_set_period(refr, ms);
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
  uint32_t frame_ms = now - S.last_ms;
  float dt = std::min(0.1f, frame_ms / 1000.0f);
  S.last_ms = now;
  bool was_animating = S.animating;
  bool busy = animate(now, dt);
  S.animating = busy;
  if (busy && !was_animating) {
    S.anim_start = now;
    S.frames = 0;
    S.worst = 0;
  } else if (busy) {
    S.frames++;
    S.worst = std::max(S.worst, frame_ms);
  } else if (was_animating && S.frames) {
    uint32_t span = now - S.anim_start;
    ESP_LOGD(TAG, "animation: %u frames in %u ms (%.0f fps), slowest frame %u ms", (unsigned) S.frames,
             (unsigned) span, S.frames * 1000.0f / std::max<uint32_t>(1, span), (unsigned) S.worst);
  }
  set_fast(busy);

  if (!valid)
    return;
  tm t;
  localtime_r(&tv.tv_sec, &t);
  float sec = t.tm_sec + (S.seconds == SWEEP ? tv.tv_usec / 1e6f : 0.0f);
  float min = t.tm_min + sec / 60.0f;
  float hr = (t.tm_hour % 12) + min / 60.0f;
  float target[3] = {hr * 30, min * 6, sec * 6};
  // Hands at most 30 times a second, even while an animation runs faster
  if (now - S.hands_ms < IDLE_MS - 3)
    return;
  S.hands_ms = now;
  for (int k = 0; k < 3; k++) {
    // Only redraw once the tip has moved about a third of a pixel
    float step = 0.35f / HANDS[k].len * 180.0f / (float) M_PI;
    if (fabsf(target[k] - S.angle[k]) >= step)
      move_hand(k, target[k]);
  }

  if (t.tm_mday != S.mday) {
    static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    S.mday = t.tm_mday;
    snprintf(S.date_text, sizeof(S.date_text), "%s %d", DAYS[t.tm_wday], t.tm_mday);
    render_background();
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
  S.date_font = date_font;
  for (auto &b : S.stage)
    b = lv_draw_buf_create(240, 240, LV_COLOR_FORMAT_RGB565, 0);
  S.work = lv_canvas_create(scr);
  lv_obj_add_flag(S.work, LV_OBJ_FLAG_HIDDEN);
  S.bg = layer(draw_bg);
  S.dial = layer(draw_dial);

  S.hands = layer(draw_hands);
  redraw_all();
  S.last_ms = lv_tick_get();
  S.timer = lv_timer_create(tick, IDLE_MS, nullptr);
  tick(nullptr);
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
  render_background();
}
static void set_feedback(bool on) { S.feedback = on; }

// Knob input
static void turned(int direction) {
  if (!S.feedback || !S.hands)
    return;
  if (S.head_level <= 0) {
    // A fresh turn starts at 12 o'clock
    S.head = S.target = S.origin = 0;
    S.speed = 0;
    S.trail = TRAIL_MIN;
    S.comet_areas = 0;
  }
  S.dir = direction > 0 ? 1 : -1;
  S.head_level = 1;
  S.target += direction;
  S.turn_ms = lv_tick_get();
  set_fast(true);
}
static void pressed() {
  if (!S.feedback || !S.hands)
    return;
  S.pressing = true;
  S.flashed = false;
  S.press_ms = lv_tick_get();
  set_fast(true);
}

}  // namespace desk_clock
