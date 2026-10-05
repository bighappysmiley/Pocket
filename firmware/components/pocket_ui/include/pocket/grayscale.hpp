#pragma once
#include <cstdint>

/** Pocket grayscale tokens (Spec §3.3). G0 black … G3 white. */
enum class Gray : uint8_t { G0 = 0, G1 = 1, G2 = 2, G3 = 3 };

constexpr int kCanvasW = 480;
constexpr int kCanvasH = 800;

// --- Global layout system (POCKET-LIVE-v50-layout-all) -------------------
// One rhythm for every screen: status → title → body/list → bottom CTA.
// Spec: side margins 16; content never draws into the status bar; wrap
// instead of clipping; calm e-ink density (not cramped, not sparse).

/** Tall enough for DejaVu StatusBar (~33px) + vertically centered icons. */
constexpr int kStatusBarH = 56;
/** Spec §3.6 side margins. */
constexpr int kSideMargin = 20;
/** First content baseline below the status bar (air under the rule). */
constexpr int kContentTop = kStatusBarH + 16;
/** Onboarding “Step N of 9” band. Titles start below this when a step is shown. */
constexpr int kOnboardingStepBand = 40;
constexpr int kOnboardingTitleY = kContentTop + kOnboardingStepBand;

/** Comfortable focus-row pitch for Body (33px) type. */
constexpr int kRowPitch = 64;
constexpr int kFocusRowH = 56;
/** Full-width focus / list row (side margins already applied). */
constexpr int kFocusRowW = kCanvasW - 2 * kSideMargin;
/** Horizontal inset for unfocused row labels. */
constexpr int kRowLabelInset = 8;
/** Vertical pad so Body sits inside a focus-row band. */
constexpr int kRowTextPad = 12;
/** Max width for unfocused row labels. */
constexpr int kRowLabelW = kFocusRowW - 2 * kRowLabelInset;

/** Stacked Body / Secondary lines. */
constexpr int kBodyLinePitch = 44;
/** ScreenTitle / WordMark (49px) → first Body / list row. */
constexpr int kTitleToBody = 64;
/** Default wrap line gap (Body / Secondary). */
constexpr int kWrapGap = 6;
/** Small section air between blocks. */
constexpr int kSectionGap = 16;

/** Tab strip (Notes/Lists, Clock tabs) then gap before the list. */
constexpr int kTabBand = kFocusRowH + kSectionGap;
/** Title-only screens: first list / body y. */
constexpr int kListTop = kContentTop + kTitleToBody;
/** Title + one Secondary meta line (Music/Reading status) before the list. */
constexpr int kListTopWithMeta = kContentTop + kTitleToBody + 28;

/** Bottom primary CTA row. */
constexpr int kBottomCtaY = kCanvasH - kFocusRowH - 28;
/** Footer meta (offline hint, lock message). */
constexpr int kFooterY = kCanvasH - 36;
/** Empty-state copy — calm mid-canvas, never under the status bar. */
constexpr int kEmptyCenterY = 300;
constexpr int kEmptyHintY = kEmptyCenterY + kBodyLinePitch;

/** Usable text width with side margins. */
constexpr int kContentW = kCanvasW - 2 * kSideMargin;

/** Y just below a ScreenTitle/WordMark drawn at `title_y`. */
inline constexpr int below_title(int title_y) { return title_y + kTitleToBody; }

/** Stacked bottom actions ending at kBottomCtaY. `from_bottom` 0 = lowest. */
inline constexpr int bottom_action_y(int from_bottom) {
  return kBottomCtaY - from_bottom * kRowPitch;
}

/** List / focus row y for index `i` starting at `top`. */
inline constexpr int row_y(int top, int i) { return top + i * kRowPitch; }
