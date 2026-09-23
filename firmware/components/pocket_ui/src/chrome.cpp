#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include <cstdio>
#include <string>

namespace pocket {

void draw_status_bar_impl(Canvas& c, const DeviceConfig& cfg, int hour, int minute, bool wifi_ok,
                          int battery_pct) {
  c.fill_rect(0, 0, kCanvasW, kStatusBarH, Gray::G3);
  c.hline(0, kStatusBarH - 1, kCanvasW, Gray::G2);

  char timebuf[8];
  if (cfg.tz_id.empty() && !cfg.onboarding_complete) {
    std::snprintf(timebuf, sizeof(timebuf), "--:--");
  } else if (cfg.time_format == 24) {
    std::snprintf(timebuf, sizeof(timebuf), "%02d:%02d", hour, minute);
  } else {
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(timebuf, sizeof(timebuf), "%d:%02d", h12, minute);
  }
  c.draw_text(kSideMargin, 8, timebuf, Canvas::TextRole::StatusBar, Gray::G0);

  // Right: wifi + battery glyphs (abstract)
  int rx = kCanvasW - kSideMargin;
  // battery body
  rx -= 22;
  c.stroke_rect(rx, 8, 18, 12, Gray::G0);
  c.fill_rect(rx + 18, 11, 2, 6, Gray::G0);
  int fill_w = static_cast<int>(14 * (battery_pct / 100.0));
  if (fill_w > 0) c.fill_rect(rx + 2, 10, fill_w, 8, battery_pct <= 15 ? Gray::G1 : Gray::G0);
  if (cfg.show_batt_pct) {
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d%%", battery_pct);
    c.draw_text(rx - 28, 8, pct, Canvas::TextRole::StatusBar, Gray::G1);
  }
  // wifi
  rx -= 20;
  if (wifi_ok) {
    c.draw_text(rx, 8, "W", Canvas::TextRole::StatusBar, Gray::G0);
  } else {
    c.draw_text(rx, 8, "w", Canvas::TextRole::StatusBar, Gray::G1);
  }
}

}  // namespace pocket