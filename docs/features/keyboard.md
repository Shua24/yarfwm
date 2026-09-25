# Feature: keyboard bindings

## What it is

The keyboard engine turns the `keybinds` array of `config.json` into
`river_xkb_binding_v1` objects, one per bind per seat, and runs the bound action
when river reports a press. Parsing lives in `src/KeybindParse.cpp`, the binding
objects and repeat timer in `src/Keybind.cpp`, and action dispatch in
`src/KeybindActions.cpp`.

## Justification

River delegates all keyboard policy to the window manager: the compositor
delivers binding presses, leaves held-key repeat to the client, and consumes the
key so it never reaches the focused window (for registered binds). Everything a
user expects from key bindings — the modifier semantics, repeat, the lock-screen
guard — is therefore yarfwm's to implement and document.

## The rules that matter

- **Keysym folding.** River matches the base-layer (level 0) keysym: `Super+
  Shift+r` arrives as `r`. ASCII A-Z binds are registered lowercase, so
  `Super+Shift+L` works while the config spells the key `L`.
- **Exact modifier mask.** The declared modifiers are OR-ed into one mask and
  river requires an exact match; a bind declared `Super` fires only with Super
  alone held. Aliases: `Super`/`Logo`, `Ctrl`/`Control`; `requires_shift` folds
  SHIFT in.
- **Per-seat registration and the enable gate.** Bindings are created per seat;
  `enable` is manage-sequence-only and happens in `Keybind::apply_manage()`,
  called from the manage sequence handler.
- **Client-side repeat.** River sends a press once and leaves repeating to the
  window manager, so yarfwm runs a `timerfd` (`src/RepeatTimer.cpp`) polled next
  to the Wayland socket; the first run is the press itself and the timer
  produces repeats. Directional focus moves and pointer warps repeat by default;
  a per-bind `"repeat"` field wins.
- **The lock guard.** While a lock screen holds the keyboard, every action
  except `exit_session` is refused. The guard is set by
  `window_manager_session_locked` and **cleared by
  `window_manager_session_unlocked`** — both must stay in step, or every binding
  silently dies after the first lock cycle (see bugs below).

## Bugs fixed in this area

- **The session lock guard never cleared.** `window_manager_session_unlocked`
  re-issued the focus but did not clear `session_locked`, so after one
  lock/unlock cycle every binding was refused for the rest of the run. The
  unlock handler now clears the guard before restoring focus. A unit test
  (`tests/ViewLockTest.cpp`) pins the lifecycle; the live regression probe
  checks both directions (refused while locked, firing after unlock).
- **Repeat could outlive its seat.** A seat river removed left binding objects
  and an armed repeat pointing at a proxy about to be destroyed;
  `Keybind::forget_removed_seats()` now drops them before `Seat::apply_manage()`
  destroys the proxy.
- **Shift+letter binds never fired** (the keysym-fold rule above, learned the
  hard way against river 0.4.8).

## New features

- Directional focus, directional window moves, window state actions (maximize,
  fullscreen, always-on-top, minimize/restore), geometry actions (center, fit,
  resize by percent), virtual desktop switching, spawn, close and exit_session —
  28 distinct actions, all implemented.
- **Keyboard pointer movement** (`move_pointer_left`/`_right`/`_up`/`_down`,
  `Super+Shift` + arrows): moves the pointer one 32px step per press through
  `river_seat_v1.pointer_warp` (manage-sequence-only, so the offset is recorded
  and sent by the next manage sequence). The position river reports in the
  `pointer_position` event is stored per seat, and each warp moves relative to
  it, so a held key walks the pointer. River clamps a target outside all
  outputs into the nearest output.
