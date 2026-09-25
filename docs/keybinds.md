# Keybindings

How yarfwm's keyboard bindings work, what the default config binds, and which
actions are live today.

## How a binding works

Bindings live in the `keybinds` array of `config.json`. The template is
`data/config.json.in`; it is embedded into the binary at build time and written
to `~/.config/yarfwm/config.json` on first start (see `src/Config.cpp`). Each
entry has a `modifiers` array, a `key` name, and an `action`:

```json
{ "modifiers": ["Super", "Shift"], "key": "L", "action": "spawn",
  "args": ["hyprlock"] }
```

- `modifiers` accepts `Super` (also spelled `Logo`), `Alt`, `Ctrl` (also
  spelled `Control`), `Shift`.
  They are OR-ed into one mask and river requires an **exact** match: a binding
  declared `Super` fires only when Super is held and nothing else.
- `key` is an xkbcommon keysym name (`Return`, `Slash`, `Page_Down`, `A`).
  Single letters are matched case-insensitively; see the note below.
- `action` is one of the names in the tables below.
- `args` is the argument vector for `spawn` (program first, then its
  arguments). The resize actions read their percentage from it too.

Keysyms are matched against the keyboard's **base layer**, "as if modifiers
didn't change keysyms": river reports `Super+Shift+r` as keysym `r`, not `R`.
yarfwm therefore registers every ASCII A-Z binding in its lowercase form, so
`Super+Shift+L` works while the config spells the key `L`.

Bindings are registered per seat (river scopes a binding to a seat) and only
`enable` inside a manage sequence, which is when river accepts that request.

A run with the default config logs, at startup:

```
Yarfwm: 45 key bindings parsed
Yarfwm: registered 45/45 key bindings on a seat
```

Every entry in the default config names an implemented action, so nothing is
skipped. An unknown action name in a hand-edited config is reported once
(`Yarfwm: keybind action not implemented yet, skipping: <name>`) and its key is
left alone, so it still reaches the focused window.

### Held keys (repeat)

river reports a press and a release and leaves repeating to the window manager,
so yarfwm runs its own timer (`src/RepeatTimer.cpp`, a timerfd the main loop
polls next to the Wayland socket) and re-runs the action while the key is held.
The first run happens on the press itself; the timer only produces the repeats.

Which actions repeat:

- `focus_window_left` / `_right` / `_up` / `_down` repeat by default, so a held
  key walks the focus across the desktop.
- `move_pointer_left` / `_right` / `_up` / `_down` repeat by default, so a held
  key walks the pointer.
- Everything else runs once per press. Spawning a program or closing a window
  on every repeat tick would be wrong.
- A per-binding `"repeat": true` or `"repeat": false` in `config.json` wins over
  the default, for both directions.

An explicit `"repeat"` field looks like this (the default config carries
`"repeat": false` on `close_window`, which is already the default for it):

```json
{ "modifiers": ["Super"], "key": "X", "action": "close_window", "repeat": false }
```

Pressing any other key while a repeat is running stops it (river sends
`stop_repeat`), as does releasing the bound key.

## Actions

### Implemented

Every action below is implemented and registers when the default config names
it. `minimize_window` and `restore_minimized_window` are implemented but not
bound by the default config; bind them by hand if wanted.

