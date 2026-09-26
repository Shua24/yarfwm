#ifndef SPAWNED_PROCESSES_HPP
#define SPAWNED_PROCESSES_HPP

#include <sys/types.h>
#include <vector>

// Reading the child processes of the running window manager.
//
// The children cannot be recorded as they are created: the wallpaper, the
// notification daemon and the panel are started by river's init script, which
// is a different process, and only `exec yarfwm` makes them yarfwm's. There is
// no fork site in this program to hook, so the kernel's own bookkeeping is
// read instead.
//
// /proc/<pid>/task/<tid>/children lists the children of one thread, space
// separated, and it is exactly the set waitpid(-1, ...) would report (the
// children of a thread that have not been reaped). The window manager is
// single threaded, so every child it has appears under its one thread. The
// file can disappear or change under us, so every read failure is treated as
// "no children", never as an error.
//
// The identifiers are written into the caller's vector rather than returned:
// returning a container by value trips -Waggregate-return, which this project
// keeps enabled (the same reason decoration_settings_from takes an
// out-parameter). The vector is cleared first.
void read_child_process_ids(std::vector<pid_t> *child_process_ids);

#endif // SPAWNED_PROCESSES_HPP
