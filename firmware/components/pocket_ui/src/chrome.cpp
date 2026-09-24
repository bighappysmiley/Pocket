#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include <cstdio>
#include <string>

namespace pocket {

namespace {

/** Classic Wi‑Fi fan icon (~16×12) — arcs + center dot. */
void draw_wifi_icon(Canvas& c, int x, int y, bool active) {
  const Gray g = active ? Gray::G0 : Gray::G1;
  // Center dot
  c.fill_rect(x + 6, y + 9, 3, 3, g);
  // Inner arc (small)
  c.set_pixel(x + 4, y + 7, g);
  c.set_pixel(x + 5, y + 6, g);
  c.set_pixel(x + 6, y + 6, g);
  c.set_pixel(x + 7, y + 6, g);
  c.set_pixel(x + 8, y + 6, g);
  c.set_pixel(x + 9, y + 7, g);
  // Mid arc
  c.set_pixel(x + 2, y + 5, g);
  c.set_pixel(x + 3, y + 4, g);
  c.set_pixel(x + 4, y + 3, g);
  c.set_pixel(x + 5, y + 3, g);
  c.set_pixel(x + 6, y + 3, g);
  c.set_pixel(x + 7, y + 3, g);
  c.set_pixel(x + 8, y + 3, g);
  c.set_pixel(x + 9, y + 3, g);
  c.set_pixel(x + 10, y + 4, g);
  c.set_pixel(x + 11, y + 5, g);
  // Outer arc
  c.set_pixel(x + 0, y + 3, g);
  c.set_pixel(x + 1, y + 2, g);
  c.set_pixel(x + 2, y + 1, g);
  c.set_pixel(x + 3, y + 0, g);
  c.set_pixel(x + 4, y + 0, g);
  c.set_pixel(x + 5, y + 0, g);
  c.set_pixel(x + 6, y + 0, g);
  c.set_pixel(x + 7, y + 0, g);
  c.set_pixel(x + 8, y + 0, g);
  c.set_pixel(x + 9, y + 0, g);
  c.set_pixel(x + 10, y + 0, g);
  c.set_pixel(x + 11, y + 1, g);
  c.set_pixel(x + 12, y + 2, g);
  c.set_pixel(x + 13, y + 3, g);
  if (!active) {
    // Slash for offline
    c.line(x + 1, y + 11, x + 12, y + 1, Gray::G1);
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
  c.draw_text(kSideMargin, 8, timebuf, Canvas::TextRole::StatusBar, Gray::G0);

  // Right: wifi icon + battery (real values from platform)
  int rx = kCanvasW - kSideMargin;
  // battery body
  rx -= 22;
  const int pct = battery_pct < 0 ? 0 : (battery_pct > 100 ? 100 : battery_pct);
  c.stroke_rect(rx, 8, 18, 12, Gray::G0);
  c.fill_rect(rx + 18, 11, 2, 6, Gray::G0);
  int fill_w = static_cast<int>(14 * (pct / 100.0));
  if (fill_w > 0) c.fill_rect(rx + 2, 10, fill_w, 8, pct <= 15 ? Gray::G1 : Gray::G0);
  if (cfg.show_batt_pct) {
    char pbuf[8];
    std::snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    const int tw = c.text_width(pbuf, Canvas::TextRole::StatusBar);
    c.draw_text(rx - tw - 6, 8, pbuf, Canvas::TextRole::StatusBar, Gray::G1);
  }
  // wifi icon left of battery
  rx -= 20;
  draw_wifi_icon(c, rx, 8, wifi_ok);
}

}  // namespace pocket
