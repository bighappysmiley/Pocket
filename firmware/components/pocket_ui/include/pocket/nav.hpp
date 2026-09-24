#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace pocket {

/** Stable screen IDs from Spec inventory (Part A + B deltas). */
enum class ScreenId : uint16_t {
  Lock,
  Pin,
  OnboardingWelcome,
  OnboardingCompanionDownload,
  OnboardingWifiList,
  OnboardingWifiPassword,
  OnboardingWifiConnecting,
  OnboardingCompanionQr,
  OnboardingPinLength,
  OnboardingPinSet,
  OnboardingPinConfirm,
  OnboardingTimezone,
  OnboardingMicTest,
  OnboardingDone,
  Home,
  NotesList,
  NotesDetail,
  ListsList,
  ListsDetail,
  LedgerComingSoon,
  ClockFace,
  ClockAlarms,
  ClockAlarmEdit,
  ClockTimers,
  ClockTimerRun,
  PassList,
  PassDetail,
  WeatherMain,
  WeatherCitySetup,
  SettingsRoot,
  SettingsSecurity,
  SettingsWifi,
  SettingsDisplay,
  SettingsSound,
  SettingsHomeApps,
  SettingsUnits,
  SettingsAbout,
  SettingsCloud,
  SettingsUpdate,
  SettingsUpdateProgress,
  SettingsUpdateResult,
};

struct NavStack {
  static constexpr int kMax = 16;
  ScreenId stack[kMax]{};
  int depth = 0;

  void reset(ScreenId root) {
    stack[0] = root;
    depth = 1;
  }
  ScreenId current() const { return depth > 0 ? stack[depth - 1] : ScreenId::Lock; }
  void push(ScreenId s) {
    if (depth < kMax) stack[depth++] = s;
  }
  bool pop() {
    if (depth > 1) {
      --depth;
      return true;
    }
    return false;
  }
  void replace(ScreenId s) {
    if (depth > 0) stack[depth - 1] = s;
    else reset(s);
  }
};

inline bool long_home_blocked(ScreenId s, bool onboarding_complete) {
  if (!onboarding_complete) {
    switch (s) {
      case ScreenId::OnboardingWelcome:
      case ScreenId::OnboardingCompanionDownload:
      case ScreenId::OnboardingWifiList:
      case ScreenId::OnboardingWifiPassword:
      case ScreenId::OnboardingWifiConnecting:
      case ScreenId::OnboardingCompanionQr:
      case ScreenId::OnboardingPinLength:
      case ScreenId::OnboardingPinSet:
      case ScreenId::OnboardingPinConfirm:
      case ScreenId::OnboardingTimezone:
      case ScreenId::OnboardingMicTest:
      case ScreenId::OnboardingDone:
        return true;
      default:
        break;
    }
  }
  if (s == ScreenId::Lock || s == ScreenId::Pin) return true;
  if (s == ScreenId::SettingsUpdateProgress) return true;
  return false;
}

/** Spec §6: full refresh on major enters; focus/routine UI stays partial. */
inline bool screen_requires_full_enter(ScreenId s) {
  switch (s) {
    case ScreenId::Lock:
    case ScreenId::Home:
    case ScreenId::Pin:
    case ScreenId::PassDetail:
    case ScreenId::SettingsUpdateProgress:
    case ScreenId::OnboardingWelcome:
    case ScreenId::OnboardingCompanionDownload:  // QR clarity
    case ScreenId::OnboardingCompanionQr:        // QR clarity
    case ScreenId::OnboardingWifiPassword:  // SoftAP Link phase — password + QR clarity
    case ScreenId::OnboardingWifiConnecting:
    case ScreenId::OnboardingPinLength:
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm:
    case ScreenId::NotesList:
    case ScreenId::ListsList:
    case ScreenId::LedgerComingSoon:
    case ScreenId::ClockFace:
    case ScreenId::PassList:
    case ScreenId::WeatherMain:
    case ScreenId::SettingsRoot:
      return true;
    default:
      return false;
  }
}

inline int onboarding_step_of(ScreenId s) {
  // welcome=1 … done=8 (companion download required after welcome)
  switch (s) {
    case ScreenId::OnboardingWelcome:
      return 1;
    case ScreenId::OnboardingCompanionDownload:
      return 2;
    case ScreenId::OnboardingWifiList:
    case ScreenId::OnboardingWifiPassword:
    case ScreenId::OnboardingWifiConnecting:
      return 3;
    case ScreenId::OnboardingCompanionQr:
      return 4;
    case ScreenId::OnboardingPinLength:
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm:
      return 5;
    case ScreenId::OnboardingTimezone:
      return 6;
    case ScreenId::OnboardingMicTest:
      return 7;
    case ScreenId::OnboardingDone:
      return 8;
    default:
      return 0;
  }
}

}  // namespace pocket