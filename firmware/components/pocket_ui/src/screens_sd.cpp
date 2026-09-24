#include "pocket/app.hpp"

namespace pocket {

void App::refresh_sd_kind() {
  if (storage_) {
    storage_->probe();
    sd_kind_ = storage_->classify();
  } else {
    sd_kind_ = SdContentKind::Absent;
  }
}

void App::enter_sd_gate_from_hotplug() {
  if (sd_gate_active_) return;
  if (nav_.current() == ScreenId::SdCardGate || nav_.current() == ScreenId::OnboardingSdCard) return;
  sd_return_screen_ = nav_.current();
  sd_waiting_eject_ = false;
  sd_gate_active_ = true;
  refresh_sd_kind();
  focus_.index = 0;
  nav_.push(ScreenId::SdCardGate);
  after_nav();
}

void App::leave_sd_gate() {
  sd_waiting_eject_ = false;
  sd_gate_active_ = false;
  // Pop gate; if stack odd, replace with saved return.
  if (nav_.current() == ScreenId::SdCardGate) {
    if (!nav_.pop()) {
      nav_.reset(sd_return_screen_);
    }
  }
  after_nav();
}

void App::maybe_poll_sd_hotplug() {
  if (!cfg_.onboarding_complete || !storage_) return;
  if (nav_.current() == ScreenId::SdCardGate || nav_.current() == ScreenId::OnboardingSdCard) return;
  if (now_ms_ - last_sd_poll_ms_ < 800) return;
  last_sd_poll_ms_ = now_ms_;

  storage_->probe();
  const bool present = storage_->present();
  if (present && !sd_was_present_ && !sd_gate_active_) {
    play_sound(SoundId::Attention);
    enter_sd_gate_from_hotplug();
    // Gate owns presence until leave; mark seated after successful reformat path.
    return;
  }
  if (!present) {
    sd_was_present_ = false;
  }
}

void App::render_sd_gate() {
  draw_status_bar();
  const int ty = kContentTop;
  canvas_.draw_text(kSideMargin, ty, "microSD card", Canvas::TextRole::ScreenTitle, Gray::G0);

  if (sd_waiting_eject_) {
    int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kContentW, 6, "Remove the card to continue.",
                                      Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, y + 12, "Waiting for eject…", Canvas::TextRole::Secondary, Gray::G1);
    focus_.count = 1;
    canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "Checking…",
                            Canvas::TextRole::Body);
    return;
  }

  int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kContentW, 6, "A microSD card was detected.",
                                    Canvas::TextRole::Body, Gray::G0);
  if (sd_kind_ == SdContentKind::FirmwareRisk) {
    y = canvas_.draw_text_wrapped(kSideMargin, y + 10, kContentW, 6,
                                  "It looks modified. Reformat before Pocket uses it, or eject to keep files.",
                                  Canvas::TextRole::Secondary, Gray::G1);
  } else if (sd_kind_ == SdContentKind::Media) {
    y = canvas_.draw_text_wrapped(kSideMargin, y + 10, kContentW, 6,
                                  "Reformat clears the card for Pocket. Eject keeps your files.",
                                  Canvas::TextRole::Secondary, Gray::G1);
  } else {
    y = canvas_.draw_text_wrapped(kSideMargin, y + 10, kContentW, 6,
                                  "Reformat prepares the card for music and files. Eject leaves it unused.",
                                  Canvas::TextRole::Secondary, Gray::G1);
  }

  focus_.count = 2;
  const char* acts[] = {"Reformat and continue", "Eject"};
  for (int i = 0; i < 2; ++i) {
    const int row_y = y + 28 + i * kRowPitch;
    if (i == focus_.index)
      canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + 8, row_y + 10, acts[i], Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_sd_gate(InputEvent e) {
  if (sd_waiting_eject_) return;
  if (e == InputEvent::Up) {
    focus_.move(-1);
    mark_content_dirty();
    return;
  }
  if (e == InputEvent::Down) {
    focus_.move(1);
    mark_content_dirty();
    return;
  }
  if (e == InputEvent::Back) {
    // Back = eject path without unmount wait if they want to dismiss? Spec: must choose.
    // Treat Back as Eject for calm escape.
    focus_.index = 1;
  }
  if (e != InputEvent::Select && e != InputEvent::Back) return;
  play_sound(SoundId::Click);

  if (focus_.index == 0) {
    if (storage_ && storage_->erase_card()) {
      storage_->unmount();
      storage_->probe();
      storage_->music_ensure_root();
      sd_was_present_ = storage_->present();
      play_sound(SoundId::Success);
      leave_sd_gate();
    } else {
      error_msg_ = "Couldn't reformat the card.";
      error_until_ms_ = now_ms_ + 3000;
      play_sound(SoundId::Attention);
      mark_content_dirty();
    }
    return;
  }

  // Eject
  if (storage_) storage_->unmount();
  sd_waiting_eject_ = true;
  last_sd_poll_ms_ = 0;
  play_sound(SoundId::Attention);
  mark_content_dirty();
}

}  // namespace pocket
