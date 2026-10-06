#include "pocket/app.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "Reading", "",     "",      "",     "",        "",      "",         "",
};

#include "home_glyphs.inc"

/** Map HomeApp → packed 1-bit bitmap index (Notes..Reading). */
int home_glyph_index(HomeApp app) {
  switch (app) {
    case HomeApp::Notes:
      return 0;
    case HomeApp::Ledger:
      return 1;
    case HomeApp::Clock:
      return 2;
    case HomeApp::Pass:
      return 3;
    case HomeApp::Weather:
      return 4;
    case HomeApp::Music:
      return 5;
    case HomeApp::Settings:
      return 6;
    case HomeApp::Update:
      return 7;
    case HomeApp::Reading:
      return 8;
    default:
      return -1;
  }
}

/** True if packed bit at (sx,sy) is ink. */
bool glyph_ink(const uint8_t* bits, int sx, int sy) {
  if (sx < 0 || sy < 0 || sx >= kHomeGlyphSize || sy >= kHomeGlyphSize) return false;
  return (bits[sy * kHomeGlyphRowBytes + (sx >> 3)] &
          static_cast<uint8_t>(1u << (7 - (sx & 7)))) != 0;
}

/**
 * Home app glyphs (v66): packed 1-bit bitmaps from Apple-quality GenerateImage
 * SF Symbol–style filled marks (tools/pack_home_glyphs.py).
 * Majority-vote scale keeps thin creases / ticks from vanishing.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int idx = home_glyph_index(app);
  if (idx < 0) return;
  const uint8_t* bits = kHomeGlyphBits[idx];
  // ~68% of tile — rim air like iOS glyphs inside the rounded squircle.
  const int dest = std::max(24, size * 68 / 100);
  const int src = kHomeGlyphSize;
  const int x0 = cx - dest / 2;
  const int y0 = cy - dest / 2;
  for (int dy = 0; dy < dest; ++dy) {
    const int sy0 = dy * src / dest;
    const int sy1 = std::max(sy0 + 1, (dy + 1) * src / dest);
    for (int dx = 0; dx < dest; ++dx) {
      const int sx0 = dx * src / dest;
      const int sx1 = std::max(sx0 + 1, (dx + 1) * src / dest);
      int ink = 0;
      int tot = 0;
      for (int sy = sy0; sy < sy1; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
          ++tot;
          if (glyph_ink(bits, sx, sy)) ++ink;
        }
      }
      // Strict majority — keeps cutouts (Notes rules, gear hub) from filling shut.
      if (ink * 2 > tot) {
        c.set_pixel(x0 + dx, y0 + dy, g);
      }
    }
  }
}

/** Display order: real content apps, then Reading, then the always-on Settings/Update tiles —
 * keeps the utility tiles last regardless of HomeApp's underlying (bitmask-compatible) index. */
constexpr int kHomeOrder[kHomeGridSlots] = {0, 1, 2, 3, 4, 5, 8, 6, 7, 9, 10, 11, 12, 13, 14, 15};

HomeApp slot_app(int i) { return static_cast<HomeApp>(kHomeOrder[i]); }

/** iPhone-style Home: 3 columns of square rounded icons with labels underneath.
 * v60+: content-sized rows (no stretch-to-fill air between label and next icon). */
constexpr int kHomeCols = 3;
constexpr int kHomeGapX = 16;
constexpr int kHomeGapY = 8;
/** WordMark (49) + hairline rule + air before the grid — must clear the brand. */
constexpr int kHomeBrandBand = 56;
constexpr int kHomeBottomPad = 16;
/** Cap so icons stay calm — slight size from v59 OK. */
constexpr int kHomeIconMax = 100;
constexpr int kHomeIconMin = 68;

