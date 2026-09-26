# Feature: child processes

## What it is

River runs its init script as a **process group leader**, and the script's
final `exec yarfwm` keeps that process: yarfwm *becomes* the group leader.
Verified live — yarfwm's process group id equals its own process id, and the
programs the script backgrounded before the exec carry that same group id:

    PID    PPID    PGID     SID STAT COMMAND
    701874  701865  701874  701874 S<sl+ river
    701939  701874  701939  701939 S<sl  yarfwm
    701942  701939  701939  701939 Z<    wbg <defunct>
    701943  701939  701939  701939 S<    fnott
    701944  701939  701939  701939 S<l   xfce4-panel

So the wallpaper, the notification daemon and the panel that river's init
script started are yarfwm's **children**. `ChildProcesses` owns the two things
that follow from that: collecting them when they exit, and stopping them when
the window manager stops.

## Justification

**A parent, and only a parent, may reap its own child.** River cannot collect
these processes — they are not river's children. Without a reap, every one of
them that dies while the window manager runs stays in state `Z` for the rest of
the session. This was observed live: a `wbg` killed at startup by the layer
shell ordering race sat as a zombie with yarfwm as its parent.

**Session teardown is not duplicated here.** River already sends SIGTERM to
this entire process group when it exits (river(1), CONFIGURATION: *"The
executable init file will be run as a process group leader ... On exit, river
will send SIGTERM to this process group."*). That is the compositor's job and
it reaches the children directly. The window manager must not reimplement
session management, and the protocol offers nothing to do it with: the
`river_window_manager_v1` interface has exactly seven requests, and the only
session-lifetime one, `exit_session`, *ends the session and exits the
compositor* — the opposite of what is wanted. It also explicitly must not be
used for ordinary window manager termination.

What river's group signal does not cover is a window manager that stops on its
own. Left alone, that leaves its children running, its group unclean, and the
next window manager finding the old programs still holding the session.

## What river's group signal does not cover

River's group signal arrives only when *river* exits. A window manager that
stops on its own leaves its children running, its group unclean, and the next
window manager finding the old programs still holding the session. River also
never re-runs the init script, so those programs are not restarted by river
either — the clean slate has to come from the window manager stopping them.

This part has **no labwc equivalent to copy, and is not claimed as parity**:

- labwc sends no signal to its autostart clients on exit. Its whole shutdown
  path is `session_run_script("shutdown")` (`src/config/session.c:342-353`),
  which runs the user's script asynchronously and returns; `server_finish()`
  then only removes signal sources and destroys Wayland clients.
- labwc needs none of this because it never owns those processes: it spawns
  through a `setsid()` double fork (`src/common/spawn.c:78-104`), so its
  autostart clients are reparented to init and can never become its zombies.
  yarfwm is in the opposite position — river's `exec` made the programs its
  children before its first instruction ran, and it cannot un-parent them.
- `SIGKILL` does not appear anywhere in labwc's source. The graceful-then-forceful
  escalation here follows systemd's defaults instead (`KillMode=control-group`,
  `KillSignal=SIGTERM`, SIGKILL on timeout), which is what labwc inherits by
  specifying neither in its unit.

## The pieces

- **Reaping** (`src/ChildProcesses.cpp`): SIGCHLD is **blocked** and delivered
  through a `signalfd` the main loop polls. Blocking rather than handling is
  what makes this safe in an event loop: a signal handler would run between
  arbitrary instructions, including inside libwayland's dispatch.

  This is labwc's mechanism, not a lookalike: labwc installs the handler with
  `wl_event_loop_add_signal` (`src/server.c:503`), and libwayland implements
  that function with `signalfd` (verified on this box: `nm -D
  /usr/lib/libwayland-server.so.0` shows `U signalfd@GLIBC_2.7`). That API is
  declared in `wayland-server-core.h` and is therefore unavailable to a river
  client, so the descriptor is created directly here and added to the same
  `poll()` set that already carries the Wayland socket and the key repeat
  timer. Same mechanism, same place in the loop.

  SIGTERM and SIGINT are blocked the same way, so a request to stop is handled
  where it is safe to handle it and the loop leaves through the normal
  teardown.
- **Discovery** (`src/SpawnedProcesses.cpp`): children are read from
  `/proc/<pid>/task/<tid>/children`. They cannot be recorded as they are
  created, because the inherited ones were created by a *different* process and
  there is no fork site in yarfwm to hook. The file lists exactly the set
  `waitpid(-1, ...)` would report.
- **Shutdown** (`ChildProcesses::terminate_children`): SIGTERM to every child,
  then reap, escalating to SIGKILL only for a child that ignores SIGTERM. The
  wait is bounded at one second. It runs at the very start of
  `Yarfwm::terminate()`, while the Wayland connection is still open, because
  the children are clients of this session and can only exit cleanly while the
  compositor is still answering them.

## The trap this feature has to avoid

**A blocked signal is inherited across `fork()` and, unlike a disposition, is
preserved across `execve()`.** Measured: a parent that blocked SIGCHLD and
exec'd `/bin/cat` produced a `cat` whose `/proc/self/status` showed
`SigBlk: 0000000000010000` — the SIGCHLD bit. Without care, every program
yarfwm starts would begin life with its own child exits silently blocked, and
a program that reaps from a SIGCHLD handler (a shell doing job control, a
terminal's process manager, `notify-send`) would silently accumulate zombies
for its whole lifetime.

Do not rely on the shell to clean this up. Bash resets the mask for a *simple*
command, but **keeps** the blocked bit for a pipeline and for `cmd; true`:

    simple command              SigBlk: 0000000000000000   (cleared)
    two commands, semicolon     SigBlk: 0000000000000000   (cleared)
    pipeline                    SigBlk: 0000000000010000   (KEPT)
    command then trailing true  SigBlk: 0000000000010000   (KEPT)

So a probe that execs a shell can miss the leak entirely, and a shell that runs
a pipeline still sees it. The fix is explicit: every child calls
`ChildProcesses::prepare_child_for_execution()` after `fork()` and before
`exec*()`, which clears the mask and restores `SIGPIPE` to its default. labwc
does the same thing for the same reason in `reset_signals_and_limits()`
(`src/common/spawn.c:15`), called in the child branch of every one of its fork
paths (`spawn.c:90,130,165,210,279`).

## Bugs fixed in this area

- **Inherited children were never reaped.** A program started by river's init
  script that died while the window manager ran stayed a zombie for the rest of
  the session (observed: `wbg` in state `Z`, parent yarfwm).
- **A stopping window manager left its children running.** With no group signal
  of its own, stopping yarfwm stranded the wallpaper, the notification daemon
  and the panel, so the next window manager started on top of stale programs.

## New features

- Child exits are collected as they happen, so no zombie survives its child.
- `SIGTERM`/`SIGINT` to the window manager now exits cleanly through the normal
  teardown instead of killing it at an arbitrary point.
- The session's programs are shut down with SIGTERM and reaped when the window
  manager stops, so every one of them can be started again afterwards.

## Verification

`~/.hermes/cache/scratch/yarfwm-childfix/probe.sh <binary> <tag>` reproduces the
real shape: an init script backgrounds a self-exiting program, a program that
gets killed mid-run, and a program left alive, then execs the window manager.
Against the pre-change binary it reports `VERDICT: FAIL` (both dead children
stay zombies; the surviving child outlives the window manager). Against the
patched binary every assertion passes and it exits 0. The probe only ever
signals process ids it captured itself.

The blocked-signal leak was measured directly with
`~/.hermes/cache/scratch/mask_reporter.cpp` and `sigmask_exec_probe2.cpp`.
