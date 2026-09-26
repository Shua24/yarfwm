# Server-side decorations

Yarfwm draws its own titlebar for every window: a 26px strip above the client
area with the window title and three buttons (minimize, maximize, close), plus
a compositor-drawn focus border around the window. Both are the window
manager's own work — no toolkit, no client cooperation required.

**Status: implemented and unit-tested; the live arm matrix is in
`.hermes/notes/2026-09-25-ssd/`.** It is not yet approved as shipped behaviour.

## Why this exists

The project's constraint is that river does the rendering and yarfwm speaks only
the river protocols. That rules out the usual compositor approach to
decorations — a wlroots scene graph with a renderer of our own — and it is why
an earlier constraint audit called server-side decorations *impossible*
(`.hermes/notes/2026-09-25-labwc-parity/constraint-verdict.md`, item A2).

That audit was wrong, and the correction is the interesting part of this
feature. Two requests in the protocol already do the work:

- **`river_window_v1.get_decoration_above`** hands the window manager a
  `river_decoration_v1` object for a `wl_surface` of its own. River composites
  that surface above the window at an offset we choose. The pixels are ours to
  paint, but the compositing, the stacking and the lifetime are river's.
- **`river_window_v1.set_borders`** makes river draw four `wlr_scene_rect`s
  around the window itself. A focus border therefore costs us no renderer at
  all.

So the honest summary is: river draws, we paint one buffer. `wl_shm` +
`wl_compositor` (bound to null in `Display.cpp` before this feature) supply the
buffer, and cairo/pango paint the title text.

**What `use_ssd` does, and does not do.** It is easy to read
`river_window_v1.use_ssd` as "please draw my decorations". It does not: it sets
`wm_requested.ssd`, which propagates to `wlr_xdg_decoration_v1.setMode(
server_side)` and tells the *client* to stop drawing its own titlebar. The
window manager is then entirely responsible for the pixels. A client that
negotiates CSD regardless will keep its own bar, and we would be drawing a
second one over it — which is why `apply_decorations` skips the titlebar
entirely for a `decoration_hint` of `only_supports_csd`, and why
`docs/features/protocol-completion.md` used to list `set_borders` and
`river_decoration_v1` as uncalled.

## The files

| file | what it holds |
|---|---|
| `include/DecorationGeometry.hpp`, `src/DecorationGeometry.cpp` | Pure layout maths: where the buttons go, which one a point hits, the text band, the titlebar offset. No Wayland, no cairo — unit-testable. |
| `include/ShmBuffer.hpp`, `src/ShmBuffer.cpp` | A `wl_shm` pool, its `mmap`, and the `wl_buffer` created from it. Owns the lifetime and releases the mapping. |
| `include/DecorationRenderer.hpp`, `src/DecorationRenderer.cpp` | Paints one titlebar into an ARGB8888 buffer with cairo/pango, and converts a config colour into the components `set_borders` wants. |
| `include/DecorationSettings.hpp` | The resolved appearance (`DecorationSettings`, `TitlebarColors`) plus `decoration_settings_from()`. Deliberately cairo-free so `View.hpp` can hold one **by value** without including the renderer. |
| `include/Decoration.hpp`, `src/Decoration.cpp` | One window's decoration: the surface, the buffer, the `river_decoration_v1`, the repaint decision. |
| `include/WindowDecorationsConfig.hpp`, `src/WindowDecorationsConfig.cpp` | Reads `window-decorations.json`. Never fails the caller. |
| `src/ViewDecoration.cpp` | The decoration pass, run at the end of the render sequence. Also `window_for_decoration_surface` / `decoration_part_at`, and `decoration_settings_from`. |
| `src/SeatDecoration.cpp` | The `wl_pointer` handlers — the click channel (see below). |
| `include/ViewWindow.hpp` | `struct View::Window`, moved out of `View.hpp` to stay under the 500-line limit. |

## The decisions, and why

**1. One surface per window, `set_offset(0, -titlebar_height)`.**
`get_decoration_above` places the decoration above the window content. The
offset is applied *after* the window's position, so `-h` puts the bar directly
on top of the client area. This is not clipped: river passes `requested.clip`
(default `{0,0,0,0}`) and `wlr_scene_subsurface_tree_set_clip` documents that an
empty clip disables clipping entirely. Verified by pixel count — the band lands
exactly where predicted.