/** Cell metrics for an iOS-style grid: square icon + label band under it. */
void home_grid_metrics(int n_focus, int label_band_h, int* out_grid_top, int* out_cell_w,
                       int* out_cell_h, int* out_icon) {
  const int grid_top_min = kContentTop + kHomeBrandBand;
  const int rows = std::max(1, (n_focus + kHomeCols - 1) / kHomeCols);
  const int cell_w = (kCanvasW - 2 * kSideMargin - (kHomeCols - 1) * kHomeGapX) / kHomeCols;
  // Size icon from column width — do NOT stretch cell_h to fill leftover canvas height
  // (that was the loose vertical rhythm on v59).
  int icon = std::min(cell_w - 4, kHomeIconMax);
  icon = std::max(kHomeIconMin, icon);
  const int cell_h = icon + label_band_h + 4;
  const int grid_h = rows * cell_h + (rows - 1) * kHomeGapY;
  const int avail = kCanvasH - grid_top_min - kHomeBottomPad;
  // Pack from the top; only a little leftover air above the grid (never between rows).
  int grid_top = grid_top_min;
  if (avail > grid_h) {
    grid_top = grid_top_min + std::min(20, (avail - grid_h) / 6);
  }
  *out_grid_top = grid_top;
  *out_cell_w = cell_w;
  *out_cell_h = cell_h;
  *out_icon = icon;
}

void home_cell_rect(int index, int n_focus, int label_band_h, int* out_x, int* out_y, int* out_w,
                    int* out_h) {
  int grid_top = 0, cell_w = 0, cell_h = 0, icon = 0;
  home_grid_metrics(n_focus, label_band_h, &grid_top, &cell_w, &cell_h, &icon);
  const int col = index % kHomeCols;
  const int row = index / kHomeCols;
  *out_x = kSideMargin + col * (cell_w + kHomeGapX);
  *out_y = grid_top + row * (cell_h + kHomeGapY);
  *out_w = cell_w;
  *out_h = cell_h;
}

/** Word-wrap `text` into at most `max_lines` centered lines within `max_w`,
 * ellipsizing the final line if it still overflows. Draws nothing if empty. */
void draw_label_wrapped_centered(Canvas& c, int cx, int top_y, int max_w, int max_lines,
                                 const char* text, Gray g) {
  if (!text || !*text) return;
  const Canvas::TextRole role = Canvas::TextRole::Secondary;
  const int line_h = c.text_height(role) + 4;
  std::string s(text);

  // Fits on one line — common case for current labels.
  if (c.text_width(s, role) <= max_w) {
    c.draw_text_centered(cx, top_y, s, role, g);
    return;
  }

  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < s.size() && static_cast<int>(lines.size()) < max_lines) {
    size_t best = pos;
    size_t scan = pos;
    while (scan <= s.size()) {
      size_t next_space = s.find(' ', scan);
      size_t end = (next_space == std::string::npos) ? s.size() : next_space;
      if (c.text_width(s.substr(pos, end - pos), role) <= max_w) {
        best = end;
        if (end >= s.size()) break;
        scan = end + 1;
      } else {
        break;
      }
    }
    if (best == pos) {
      // Single token wider than max_w — hard cut.
      size_t cut = pos + 1;
      while (cut < s.size() && c.text_width(s.substr(pos, cut - pos), role) <= max_w) ++cut;
      best = std::max(pos + 1, cut - 1);
    }
    lines.push_back(s.substr(pos, best - pos));
    pos = best;
    while (pos < s.size() && s[pos] == ' ') ++pos;
  }
  if (pos < s.size() && !lines.empty()) {
    std::string& last = lines.back();
    std::string ell = "...";
    while (!last.empty() && c.text_width(last + ell, role) > max_w) last.pop_back();
    last += ell;
  }
  int y = top_y - (static_cast<int>(lines.size()) - 1) * line_h;
  for (const auto& line : lines) {
    c.draw_text_centered(cx, y, line, role, g);
    y += line_h;
  }
}

int count_home_focusable(const DeviceConfig& cfg, HomeApp* out, int cap) {
  int n = 0;
  for (int i = 0; i < kHomeGridSlots && n < cap; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg, a)) out[n++] = a;
  }
  return n;
}

}  // namespace

