#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace pocket {

namespace {

/**
 * Wi‑Fi status glyph — three rising bars with even optical weight.
 * Offline slash stays bold enough to read on e-ink.
 */
void draw_wifi_icon(Canvas& c, int x, int y, bool active) {
  const Gray g_on = Gray::G0;
  const Gray g_off = Gray::G2;
  constexpr int kBarW = 5;
  constexpr int kGap = 5;
  constexpr int kBaseY = 26;
  const int heights[3] = {10, 17, 24};
  for (int i = 0; i < 3; ++i) {
    const int bx = x + 3 + i * (kBarW + kGap);
    const int bh = heights[i];
    c.fill_round_rect(bx, y + kBaseY - bh, kBarW, bh, 2, active ? g_on : g_off);
  }
  if (!active) {
    // Slightly inset slash so it clears the shortest bar cleanly.
    c.line(x + 1, y + 26, x + 27, y + 2, Gray::G0);
    c.line(x + 2, y + 26, x + 28, y + 2, Gray::G0);
  }
}

/**
 * Battery gauge — rounded body, four segments, clear low-battery wash.
 */
void draw_battery_icon(Canvas& c, int x, int y, int pct) {
  constexpr int kW = 34;
  constexpr int kH = 18;
  constexpr int kSegs = 4;
  c.stroke_round_rect(x, y, kW, kH, 4, Gray::G0, 2);
  c.fill_round_rect(x + kW, y + 5, 4, 8, 2, Gray::G0);

  const int pad = 3;
  const int inner_w = kW - 2 * pad;
  const int seg_gap = 2;
  const int seg_w = (inner_w - (kSegs - 1) * seg_gap) / kSegs;
  const int lit = std::max(pct > 0 ? 1 : 0, (pct * kSegs + 50) / 100);
  const Gray fill_g = pct <= 15 ? Gray::G1 : Gray::G0;
  for (int i = 0; i < kSegs; ++i) {
    if (i >= lit) continue;
    const int sx = x + pad + i * (seg_w + seg_gap);
    c.fill_round_rect(sx, y + pad, seg_w, kH - 2 * pad, 1, fill_g);
  }
}

}  // namespace

void draw_status_bar_impl(Canvas& c, const DeviceConfig& cfg, int hour, int minute, bool wifi_ok,
                          int battery_pct, bool time_ok) {
  c.fill_rect(0, 0, kCanvasW, kStatusBarH, Gray::G3);
  // Hairline rule — G1 reads cleaner on partial refreshes than soft G2.
  c.hline(0, kStatusBarH - 1, kCanvasW, Gray::G1);

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

  constexpr int kBattW = 34;
  constexpr int kBattH = 18;
  constexpr int kWifiW = 28;
  constexpr int kWifiH = 28;
  const int batt_y = (kStatusBarH - kBattH) / 2;
  const int wifi_y = (kStatusBarH - kWifiH) / 2;

  int rx = kCanvasW - kSideMargin;
  const int pct = battery_pct < 0 ? 0 : (battery_pct > 100 ? 100 : battery_pct);

  rx -= (kBattW + 5);
  draw_battery_icon(c, rx, batt_y, pct);

  if (cfg.show_batt_pct) {
    char pbuf[8];
    std::snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    const int tw = c.text_width(pbuf, Canvas::TextRole::StatusBar);
    c.draw_text(rx - tw - 8, text_y, pbuf, Canvas::TextRole::StatusBar, Gray::G1);
    rx -= (tw + 8);
  }

  rx -= (kWifiW + 14);
  draw_wifi_icon(c, rx, wifi_y, wifi_ok);
}

}  // namespace pocket