**1a. The bar is never drawn over the window, and windows are placed to give it
room.**

The bar belongs outside the content. Getting there took two changes, because
either one alone leaves a hole:

*Placement reserves the frame's top strip.* `View::content_area()` is the
placement area with `titlebar_height + border_width` reserved at its top, and
that — not `placement_area()` — is what `place_windows()`,
`propose_default_dimensions()`, maximize, `fit_to_output` and `center_window`
fill. A window placed at the reserved origin therefore has exactly the room the
bar needs above it, so `set_offset(0, -(h + b))` lands the bar above the window
with neither the content nor the border row covered.

*The offset refuses to cover the content, and clears the border too.*
`titlebar_offset()` returns a `TitlebarPlacement`. `b` is `border_width`, which
matters because river draws the border OUTSIDE the content — the top border
occupies the rows `[-b, 0)` relative to the content's top edge
(`river/Window.zig:1011-1016`) — and river paints above-decorations LAST, so the
bar wins every row it covers (`river-window-management-v1.xml:1199-1202`):

| Room | Placement | Offset |
| --- | --- | --- |
| `>= h + b` above the content, inside the area | `titlebar_placement_above` | `(0, -(h + b))` |
| not above, but `>= h + b` below | `titlebar_placement_below` | `(0, +content.height + b)` |
| neither | `titlebar_placement_hidden` | nothing painted |

**The border row bug.** With the plain `-h` offset the bar's bottom row lands
exactly on the top border row, and because the bar is painted last it hides it:
measured on a focused decorated window, the top border row had **0 of 640
border pixels** while the bottom and right had all of theirs. The bar now clears
the border by `border_width`, which is what labwc does as well — its frame top
margin is `titlebar_height + border_width` (`labwc src/ssd/ssd.c:74-79`) and it
insets a maximized window's content by that whole margin
(`src/view.c:1283-1299`). After the fix the same row measures 640 of 640.

**The bug this fixes.** Before both changes, `titlebar_offset()` clamped the
offset to `0` whenever the window was flush with the area's top edge, which drew
the bar *inside* the content's top edge — over the window. The original reason
for that clamp was real: a maximized window filling the area would otherwise
have its bar at `y = -26..-1`, entirely above the output, composited and showing
nothing (measured: 0 titlebar pixels anywhere). The clamp fixed that by covering
the window instead, and because the cascade started at the raw area origin, the
*first window of every session* hit it. Measured on the built binary with a
flat-colour probe window: the content's rows `0..25` — exactly
`titlebar_height` — were titlebar pixels.

**Measured after the fix** (headless river, magenta probe window, one grim
screenshot per arm; the full matrix is in
`.hermes/notes/2026-09-26-decor-border/decoration-overlap-fix.md`):

| arm | bar rows | rows between bar and content | content rows | verdict |
|---|---|---|---|---|
| fixed, floating | 0..25 | 26 (641/1280 border px) | 27..372 (346) | nothing covered |
| control (installed, pre-fix), floating | 0..25 | none | 26..359 (334) | 26 rows covered |
| fixed, maximized | 0..25 | 26 (1280/1280 border px) | 27..719 (693) | nothing covered |
| control, maximized | 0..25 | none | 26..719 (694) | 26 rows covered |

The vertical order through a fixed window reads bar `#203040`, then one focus
border row `#5c8fb0`, then content: the window is moved down by the reserved
strip rather than shrunk, so no content is lost.

The replacement keeps the bar visible without covering anything: the reserved
strip moves the window down instead. `titlebar_placement_below` covers the case
where the user drags a window to the area's top edge by hand (there is no room
above it any more, but the bar can go under it), and `titlebar_placement_hidden`
covers a window that fills the area on both axes — a lost bar is a smaller loss
than a window with its top rows hidden. The maximized case now measures as
content at `y 27..719` with the bar at `y 0..25` and the border row at `y 26`:
nothing covered. `titlebar_when_maximized: false` still hides the bar entirely,
which is what that key is for.

Two consequences worth knowing:

- A window the user drags flush against the top edge of the area gets its bar
  *below* it, not above it, so the bar moves to the bottom of the window rather
  than disappearing. Drag it down one step and the bar returns to the top.
