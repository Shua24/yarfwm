# Feature: protocol completion

## What it is

The pass that closed the gap between what yarfwm's config declared and what the
river window management protocol can actually carry: every action the default
config names is implemented and registered, every request the protocol offers
has been classified (used, or deliberately unused), and the config carries only
keys with consumers.

## Justification

A window manager that quietly skips a third of its config is lying to its user.
Two rules drove the pass:

1. **If river's protocol cannot carry it out, it goes** — config entry, parser
   branch, enum, dead code, docs.
2. **Every surviving config key must be read by code that actually runs under
   the protocol.** A key nothing reads is removed, not deferred; it returns only
   with the feature that consumes it.

## The state it produced

- **Actions:** 29 distinct action names in the default config, all implemented,
  all registered; 45 binds parsed and 45 registered, nothing skipped. The
  startup line reports it: `Yarfwm: 45 key bindings parsed`,
  `Yarfwm: registered 45/45 key bindings on a seat`.
- **Stubs:** 23 empty handler bodies at the start → **8** now, and each
  remaining one is inert by design (a listener slot that must stay non-NULL, or
  an event with no policy attached). The `pointer_position` handler — the one
  meaningful stub — now stores the position and feeds the keyboard pointer
  warp (`docs/features/keyboard.md`).
- **Unused requests, classified:** of the requests the protocol offers, these
  remain uncalled and are documented as such, not hidden: `get_shell_surface`,
  `set_xcursor_theme`, `get_pointer_binding`, `set_borders`, `set_clip_box`,
  `set_content_clip_box`, `set_dimension_bounds`, `set_presentation_mode`,
  `place_below`, `river_decoration_v1`. Some are candidates for future
  features (compositor-drawn borders via `set_borders`); some have no consumer
  under yarfwm's policy.
- **The config-constraint schema.** The config is now exactly
  `{ "input": { "focus_follows_mouse": true }, "keybinds": [ ...45... ] }` —
  two keys, both consumed. Everything else the inherited config carried
  (`layout`, `outputs`, `workspaces`, `spawn_at_startup`, `window_rules`,
  `prefer_no_csd`, `screenshot_path`, the inert input sub-blocks) was read by
  nothing and is gone; each returns with the feature that consumes it.
- **Dropped for good:** `toggle_expose` and `show_hotkey_overlay` — an expose
  grid needs a picture of every window (the protocol has no screenshot,
  thumbnail or scaling request) and the overlay needs a WM-owned renderer.

## Decoration honesty (the CSD/SSD split)

`apply_decoration_hint` sends nothing, on purpose. Client-side decorations are
**river's default** when the window manager sends neither `use_csd` nor
`use_ssd` ("This is the default if neither this request nor the use_ssd request
is ever made"), so windows that want CSD already get it. Honoring a
server-side hint would mean drawing the decoration — that needs a WM renderer
(`river_decoration_v1` surfaces + shared memory, or compositor-drawn borders
via `set_borders`), which is a future feature, not this batch. The handler is a
pure observer: it logs each hint and stores nothing, because the XML allows the
hint to be re-sent whenever the window changes its preferences.

## The two protocol traps that shaped the code

- **A NULL listener slot aborts the process.** libwayland kills the client
  (SIGABRT) when an event arrives for a NULL slot; a river window sends
  app_id/title immediately, so a half-filled listener kills the WM on the first
  window. Every slot is populated, even with a no-op.
- **Sequence rules are per-request and versioned.** v5 is what river 0.4.8
  enforces: `show`/`set_position`/`place_*`/`set_borders` are render-only there,
  while `propose_dimensions`, focus, and the state requests are manage-only.
  The code binds `min(advertised, 6)` and obeys v5 until river speaks v6.

## Bugs fixed during the pass

- `session_locked` never cleared on unlock (every binding died after one lock
  cycle) — see `docs/features/keyboard.md`.
- `prefer_no_csd` was read into a member no code used, and a stale comment
  claimed the WM sent `use_csd`/`use_ssd` — it never did. Both removed.
- `decorations_sent` once-guard in the hint handler swallowed re-sends the XML
  explicitly allows; the handler is now stateless.

## New features

- 12 previously-skipped actions implemented (window moves, desktop switching,
  state and geometry actions), the capability fix that unadvertised
  `WINDOW_MENU`, keyboard pointer movement, and a unit-test suite that pins
  parsing, placement math, config behaviour and the lock guard.
