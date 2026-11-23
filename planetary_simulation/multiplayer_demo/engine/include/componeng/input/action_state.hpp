#pragma once

#include <cstddef>

namespace componeng::input {

enum class Action {
  MoveForward,
  MoveBackward,
  MoveLeft,
  MoveRight,
  Jump,
  Crouch,
  Sprint,
  Attack,
  Aim,

  Count
};

struct ActionState {
  bool current[size_t(Action::Count)] = {false};
  bool previous[size_t(Action::Count)] = {false};

  bool pressed(Action action) const {
    return current[size_t(action)] && !previous[size_t(action)];
  }

  bool down(Action action) const {
    return current[size_t(action)];
  }

  bool released(Action action) const {
    return !current[size_t(action)] && previous[size_t(action)];
  }

  void swapBuffers() {
    for (size_t i = 0; i < size_t(Action::Count); ++i) {
      previous[i] = current[i];
    }
  }
};

} // namespace componeng::input