- The bar's hidden case is per-sequence: it is recomputed in `apply_offset()`
  and the surface is detached (`wl_surface_attach(NULL)`) when the bar has
  nowhere to sit, so a window that loses the room cannot keep a stale bar
  covering it.

**2. The border is river's, not ours.**
`set_borders(edges, width, r, g, b, a)` with the colour as four 32-bit
percentages (`0xffffffff` = 100%). It costs us nothing and cannot get out of
step with the window geometry, because river draws it against the window it
belongs to.

**3. The appearance lives in its own file.**
`$HOME/.config/yarfwm/window-decorations.json`, not `config.json`. Two files,
two concerns, two consumers, two test suites: a typo in a colour must not be
able to break your keybindings, and a bad keybind must not make a titlebar
unreadable. It also keeps jsoncpp out of the renderer's link. `Config` still
carries exactly two settings. The pattern is written up in the README under
"Adding a configuration file" so the next setting copies it.

**4. Colours are `0xAARRGGBB`.**
Alpha first, as one 32-bit integer in the config. `set_borders` wants the
components separately, so `border_color_components()` is the single conversion
site. (The plan's own first draft of a border test expected `0xff0000ff` to be
"full red" — under this format it is opaque **blue**. The test was wrong, not
the code.)

**5. cairo + pango, not fcft.**
pango is the larger dependency (it pulls glib and harfbuzz) but it gives
ellipsis, font fallback and text metrics for free. With fcft we would hand-roll
elision. Both are already installed on the target system, and JetBrains Mono
resolves through fontconfig.

**6. The titlebar is only repainted when something changed.**
`Decoration::update()` returns false unless the title, geometry, focus state,
maximize/fullscreen state or metrics actually changed. A render sequence fires
on every pointer move; repainting there would be a frame-rate-dependent CPU
burn. The probe checks it by counting `wl_surface.commit` on the decoration
surface while idle.

**7. Edge-resize by dragging is refused, not forgotten.**
labwc's resize grab bands sit *outside* the frame (`ssd-extents.c`), which is
possible for it because a wlroots view is not a `wl_surface` with a fixed input
region. A `wl_surface` input region cannot extend past its own surface, and the
border belongs to the client's surface anyway, so a resize grab band is
structurally impossible here. Keyboard resize is the answer, and this is the
honest cost of the no-wlroots constraint. Shadows are refused for the same
reason: river owns the scene, and a shadow is a second surface with a blur we
would have to composite ourselves.

## The bug that mattered: the click double-fires

A press on our own titlebar produces **two** events, and this is measured, not
inferred (a standalone probe against river 0.4.8):

1. `wl_pointer.button` on the window manager's own pointer, with
   **surface-local** coordinates.
2. `river_seat_v1.window_interaction` naming the **parent window**, in the
   manage sequence that follows.

Neither suppresses the other, and the second one arrives *after* the first. The
interaction handler records focus and raises; `Seat::record_focus` only *queues*
intent, which `Seat::apply_manage` drains later. So the naive version of this
feature behaved like this:

1. Click minimize → the window minimizes, and focus is handed to another
   window (queued).
2. `window_interaction` for the **minimized** window arrives and re-queues focus
   for it, overwriting the handoff.
3. `apply_manage` focuses the minimized window — and it **pops back up**.

The fix is a claim/consume pair: `View::claim_decoration_click(window)` from the
button handler, and `consume_decoration_click(window)` at the top of
`river_seat_window_interaction`, which returns early on a match. The claim is
retired in `Seat::pointer_enter` when the pointer enters a surface that is not
one of our decorations, so a claim from a press river ignored can never swallow
a later genuine content click.

**An independent verification pass corrected the symptom, and the correction is
worth recording.** The pop-back-up described above is *not reachable in this
design*: focus never gates visibility. `show()`/`hide()` are called from exactly
one place (`src/ViewSequences.cpp`, the visibility pass) and keyed only on
`Window::minimized` and the window's desktop — never on focus. So without the
guard the interaction re-queues focus for the minimized window and the keyboard
goes to an **off-screen** window; the window does not reappear. Measured with a
deliberately de-guarded build: minimizing with and without the guard produced
identical pixels (the window stayed hidden either way), but the focus handoff
diverged — with the guard, typing went to the still-visible window; without it,
the keystrokes landed in the hidden window's client. **The guard is load-bearing
for the focus handoff, not for visibility**, and the no-guard build is the
control that proves it.

