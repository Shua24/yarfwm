# Panel / taskbar integration — an accepted limitation

**Summary: a taskbar or panel cannot restore a minimized yarfwm window, and this
cannot be fixed inside yarfwm.** The click is dropped by river itself. This
document records the finding, the evidence, and the decision (accepted and
documented, on 2026-09-25).

## The symptom

With `xfce4-panel` (and with `sfwbar`, verified the same way):

1. Minimize a window (`minimize_window`, unbound by default — bind it by hand).
2. Click the window's entry in the panel.
3. Nothing happens. The window stays hidden.

The window manager's own restore path works fine: `restore_minimized_window`
brings the window back. The failure is isolated to the panel entry point.

## Why it cannot be fixed in yarfwm

The chain, traced in river 0.4.8's source (`c4b5f706`):

1. A panel click sends `zwlr_foreign_toplevel_handle_v1.activate` (xfce4-panel
   via libxfce4windowing, `xfw-window-wayland.c:451`; sfwbar additionally sends
   `unset_minimized` first, `foreign-toplevel.c:171-172`).
2. wlroots turns that into `wlr_foreign_toplevel_handle_v1.events.request_activate`
   (`wlr_foreign_toplevel_management_v1.c:103-120`).
3. **River subscribes to no listener on that signal.** There is no
   `.events.request_*` subscription for the foreign-toplevel handle anywhere in
   river 0.4.8. The click dies inside river; nothing is forwarded to the window
   manager.
4. The window management protocol has no event that could carry it: neither the
   v5 nor v6 XML contains an `activate`, `restore`, or `unminimize` event.
   River's own xdg-activation path is an explicit no-op for windows:
   `'.window => {}, // TODO support xdg-activation with a rwm extension protocol`
   (`Server.zig:506`, still present on main as of 2026-09-25).

The same gap covers the other two panel interactions:

- **Panel minimize** (`set_minimized`): dropped the same way.
- **Minimized state display**: river never sets the `minimized` flag on its
  foreign-toplevel handles (only `setActivated` is driven), so a panel cannot
  even tell that a window is hidden — which is why the entry still looks
  clickable.

A live headless probe (control-proven) confirmed all of it: `activate` on a
hidden window produced **0 events and 0 log lines** in the window manager, the
window stayed hidden, and a passive foreign-toplevel watcher saw no state event.
The WM's own restore binding works in the same probe, which isolates the
failure to the panel entry point. Evidence:
`.hermes/notes/2026-09-25-panel-recovery/` (river-activate-trace, live-probe,
panel-request-trace).

## What still works

- `restore_minimized_window` (`Super+Shift+M`) — brings back the most recently
  minimized hierarchy, raises it to the front, focuses it, and brings its
  virtual desktop forward if it was minimized on another one.
- `minimize_window` (`Super+M`).

Both are bound in the default config, so a minimized window is always
recoverable from the keyboard.

## Paths considered

| Path | Verdict |
|---|---|
| Window-manager-side fix | Impossible — no protocol event exists to receive the click. |
| River-side patch (forward the request / protocol extension) | A river change, outside this project. |
| Out-of-band IPC (a socket a helper calls; att_wm style) | Possible in principle, but xfce4-panel's tasklist click is hardcoded to `activate` — it would need a custom launcher, not panel configuration. Out of scope. |
| **Accept and document** | **Chosen (Carolus, 2026-09-25).** |

## Related: desktops are invisible to panels

For the same class of reason, yarfwm's five virtual desktops cannot be shown in
a panel: river implements no workspace protocol (`ext-workspace-v1` is absent)
and a window manager client cannot create globals. Desktop switching works
through key bindings with the notify-send indicator (see
`docs/features/keyboard.md`).
