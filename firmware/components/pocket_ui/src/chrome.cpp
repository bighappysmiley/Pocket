#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace pocket {

namespace {

/**
 * Wi‑Fi status glyph sized for StatusBar type (~28px tall): three ascending signal
 * bars, solid blocks for crisp e-ink rendering (no thin-arc anti-aliasing). Slash
 * through the bars when offline.
 */
void draw_wifi_icon(Canvas& c, int x, int y, bool active) {
  const Gray g_on = Gray::G0;
  const Gray g_off = Gray::G2;
  constexpr int kBarW = 5;
  constexpr int kGap = 3;
  constexpr int kBaseY = 20;  // bar baseline within the 22px-tall icon box
  const int heights[3] = {7, 13, 19};
  for (int i = 0; i < 3; ++i) {
    const int bx = x + i * (kBarW + kGap);
    const int bh = heights[i];
    c.fill_rect(bx, y + kBaseY - bh, kBarW, bh, active ? g_on : g_off);
  }
  if (!active) {
    c.line(x - 1, y + 21, x + 21, y - 1, Gray::G0);
    c.line(x, y + 21, x + 22, y - 1, Gray::G0);
  }
}

/**
 * Battery gauge sized for StatusBar type: rounded body + terminal nub, filled with
 * distinct segments (not a continuous sweep) so charge level reads clearly at a
 * glance on e-ink.
 */
void draw_battery_icon(Canvas& c, int x, int y, int pct) {
  constexpr int kW = 28;
  constexpr int kH = 16;
  constexpr int kSegs = 4;
  c.stroke_round_rect(x, y, kW, kH, 3, Gray::G0, 2);
  c.fill_rect(x + kW, y + 4, 3, 8, Gray::G0);

  const int pad = 3;
  const int inner_w = kW - 2 * pad;
  const int seg_gap = 2;
  const int seg_w = (inner_w - (kSegs - 1) * seg_gap) / kSegs;
  const int lit = std::max(pct > 0 ? 1 : 0, (pct * kSegs + 50) / 100);
  const Gray fill_g = pct <= 15 ? Gray::G1 : Gray::G0;
  for (int i = 0; i < kSegs; ++i) {
    if (i >= lit) continue;
    const int sx = x + pad + i * (seg_w + seg_gap);
    c.fill_rect(sx, y + pad, seg_w, kH - 2 * pad, fill_g);
  }
}

}  // namespace

void draw_status_bar_impl(Canvas& c, const DeviceConfig& cfg, int hour, int minute, bool wifi_ok,
                          int battery_pct, bool time_ok) {
  c.fill_rect(0, 0, kCanvasW, kStatusBarH, Gray::G3);
  c.hline(0, kStatusBarH - 1, kCanvasW, Gray::G2);

  char timebuf[16];
  if (!time_ok || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    std::snprintf(timebuf, sizeof(timebuf), "--:--");
  } else if (cfg.time_format == 24) {
    std::snprintf(timebuf, sizeof(timebuf), "%02d:%02d", hour, minute);
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(timebuf, sizeof(timebuf), "%d:%02d", h12, minute);
  }

  const int text_h = c.text_height(Canvas::TextRole::StatusBar);
  const int text_y = (kStatusBarH - text_h) / 2;
  c.draw_text(kSideMargin, text_y, timebuf, Canvas::TextRole::StatusBar, Gray::G0);

  constexpr int kBattW = 28;
  constexpr int kBattH = 16;
  constexpr int kWifiW = 22;
  constexpr int kWifiH = 22;
  const int batt_y = (kStatusBarH - kBattH) / 2;
  const int wifi_y = (kStatusBarH - kWifiH) / 2;

  int rx = kCanvasW - kSideMargin;
  const int pct = battery_pct < 0 ? 0 : (battery_pct > 100 ? 100 : battery_pct);

  rx -= (kBattW + 4);
  draw_battery_icon(c, rx, batt_y, pct);

  if (cfg.show_batt_pct) {
    char pbuf[8];
    std::snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    const int tw = c.text_width(pbuf, Canvas::TextRole::StatusBar);
    c.draw_text(rx - tw - 8, text_y, pbuf, Canvas::TextRole::StatusBar, Gray::G1);
    rx -= (tw + 8);
  }

  rx -= (kWifiW + 12);
  draw_wifi_icon(c, rx, wifi_y, wifi_ok);
}

}  // namespace pocket
