#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

namespace aurora::window {

// Automation owns every game-facing input while a tape is active. This is the
// deliberately narrow exception forwarded only to ImGui so host debug
// rendering can be inspected without leaking input to RmlUi or the game.
constexpr bool is_automation_debug_ui_event(const SDL_Event& event) noexcept {
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
  case SDL_EVENT_MOUSE_WHEEL:
    return true;
  default:
    break;
  }

  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
    return false;
  }

  switch (event.key.key) {
  case SDLK_F1:
  case SDLK_LSHIFT:
  case SDLK_RSHIFT:
    return true;
  default:
    return false;
  }
}

} // namespace aurora::window
