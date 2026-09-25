# Yarfwm — Yet Another River Floating Window Manager

A floating window manager for the [river](https://codeberg.org/river/river) Wayland
compositor, written in C++17.

> Critical note: The packaging is still planned, as the project is still not fully featured yet. It will be available on the
AUR once it's in a fully featured state. It is still a work in progress, in other words.

River delegates window management to a separate client that speaks the
`river-window-management-v1` protocol: the window manager decides where windows go,
what sizes they get, and how layer surfaces (bars, wallpapers, launchers) are
handled. Yarfwm is that client. It connects to river over Wayland, receives
windows, outputs and seats, and applies a floating placement policy.

The project aims to be packageable (target: the AUR) and to serve as a readable
reference for developers writing their own river window manager.

## Status

Pre-1.0, under active development. Verified live on 2026-09-23 against river
0.4.8 in a headless session:

- Builds clean from a fresh checkout (`meson setup build && ninja -C build` → exit 0)
- Manages windows: proposes sizes, tracks configures, maps and releases windows
  (verified with `foot`)
- Supports layer shell: wallpaper clients and bars map correctly, including
  exclusive-zone tracking (verified with `swaybg`, `wbg` and `waybar`)
- Keyboard bindings: all 45 binds in the default config register per seat
  (nothing is skipped), and spawn / close / directional focus / focus-previous /
  quit / exit-session fire on injected key events (verified with `wtype`; see
  `docs/keybinds.md`)
- Keyboard pointer movement: the four `move_pointer_*` binds warp the pointer
  through `river_seat_v1.pointer_warp`, one 32px step per press, held keys
  repeating (verified with injected key events against a parked virtual
  pointer; see `docs/keybinds.md`)
- Key repeat: a held focus binding repeats (verified with `wtype` holding
  `Super+Right` for 1.5s → 29 repeats; a held `spawn` still fires once)
- Pointer focus: click-to-focus and focus-follows-mouse both focus the window
  under a virtual pointer, and the setting is honoured — with
  `focus_follows_mouse: false` a motion focuses nothing (verified with
  `zwlr_virtual_pointer_v1`)
- Lock screen: after a lock surface releases the keyboard the focused window is
  re-focused, so typing resumes without a click (verified with `swaylock`;
  without the fix the same probe leaves the keyboard unfocused)
- Closing the last window no longer crashes: the window is dropped from the
  tracked list before the seat picks a fallback, so the fallback can never be
  the dying window (before the fix: SIGSEGV, exit 139, verified with a control)
- A window dying while an overlay holds exclusive keyboard focus no longer
  strands the keyboard: the recorded fallback survives the lock and is applied
  when the overlay lets go (before the fix: no focus request at all, verified
  with a control)
- Exits cleanly when river shuts down (exit code 0, no crash)

Not implemented yet:

- Window decorations — yarfwm draws none and asks for none: river's default
  (client-side decorations) applies, and honoring a server-side decoration hint
  would mean drawing the decoration, which needs a renderer yarfwm does not
  have. Compositor-drawn borders (`set_borders`) are a candidate for a later
  batch.

## Requirements

| Dependency | Minimum | Notes |
|---|---|---|
| meson | 0.59.0 | |
| ninja | — | |
| C++17 compiler | — | built with `warning_level=2` plus ~30 extra warning flags |
| wayland-client | 1.22.90 | also provides `wayland-scanner`, used at build time |
| wayland-cursor | — | optional, detected at configure time |
| xkbcommon | — | |
| pixman-1 | — | |
| jsoncpp | — | required in practice — see the note below |

Verified with: Arch Linux, GCC 16.2.1, meson 1.12.0, ninja 1.13.2, wayland 1.26.0,
river 0.4.8.

On Arch:

```
sudo pacman -S meson ninja gcc wayland jsoncpp libxkbcommon pixman
```

> **jsoncpp note.** `meson.build` declares jsoncpp as optional and warns about a
> "built-in JSON parser" fallback. That fallback does not exist yet: without
> jsoncpp the build fails at link time. Install it.

## Quick start

```
git clone https://github.com/Shua24/yarfwm.git
cd yarfwm
meson setup build
ninja -C build
```

Run it inside a river session — yarfwm must be the only window manager on the
connection (river refuses a second one):

```
./build/src/yarfwm
```

Install for development — binary to `/usr/local/bin/yarfwm`, plus the icon:

```
sudo ninja -C build install
```

A typical river setup backgrounds a wallpaper client and execs the window
manager from the river init script. Note that river closes layer surfaces
immediately when no window manager holds `river_layer_shell_v1`, so the window
manager must be running for wallpapers and bars to map.

## Starting Yarfwm

Yarfwm is a window manager *client*: it speaks
`river-window-management-v1` to river, and river is the compositor. Yarfwm on
its own, with no compositor, has nothing to talk to. Start it from river's init
script, which is how a normal login does it.

From a tty, run river with Yarfwm as its window manager:

```
river -c yarfwm
```

Or let river's own init script start it, which is what a normal login does. The
init script at `$XDG_CONFIG_HOME/river/init` (or `~/.config/river/init`) is
executed by river on startup:

```sh
#!/bin/sh
wbg "$HOME/Pictures/Wallies/wallpaper.jpg" &
waybar &
exec yarfwm
```

`exec yarfwm` works here because river is already running and already
advertising `river_window_manager_v1` — Yarfwm connects to it as a client.

### Why there is no desktop entry

Yarfwm ships no `.desktop` file, in either `applications/` or
`wayland-sessions/`, because neither entry point matches how river works.

River's own mechanism for choosing a window manager is the init script: river
executes `$XDG_CONFIG_HOME/river/init` (or `~/.config/river/init`) on startup
and expects it to `exec` the window manager. That is the supported way to start
Yarfwm, and it is what a normal login does.

- A **display manager session entry** would have to be `Exec=river -c yarfwm`.
  River's `-c` option exists to *override* the default search paths for the init
  executable — using it in a session file bypasses the user's own
  `~/.config/river/init`, so their wallpaper, bar and per-machine setup would
  silently stop running. River's own packaged session entry is a plain
  `Exec=river`, which runs the init script as intended.
- An **application entry** would have to be `Exec=yarfwm`, which starts a window
  manager client with no compositor to connect to. It cannot work by
  construction.

Start Yarfwm from the river init script, or by running `river -c yarfwm` from a
tty when you deliberately want to skip the init script.

The configuration file lives at `$HOME/.config/yarfwm/config.json`. It is
written from a default embedded in the binary on first startup, and never
overwritten afterwards. Lookup order: `./config.json` in the working directory,
then `$HOME/.config/yarfwm/config.json`.

## Repository layout

```
meson.build                       project definition, dependencies, warning flags
meson_options.txt                 build options (see "Tests" below)
river-window-management-v1.xml    core protocol definition — the single source of truth
protocols/                        layer-shell + xkb-bindings protocol XMLs and their build rules
src/                              implementation
include/                          headers (mirrors src/)
data/                             icon and default config (config.json.in)
docs/                             developer notes (keybindings)
.clang-format                     the code style, applied with clang-format -i
build/                            meson output directory — generated, never edit, never commit
```

Protocol headers and sources are **generated at build time** by `wayland-scanner`
into `build/protocols/` from the XML files. Nothing generated is checked in; a
fresh checkout always builds from the XMLs alone. If you need to change protocol
behavior, edit the XML.

## Architecture

`main.cpp` creates a single `Yarfwm` object, which owns everything and wires the
pieces together in this order: config → server → display → roundtrip → seat →
view → keybind. Dependency direction, top to bottom:

| Unit | Role |
|---|---|
| `Server` | Wayland connection and registry. Owns the `wl_registry` proxy. |
| `Display` | Registry listener. Binds `river_window_manager_v1` (clamped to the protocol version) and delegates `river_layer_shell_v1` to `LayerShell`. |
| `LayerShell` | Owns the layer shell global; creates per-output and per-seat state objects. |
| `View` | The policy core: window and output tracking, floating cascade placement. Owns the `Output` objects. The manage/render handshake lives in `ViewSequences`. |
| `ViewEvents` | Listener trampolines for the window-management events. |
| `ViewActions` | The View requests the event handlers and the keybind engine make (close, manage, focus queries, exit session). |
| `ViewSequences` | The two sequence handlers, `window_manager_manage_start` and `window_manager_render_start`. |
| `Output` | Per-output state: position, dimensions, layer-shell non-exclusive area, `set_default`. |
| `Seat` | Per-seat state: keyboard focus and focus history, layer-shell focus tracking. |
| `Config` | JSON parsing and the first-startup config write. |
| `Keybind` | Keyboard bindings: parses `keybinds`, registers one binding object per bind per seat, drives held-key repeat. Action dispatch lives in `KeybindActions`, config parsing in `KeybindParse`. See `docs/keybinds.md`. |
| `KeybindParse` | The parsing half of the engine: action names, `args`, keysym and modifier resolution. |
| `KeybindActions` | What a binding does when it fires: `perform` and the `spawn` fork/exec. |
| `RepeatTimer` | The timerfd behind held-key repeat; the main loop polls it next to the Wayland socket. |

## Invariants (read before patching)

1. **Every listener slot must be non-NULL.** libwayland aborts the process
   (SIGABRT, exit 134) when an event arrives for a NULL slot. Windows send
   app_id/title immediately, so a half-filled listener kills the WM on the first
   window. Populate every slot, even with a no-op.
2. **The manage/render loop is mandatory and strictly ordered.**
   `manage_start` → manage-only requests → `manage_finish`; `render_start` →
   render-only requests → `render_finish`. Skipping a finish stalls river's loop.
3. **The protocol in the repo is v6, but river 0.4.8 enforces v5 sequencing.**
   The client binds `min(advertised, 6)`, so it binds at 5 today. In v5,
   `set_position` and `show` are render-sequence-only, while
   `propose_dimensions`, `set_capabilities` and `set_default` are
   manage-sequence-only. Never send v6-only members (`op_start_touch`,
   `op_end_touch` and the touch events) until river advertises v6.
4. **Wayland proxy ownership.** The `wl_registry` belongs to `Server`; other
   units borrow it. Destroying a proxy twice segfaults at shutdown — this
   happened with the registry and is now guarded by convention.
5. **Style constraints.** Every file stays under 500 lines. Identifiers use full
   words, no abbreviations (`shared_memory`, not `shm`). Code is C++17 in a
   "C with classes" style, formatted in the Linux kernel's line style (tabs,
   80 columns). `.clang-format` at the repository root is the single source of
   truth — run `clang-format -i` on the files you touch.