Two consequences worth knowing:

- The titlebar **bar** (away from the buttons) still has to focus and raise the
  window itself, because the interaction is consumed. It does that explicitly
  before starting an interactive move, so dragging the bar moves the window
  through the same `op_start_pointer` handshake a client-side titlebar uses.
- The three **buttons** deliberately do *not* focus the window. Minimizing or
  closing a window should not give it the keyboard.

**`wl_pointer.button` carries no coordinates.** Only `enter`, `motion` and
`warp` do. The position therefore has to be cached from the preceding
`enter`/`motion` — which is what `Seat::hovered_surface` / `pointer_surface_x` /
`pointer_surface_y` are for. Never read a position off the button event.

**`wl_seat.get_pointer` is a protocol error before the capability exists.** The
pointer is bound from the `wl_seat.capabilities` event, not when river names the
seat. A headless session with `WLR_LIBINPUT_NO_DEVICES=1` starts with no
capabilities at all, and getting this wrong kills the window manager at startup
with `wl_seat.get_pointer called when no pointer capability has existed`.

## Sequence discipline

River 0.4.8 enforces the **v5** rules, which are stricter than the v6 XML this
repository vendors:

| request | sequence |
|---|---|
| `set_borders` | render only |
| `river_decoration_v1.set_offset` | render only |
| `use_ssd` | manage only |
| `get_decoration_above` | neither |

So `apply_decorations()` runs at the **end** of the render sequence, after
`place_windows()` has written the geometry it needs, and `use_ssd` is sent from
`window_manager_manage_start`. **Never call `sync_next_commit`** — it raises a
`no_commit` protocol error unless a `wl_surface.commit` follows before
`render_finish`; a plain `attach` + `commit` avoids the hazard entirely.

## Configuration

`$HOME/.config/yarfwm/window-decorations.json`, written from the default
embedded in the binary on first start and never overwritten afterwards:

```json
{
  "enabled": true,
  "titlebar_height": 26,
  "border_width": 1,
  "button_width": 26,
  "button_spacing": 0,
  "padding": 0,
  "titlebar_when_maximized": true,
  "font": "JetBrains Mono 10",
  "titlebar_active_color": "ff203040",
  "titlebar_inactive_color": "ff404040",
  "text_active_color": "ffffffff",
  "text_inactive_color": "ffcccccc",
  "button_glyph_color": "ffffffff",
  "border_active_color": "ff5c8fb0",
  "border_inactive_color": "ff404040"
}
```

Every key has a hard-coded fallback and `load()` never fails the caller: a
missing, partial or malformed file leaves sane decorations and the window
manager still starts. The defaults follow labwc: 26px titlebar, 1px border,
26x26 buttons with no spacing and no padding, and — matching labwc's
`hide_maximized_window_titlebar: false` — the titlebar **stays** when a window is
maximized.

`Super+Shift+D` toggles decorations on and off. It is the only thing this
feature adds to `config.json`.

## Adding a titlebar button

The layout is in `DecorationGeometry.cpp`. Buttons are laid out **backwards
from the right edge** so that close ends up rightmost, and the pruning rule
(which button is dropped first when the bar is too narrow) follows labwc. The
glyphs are drawn as rectangles and lines in `DecorationRenderer.cpp` — v1 has no
icon assets, so minimize is a bar, maximize a square outline and close a cross.

## Known gaps

- **No touch or tablet support.** Touch reaches the titlebar the same way the
  mouse does (river's `Cursor.zig` calls `interact` for it), but the
  tablet-tool path does not call `interact` at all, and yarfwm binds neither
  `wl_touch` nor any tablet protocol. Do not claim stylus support for these
  buttons without testing it.
- **No edge-resize, no shadows** — see decision 7.
- **A CSD-only client keeps its own titlebar** and gets no bar from us. If a
  client that negotiates CSD anyway is seen doubling up, honouring
  `prefers_csd` as "no titlebar" too is the fix; it is recorded as a finding
  rather than papered over.
- **Fullscreen** gets no titlebar and no border. The decoration object is kept
  and simply not painted, so leaving fullscreen is instant.
