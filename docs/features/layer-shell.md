# Feature: layer shell

## What it is

`river_layer_shell_v1` is how the window manager declares that it supports
layer surfaces (bars, wallpapers, launchers, lock screens) and tracks their
per-output and per-seat state. yarfwm binds the global in `Display`, owns the
per-output and per-seat state in `LayerShell`, and stores what it needs on
`Output` and `Seat` entries.

## Justification

**The bind is the gate.** River closes layer surfaces immediately when no
window manager holds `river_layer_shell_v1`; binding it is how the WM signals
support. Without the binding, a user's wallpaper and bar would never map.

## The pieces

- **Per output** (`src/Output.cpp`): `get_output` once per output,
  `set_default` on one output (manage-sequence-only, guarded by
  `default_layer_output` so it is sent once), and `non_exclusive_area` tracking —
  the area a bar leaves for windows, in **global** coordinates (verified: a
  37px top bar on 1280x720 gives `0,37 1280x683`). The placement pass uses it,
  so windows never sit under a bar.
- **Per seat** (`src/SeatLayerShell.cpp`): the focus split. An **exclusive**
  surface (a lock screen) takes the keyboard away from windows; a
  **non-exclusive** surface (a bar) does not — the XML says the window manager
  "continues to control focus and may choose to focus a different
  window/shell surface at any time", so yarfwm records the state and acts only
  on the transitions that matter. While exclusive focus is held, recorded focus
  intent is left pending, not discarded (river ignores focus requests until the
  surface lets go).
- **Session lock guard** (`src/ViewEvents.cpp`): `session_locked` /
  `session_unlocked` drive the key binding guard; the unlock handler also
  re-issues the recorded focus, because river drops the keyboard focus on unlock
  while the WM's own record still says the window is focused.

## Bugs fixed in this area

- **Layer surfaces never mapped** before the bind-as-gate fix: startup ordering
  had to bind `river_layer_shell_v1` before the surfaces appeared.
- **The lock guard stuck after one cycle** (see `docs/features/keyboard.md`) —
  the lock is a layer-shell concern and the bug lived in the interaction
  between the two.
- **A window dying while an overlay held exclusive focus stranded the
  keyboard:** the fallback is now kept pending and applied when the overlay
  releases the keyboard.
- **Shutdown crash** from a double-destroyed `wl_registry` proxy, fixed by
  giving `Server` sole ownership.

## New features

- Bars, wallpapers and launchers work out of the box (`waybar`, `wbg`,
  `swaybg` verified live), including exclusive-zone tracking, and the keyboard
  comes back to the right window after a lock screen lets go.