| Action | Effect |
|---|---|
| `spawn` | Fork and exec `args` (double fork; the child is reparented to init). |
| `close_window` | Ask river to close the focused window. |
| `quit` | Stop the window manager and exit. River does **not** exit when its window manager disconnects, so this leaves the compositor running with no window manager; use `exit_session` to end the session. |
| `exit_session` | Ask river to end the Wayland session and exit the compositor (`river_window_manager_v1.exit_session`). Every client in the session is disconnected, including this window manager. |
| `focus_window_left` / `_right` / `_up` / `_down` | Move focus to the nearest window in that direction, measured centre to centre. |
| `focus_window_previous` | Return focus to the previously focused window. |
| `move_window_left` / `_right` / `_up` / `_down` | Move the focused window by one 32px step in that direction. |
| `move_pointer_left` / `_right` / `_up` / `_down` | Move the pointer by one 32px step in that direction (`river_seat_v1.pointer_warp`). River clamps the target into the outputs, so a warp past the screen edge stops at the edge. |
| `focus_desktop_next` / `_previous` | Switch to the next/previous virtual desktop. |
| `move_window_to_desktop_next` / `_previous` | Send the focused window to the next/previous virtual desktop. |
| `toggle_always_on_top` | Keep the focused window above the others (`place_top`/`place_bottom`). |
| `toggle_maximize` | Toggle the focused window's maximized state. |
| `fullscreen_window` | Toggle fullscreen for the focused window, on the output it mostly sits on. |
| `fit_to_output` | Resize the focused window to the placement area (the output minus bars/docks). |
| `center_window` | Centre the focused window in the placement area. |
| `center_all_windows` | Centre every window in the placement area. |
| `set_window_width` | Grow/shrink the focused window's width by the percentage in `args` (`"-10%"`, `"+10%"`). |
| `set_window_height` | Same for height. |
| `minimize_window` | Hide the focused window (the protocol's own answer to minimize). Not bound by default. |
| `restore_minimized_window` | Show the most recently minimized window again. Not bound by default. |

`quit` and `exit_session` are deliberately separate. River's protocol asks that
`exit_session` be sent only when the user explicitly wants the session to end,
not on ordinary window manager termination, so `quit` keeps its meaning as "stop
the window manager" and the session-ending request has its own binding
(`Super+Shift+E` by default). Under a display manager the distinction is what
gets you out: `quit` on its own leaves river running with no window manager and
a blank screen.

When the focused window disappears, focus falls back to the previously focused
survivor, then to any surviving window; only when no window is left does the
seat clear the keyboard focus.

The fallback is computed **after** the dying window is dropped from the tracked
window list, so the fallback can never be the dying window itself (focusing a
destroyed proxy is a crash).

`focus_window_previous` is honest about what it has: the slot only ever points
at a live window that is not the focused one, so it either moves the focus back
or logs `focus_window_previous: no previous window` — it never silently does
nothing. A closed previous window is dropped rather than remembered.

When a lock surface takes the keyboard (a lock screen) and then releases it,
the focus is re-issued to the window the user was on, so the keyboard comes back
without a click. River drops the focus on unlock without the window manager's
own record changing, so without that re-issue the seat would sit with no window
focused.

While a layer surface holds **exclusive** keyboard focus, a recorded focus
change is left pending rather than discarded — river ignores focus requests
until the surface lets go, and the first manage sequence after it does applies
the pending change. This matters when the focused window dies in the meantime:
the fallback survives the lock and the keyboard comes back to a real window.

Pointer events: with `input.focus_follows_mouse` true (the default) moving the
pointer into a window focuses it; clicking a window focuses it regardless of
that setting. The `move_pointer_*` actions move the pointer itself, which can
focus a window under it through that same path.

A close request is a request, not a command: a window may refuse it (terminal
emulators ask for confirmation when a process is still running). Pressing the
close binding again re-sends the request to the focused window.

## Changes to the inherited default config

The default config (`data/config.json.in`) came from a tiling window manager's
session. Batch 2 renamed its tiling verbs to floating equivalents and deleted
the binds that have no meaning in a floating window manager, because the JSON
format has no comments and could not explain itself. The protocol-completion
pass then deleted the two binds whose features river's protocol cannot carry
out. This file is that record.

### Renamed (15)

The key and modifiers were kept; only the action name changed.

| Was (tiling) | Now (floating) |
|---|---|
| `center_column` | `center_window` |
| `center_visible_columns` | `center_all_windows` |
| `expand_column_to_available_width` | `fit_to_output` |
| `focus_column_left` | `focus_window_left` |
| `focus_column_right` | `focus_window_right` |
| `focus_workspace_down` | `focus_desktop_next` |
| `focus_workspace_up` | `focus_desktop_previous` |
| `maximize_column` | `toggle_maximize` |
| `move_column_left` | `move_window_left` |
| `move_column_right` | `move_window_right` |
| `move_column_to_workspace_down` | `move_window_to_desktop_next` |
| `move_column_to_workspace_up` | `move_window_to_desktop_previous` |
| `set_column_width` | `set_window_width` |
| `switch_focus_between_floating_and_tiling` | `focus_window_previous` |
| `toggle_window_floating` | `toggle_always_on_top` |

### Deleted (7 binds)

These actions have no equivalent in the river window management protocol. The
first five were deleted in Batch 2; the last two in the protocol-completion
pass, when the config was brought in line with the constraint that every key
must be consumable under the protocol. Deleting the bind leaves the key free
for the client instead of consuming it.

| Action | Was bound to | Why |
|---|---|---|
| `move_workspace_up` | `Super+Shift+Page_Up`, `Super+Shift+I` | no workspaces in the protocol |
| `move_workspace_down` | `Super+Shift+Page_Down`, `Super+Shift+U` | no workspaces in the protocol |
| `power_off_monitors` | `Super+Shift+P` | no monitor power control in the protocol |
| `toggle_expose` | `Super+O` | an expose grid needs a picture of every window; the protocol has no screenshot, thumbnail or scaling request |
| `show_hotkey_overlay` | `Super+Slash` | the overlay needs a window-manager-owned renderer; yarfwm has none |

Net effect: 47 inherited binds became 45 — 15 renames in place, 7 deleted, 5
added (`Super+Shift+E` for `exit_session` when that action landed, and the four
`move_pointer_*` binds when the keyboard pointer warp landed).
