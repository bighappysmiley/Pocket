#pragma once
#include <cstdint>

/** Pocket grayscale tokens (Spec §3.3). G0 black … G3 white. */
enum class Gray : uint8_t { G0 = 0, G1 = 1, G2 = 2, G3 = 3 };

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 800;
/** Tall enough for DejaVu StatusBar (~33px) + vertically centered icons. */
constexpr int kStatusBarH = 56;
constexpr int kSideMargin = 16;
/** First content baseline below the status bar. */
constexpr int kContentTop = kStatusBarH + 14;
/** Onboarding “Step N of 9” band. Titles start below this when a step is shown. */
constexpr int kOnboardingStepBand = 40;
constexpr int kOnboardingTitleY = kContentTop + kOnboardingStepBand;
/** Comfortable focus-row pitch for Body (33px) type. */
constexpr int kRowPitch = 64;
constexpr int kFocusRowH = 56;
/** Stacked Body / Secondary lines. */
constexpr int kBodyLinePitch = 44;
/** ScreenTitle (49px) to first Body line. */
constexpr int kTitleToBody = 64;
/** Bottom CTA row. */
constexpr int kBottomCtaY = kCanvasH - kFocusRowH - 32;
/** Usable text width with side margins. */
constexpr int kContentW = kCanvasW - 2 * kSideMargin;
