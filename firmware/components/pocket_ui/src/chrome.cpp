#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include <cstdio>
#include <string>

namespace pocket {

namespace {

/** Draw a filled circle (nodule). */
void fill_disk(Canvas& c, int cx, int cy, int r, Gray g) {
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      if (dx * dx + dy * dy <= r * r) c.set_pixel(cx + dx, cy + dy, g);
    }
  }
}

/**
 * Wi‑Fi fan icon sized for StatusBar type (~22px tall).
 * Three concentric arcs + center disk; slash when offline.
 */
void draw_wifi_icon(Canvas& c, int x, int y, bool active) {
  const Gray g = active ? Gray::G0 : Gray::G1;
  const int cx = x + 11;
  const int cy = y + 18;

  fill_disk(c, cx, cy, 2, g);

  auto thick_arc = [&](int r_outer, int r_inner) {
    for (int yy = y; yy <= cy; ++yy) {
      for (int xx = x; xx <= x + 22; ++xx) {
        const int dx = xx - cx;
        const int dy = yy - cy;
        if (dy > 0) continue;  // upper half only
        const int d2 = dx * dx + dy * dy;
        if (d2 <= r_outer * r_outer && d2 >= r_inner * r_inner) {
          c.set_pixel(xx, yy, g);
        }
      }
    }
  };

  thick_arc(7, 5);
  thick_arc(13, 11);
  thick_arc(19, 16);

  if (!active) {
    c.line(x + 2, y + 20, x + 20, y + 2, Gray::G1);
    c.line(x + 3, y + 20, x + 21, y + 2, Gray::G1);
  }
}

void draw_battery_icon(Canvas& c, int x, int y, int pct) {
  constexpr int kW = 28;
  constexpr int kH = 16;
  c.stroke_rect(x, y, kW, kH, Gray::G0);
  c.fill_rect(x + kW, y + 4, 3, 8, Gray::G0);

  const int inner = kW - 4;
  int fill_w = static_cast<int>(inner * (pct / 100.0));
  if (fill_w > 0) {
    c.fill_rect(x + 2, y + 2, fill_w, kH - 4, pct <= 15 ? Gray::G1 : Gray::G0);
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
