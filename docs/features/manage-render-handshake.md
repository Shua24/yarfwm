# Feature: the manage/render handshake

## What it is

The `river-window-management-v1` protocol makes the window manager drive two
strictly ordered request sequences per event-loop turn:

```
manage_start -> (manage-only requests) -> manage_finish     [mandatory]
render_start -> (render-only requests) -> render_finish     [mandatory]
```

River stalls its event loop until each `finish` arrives. yarfwm implements the
two sequence handlers in `src/ViewSequences.cpp`:

- `window_manager_manage_start` (`src/ViewSequences.cpp:25`) proposes
  dimensions for unmanaged windows, points layer shell at a default output,
  sends pending resize proposals, applies recorded window state (maximized,
  fullscreen, always-on-top, minimized, parent stacking), sends close requests,
  then runs the key binding and seat `apply_manage()` paths, and finishes.
- `window_manager_render_start` (`src/ViewSequences.cpp:195`) applies the
  show/hide visibility pass (desktop + minimized state) and the placement pass
  (`place_windows()`), then finishes.

## Justification

The protocol requires it: a window does not appear until the manager proposes
dimensions in a manage sequence, river answers with `dimensions`, and the
following render sequence is finished. Skipping either finish stalls river's
loop.

## The v5/v6 rule

The vendored protocol XML is v6, but river 0.4.8 enforces v5 sequencing, so the
client binds `min(advertised, 6)`. In v5, `set_position` and `show` are
render-sequence-only, while `propose_dimensions`, `set_capabilities`,
`set_default`, `close`, and the state requests are manage-sequence-only. The
code never sends the v6-only members (`op_start_touch`/`op_end_touch` and the
touch events) until river advertises v6.

## Bugs fixed in this area

- **Closing the last window crashed the WM** (SIGSEGV, exit 139):
  `View::remove_window` asked the seat for a fallback before the dying window
  was dropped from the tracked list, so the fallback could be the dying window
  itself and the next manage sequence focused a destroyed proxy. The window is
  now dropped before the fallback is computed.
- **A window dying under an exclusive layer surface stranded the keyboard:**
  `Seat::apply_manage()` discarded the pending focus fallback while an overlay
  held exclusive focus; it now leaves the recorded intent pending, and the
  first manage sequence after the overlay lets go applies it.
- **Shutdown double-free:** the `wl_registry` proxy was destroyed by both
  `Display` and `Server`; only `Server` owns it now.

## New features

- The full window lifecycle: propose, track configures, map, render, close,
  release, with the placement cascade re-applying on every render sequence.