## Developing

### Testing headless

River runs headless, so you can exercise the WM without touching your session:

```
export XDG_RUNTIME_DIR=/tmp/yarfwm-test
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
river -no-xwayland -log-level debug &

WAYLAND_DISPLAY=wayland-1 ./build/src/yarfwm &
WAYLAND_DISPLAY=wayland-1 foot    # any client
```

Read river's `debug(wm):` log lines: `window 'foot' mapped`,
`sent 1 tracked configure(s)`, `layer surface 'wallpaper' mapped`. The
non-exclusive area (what a bar leaves for windows) appears in the WM's stderr as
`Yarfwm: non-exclusive area x,y WxH`.

Note on input: headless river still advertises a seat (`wl_seat` plus
`river_xkb_bindings_v1`), so keyboard bindings can be exercised there by
injecting synthetic key events with a virtual-keyboard client such as `wtype`
(not packaged on this machine; it builds from source in a scratch directory).
Pointer paths work headless too: river advertises
`zwlr_virtual_pointer_manager_v1`, and a small injector built against
`zwlr-virtual-pointer-unstable-v1.xml` can park or click the pointer at any
position, which is how click-to-focus, focus-follows-mouse and the keyboard
pointer warp were verified.

### Tests

`meson_options.txt` declares a `test` option; with it enabled the unit suite
builds and runs:

