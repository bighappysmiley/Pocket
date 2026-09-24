#pragma once
#include <cstdint>

/** Pocket grayscale tokens (Spec §3.3). G0 black … G3 white. */
enum class Gray : uint8_t { G0 = 0, G1 = 1, G2 = 2, G3 = 3 };

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 800;
/** Tall enough for DejaVu StatusBar (~27px) + vertically centered icons. */
constexpr int kStatusBarH = 48;
constexpr int kSideMargin = 16;
/** First content baseline below the status bar. */
constexpr int kContentTop = kStatusBarH + 12;
/** Comfortable focus-row pitch for Body (27px) type. */
constexpr int kRowPitch = 56;
constexpr int kFocusRowH = 48;