void App::render_home() {
  draw_status_bar();

  // Product brand — no version number on Home. Wordmark stays the bitmap font.
  canvas_.draw_text(kSideMargin, kContentTop, kProductName, Canvas::TextRole::WordMark, Gray::G0);
  const int brand_rule_y = kContentTop + canvas_.text_height(Canvas::TextRole::WordMark) + 8;
  canvas_.hline(kSideMargin, brand_rule_y, 112, Gray::G1);

  HomeApp focusable[kHomeGridSlots];
  const int n_focus = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  const int label_line_h = canvas_.text_height(Canvas::TextRole::Secondary) + 2;
  const int label_band_h = label_line_h + 4;
  int grid_top = 0, cell_w = 0, cell_h = 0, icon = 0;
  home_grid_metrics(n_focus, label_band_h, &grid_top, &cell_w, &cell_h, &icon);
  // iOS corner radius ≈ 22% of icon edge.
  const int radius = std::max(12, icon * 22 / 100);

  for (int i = 0; i < n_focus; ++i) {
    const int col = i % kHomeCols;
    const int row = i / kHomeCols;
    const int cell_x = kSideMargin + col * (cell_w + kHomeGapX);
    const int cell_y = grid_top + row * (cell_h + kHomeGapY);
    const HomeApp a = focusable[i];
    const bool is_focus = a == focused;
    const int label_i = static_cast<int>(a);
    const char* label =
        (label_i >= 0 && label_i < kHomeGridSlots) ? kAppLabels[label_i] : "";

    // Square icon centered in the cell width; label band sits under it.
    const int icon_x = cell_x + (cell_w - icon) / 2;
    const int icon_y = cell_y;
    const int glyph_cx = icon_x + icon / 2;
    const int glyph_cy = icon_y + icon / 2;
    const int label_top = icon_y + icon + 4;
    const int label_max_w = cell_w - 4;

    if (is_focus) {
      canvas_.fill_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G0);
      draw_app_glyph(canvas_, a, glyph_cx, glyph_cy, icon, Gray::G3);
    } else {
      // Soft filled square (app-icon plate) + calm outline.
      canvas_.fill_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G2);
      canvas_.stroke_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G1, 1, Gray::G2);
      draw_app_glyph(canvas_, a, glyph_cx, glyph_cy, icon, Gray::G0);
    }
    // Name centered under the icon — always ink on paper (iPhone-style caption).
    draw_label_wrapped_centered(canvas_, cell_x + cell_w / 2, label_top, label_max_w, 1, label,
                                Gray::G0);
  }
}

void App::mark_home_focus_dirty(int prev_index, int next_index) {
  ++home_focus_spin_count_;
  // Every 6th focus move: content-band partial clears residual tile ghosts.
  // Otherwise only the union of the two cells — much less panel work than ContentBand.
  if ((home_focus_spin_count_ % 6) == 0) {
    mark_content_dirty();
    return;
  }
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  if (n <= 0) {
    mark_content_dirty();
    return;
  }
  const int label_band_h = canvas_.text_height(Canvas::TextRole::Secondary) + 6;
  int x0 = 0, y0 = 0, w0 = 0, h0 = 0;
  int x1 = 0, y1 = 0, w1 = 0, h1 = 0;
  home_cell_rect(std::clamp(prev_index, 0, n - 1), n, label_band_h, &x0, &y0, &w0, &h0);
  home_cell_rect(std::clamp(next_index, 0, n - 1), n, label_band_h, &x1, &y1, &w1, &h1);
  const int pad = 6;
  const int rx = std::max(0, std::min(x0, x1) - pad);
  const int ry = std::max(0, std::min(y0, y1) - pad);
  const int r2 = std::min(kCanvasW, std::max(x0 + w0, x1 + w1) + pad);
  const int b2 = std::min(kCanvasH, std::max(y0 + h0, y1 + h1) + pad);
  mark_region_dirty(rx, ry, r2 - rx, b2 - ry);
}

bool App::is_home_screen() const { return nav_.current() == ScreenId::Home; }

void App::apply_home_focus_delta(int delta) {
  if (!is_home_screen() || delta == 0) return;
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  focus_.count = std::max(1, n);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const int prev = focus_.index;
  focus_.move(delta);
  if (focus_.index == prev) return;
  mark_home_focus_dirty(prev, focus_.index);
}

void App::handle_home(InputEvent e) {
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  focus_.count = std::max(1, n);
  if (e == InputEvent::Up || e == InputEvent::Down) {
    apply_home_focus_delta(e == InputEvent::Up ? -1 : 1);
  } else if (e == InputEvent::Select && n > 0) {
    const HomeApp launch = focusable[focus_.index];
    if (parental_requires_pin(launch) && !parental_session_unlocked_) {
      parental_pending_app_ = launch;
      parental_pin_for_app_ = true;
      pin_entry_.clear();
      pin_spin_count_ = 0;
      pin_block_select_bounce_ = false;
      pin_enter_empty_slot();
      nav_.push(ScreenId::Pin);
      after_nav();
      return;
    }
    launch_home_app(launch);
  } else if (e == InputEvent::Back) {
    // stay on home
  }
}

}  // namespace pocket