```
meson setup build -Dtest=true
ninja -C build
meson test -C build
```

Five suites run without a Wayland connection: `placement` and
`placement_helpers` (cascade step, directional scoring, desktop wrap,
dimension-hint clamping), `config` (parsing, removed-key tolerance, the
first-start default write), `keybind_parse` (keysym folding, modifier masks,
action names) and `view_lock` (the session lock guard). Everything else is
verified with the headless recipe above.

### Adding a protocol

1. Vendor the XML under `protocols/`.
2. Add a `client-header` and a `private-code` `custom_target` pair in
   `protocols/meson.build`.
3. List both targets in the `executable()` sources in `src/meson.build`.

Never check in generated protocol files.

## Contributing

- Open an issue before non-trivial changes so the approach can be agreed on first.
- Keep changes focused: one concern per pull request.
- Before pushing, verify a fresh-tree build is green and introduces no new
  warnings:

  ```
  rm -rf build && meson setup build && ninja -C build
  ```

- Follow the invariants and style constraints above; match the surrounding code.
- Commit messages: imperative mood, concise subject line, explain the why in the
  body.

## License

GPL-2.0 — the full text is in `LICENSE` at the repository root; `meson.build`
declares the same. The protocol XMLs vendored under `protocols/` are
MIT-licensed, © 2025 Isaac Freund; their copyright headers are preserved in the
files, and they remain under their own terms.
