// First-run screens shown over the clock face:
//  - WIFI: no Wi-Fi yet, the setup hotspot is up. A QR code joins it; the phone then
//    opens the captive portal page to pick the home network.
//  - HOME_ASSISTANT: on Wi-Fi but Home Assistant hasn't added the clock yet (no API key
//    installed). Says where to find it and shows the IP address.
// Once Home Assistant installs its key the screens go away for good, with the press
// shockwave as a little "connected" flourish.
#pragma once
#include "lvgl.h"
#include "clock_face.h"
#include <cstring>
#include <string>

namespace desk_clock_setup {

enum Mode : uint8_t { NONE, WIFI, HOME_ASSISTANT };

struct Ui {
  lv_obj_t *root = nullptr, *wifi = nullptr, *ha = nullptr, *ip = nullptr;
  Mode mode = NONE;
  std::string ip_text;
};
static Ui U;

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s, int y, int spacing = 0) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_letter_space(l, spacing, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, s);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  return l;
}

static lv_obj_t *panel(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, 240, 240);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

static void set_rotation(int deg) {
  if (U.root)
    lv_obj_set_style_transform_rotation(U.root, deg * 10, 0);
}

static void build(const lv_font_t *small, const lv_font_t *title, const char *ssid, int rotation) {
  using namespace desk_clock;
  U.root = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(U.root);
  lv_obj_set_size(U.root, 240, 240);
  lv_obj_set_style_bg_color(U.root, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(U.root, LV_OPA_COVER, 0);
  lv_obj_set_style_transform_pivot_x(U.root, 120, 0);
  lv_obj_set_style_transform_pivot_y(U.root, 120, 0);
  lv_obj_add_flag(U.root, LV_OBJ_FLAG_HIDDEN);
  set_rotation(rotation);

  // A thin accent ring so the setup screens still feel like the clock
  lv_obj_t *ring = lv_arc_create(U.root);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, 228, 228);
  lv_obj_center(ring);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_angles(ring, 0, 0);
  lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, lv_color_hex(ACCENT), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(ring, LV_OPA_60, LV_PART_MAIN);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

  // Wi-Fi setup: QR code that joins the hotspot
  U.wifi = panel(U.root);
  text(U.wifi, small, ACCENT, "WI-FI SETUP", -84, 2);
  lv_obj_t *qr = lv_qrcode_create(U.wifi);
  lv_qrcode_set_size(qr, 96);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  std::string data = std::string("WIFI:T:nopass;S:") + ssid + ";;";
  lv_qrcode_update(qr, data.c_str(), data.size());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 5, 0);
  lv_obj_align(qr, LV_ALIGN_CENTER, 0, -16);
  text(U.wifi, small, DATE, "Scan, or join Wi-Fi", 50);
  text(U.wifi, title, HAND, ssid, 72);

  // Home Assistant: where to find it
  U.ha = panel(U.root);
  text(U.ha, small, ACCENT, "WI-FI CONNECTED", -74, 2);
  text(U.ha, title, HAND, "Add me in\nHome Assistant", -30);
  text(U.ha, small, DATE, "Settings \xE2\x80\xBA Devices & services\nlook under Discovered", 30);
  U.ip = text(U.ha, small, DATE, "", 80);
}

// Called about once a second with the current state
static void show(Mode mode, const char *ip) {
  if (!U.root)
    return;
  if (U.ip_text != ip) {
    U.ip_text = ip;
    lv_label_set_text(U.ip, ip);
  }
  if (mode == U.mode)
    return;
  Mode was = U.mode;
  U.mode = mode;
  if (mode == NONE)
    lv_obj_add_flag(U.root, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(U.root, LV_OBJ_FLAG_HIDDEN);
  if (mode == WIFI)
    lv_obj_remove_flag(U.wifi, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(U.wifi, LV_OBJ_FLAG_HIDDEN);
  if (mode == HOME_ASSISTANT)
    lv_obj_remove_flag(U.ha, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(U.ha, LV_OBJ_FLAG_HIDDEN);
  // Just added to Home Assistant: celebrate with the press shockwave
  if (mode == NONE && was == HOME_ASSISTANT)
    desk_clock::pressed();
}

}  // namespace desk_clock_setup
