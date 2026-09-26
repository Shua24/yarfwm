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
  resize by percent), virtual desktop switching, spawn, close, exit_session and
  the decoration toggle — 31 distinct actions, all implemented.
- **Minimize follows labwc's Iconify** (2026-09-25): minimizing works on the
  **whole window hierarchy** — a dialog and its toplevel go together, whichever
  one asked, the way labwc minimizes the root and then every sub-view
  (`src/view.c:784-816`). Restore brings back the **most recently minimized**
  hierarchy, selected by minimize order rather than array position, and hands it
  the keyboard and the front of the stack. Both are bound by default
  (`Super+M` / `Super+Shift+M`), and restoring a window that was minimized on
  another virtual desktop brings that desktop forward so the window is actually
  visible (`src/desktop.c:142-148`). A taskbar still cannot restore one — that
  path is river-side and impossible here (`docs/features/panel-taskbar.md`).
- **Window stacking order** (labwc-resemblance batch): clicking a window
  focuses **and raises** it, a newly mapped window starts at the front, an
  un-minimized window comes back to the front, and the focus fallbacks (close,
  minimize, desktop switch, lock restore) hand the keyboard to the **topmost
  visible window** — the same choice labwc's `desktop_focus_topmost_view()`
  makes. yarfwm keeps its own stacking record (`Window::z_order`) because the
  protocol has no stacking query, and every `place_top`/`place_bottom`/
  `place_above` goes out in the **render** sequence, which is the only sequence
  v5 allows for them.
- **Exact maximize restore**: un-maximizing restores the pre-maximize geometry
  (position and size), the way labwc restores `natural_geometry`; the old
  behavior recomputed half the placement area and lost the position.
- **Keyboard pointer movement** (`move_pointer_left`/`_right`/`_up`/`_down`,
  `Super+Shift` + arrows): moves the pointer one 32px step per press through
  `river_seat_v1.pointer_warp` (manage-sequence-only, so the offset is recorded
  and sent by the next manage sequence). The position river reports in the
  `pointer_position` event is stored per seat, and each warp moves relative to
  it, so a held key walks the pointer. River clamps a target outside all
  outputs into the nearest output.
