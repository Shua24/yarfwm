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
- Exits cleanly when river shuts down (exit code 0, no crash)

Not implemented yet:

- Keybindings — `river-xkb-bindings-v1` is vendored, but the binding code is a stub
- Seat focus — pointer enter, focus follows mouse, keyboard focus
- Window decorations — borders and focus rings are declared in the config but
  nothing draws them yet
- Config consumption — `config.json` is parsed and validated, but no feature
  reads the values yet

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

Install for development — binary to `/usr/local/bin/yarfwm`, plus the desktop
entry, session script and icon:

```
sudo ninja -C build install
```

A typical river setup backgrounds a wallpaper client and execs the window
manager from the river init script (`data/yarfwm-session` does the `exec` half).
Note that river closes layer surfaces immediately when no window manager holds
`river_layer_shell_v1`, so the window manager must be running for wallpapers and
bars to map.

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
data/                             desktop entry, session script, icon, default config (config.json.in)
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
| `View` | The policy core: the manage/render handshake, window and output tracking, floating cascade placement. Owns the `Output` objects. |
| `ViewEvents` | Listener trampolines for the window-management events. |
| `Output` | Per-output state: position, dimensions, layer-shell non-exclusive area, `set_default`. |
| `Seat` | Per-seat state; currently layer-shell focus tracking. |
| `Config` | JSON parsing and the first-startup config write. |
| `Keybind` | Not implemented yet. |

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
   "C with classes" style. The line style target is the Linux kernel's (tabs,
   80 columns) but the tree is not converted yet — treat it as the direction
   for new code.

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

Caveat: headless river has no seat (no input devices), so keyboard and focus
paths cannot be exercised there — those need a real session.

### Tests

GoogleTest is available on the development machine (1.18.0), and
`meson_options.txt` declares a `test` option — but **no `test()` call is wired
up yet**, so `-Dtest=true` currently does nothing. Contributions that add a test
suite should hook it up there. Until then, verification is manual and follows
the headless recipe above.

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

MIT — declared in `meson.build`. Note: **no `LICENSE` file exists in the
repository yet**; one should be added before distributing. The protocol XMLs
vendored under `protocols/` are MIT-licensed, © 2025 Isaac Freund; their
copyright headers are preserved in the files.
