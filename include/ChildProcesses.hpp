#ifndef CHILD_PROCESSES_HPP
#define CHILD_PROCESSES_HPP

#include <sys/types.h>

// The child processes this window manager must account for.
//
// River runs its init script as a process group leader, and the script's
// final `exec yarfwm` keeps that process: yarfwm *becomes* the group leader
// (verified live: yarfwm's process group id equals its own process id, and
// the programs the script backgrounded carry that same group id). So the
// wallpaper, notification daemon and panel the init script started are
// yarfwm's children, and every one of them that dies while yarfwm runs
// leaves a zombie behind, because only a parent may reap its own child.
// Observed live: a wbg killed at startup by the layer shell ordering race sat
// in state Z with yarfwm as its parent for the rest of the session.
//
// Session teardown is NOT duplicated here. River already sends SIGTERM to
// this whole process group when it exits (river(1), CONFIGURATION), which is
// the compositor's job and reaches the children directly. The window manager
// must not reimplement session management; it only has to do the part river
// cannot:
//
//   1. Reaping. A blocked SIGCHLD feeds a signalfd the main loop polls, so a
//      child exit is collected as soon as it happens and no zombie survives
//      it. Blocking the signal instead of handling it is what makes this safe
//      inside a Wayland event loop: a handler would run between arbitrary
//      instructions, including inside libwayland's dispatch. labwc reaches
//      the same place with wl_event_loop_add_signal (src/server.c:503), which
//      is server-side only and unavailable to a river client, so the
//      signalfd is this side's equivalent.
//
//   2. Graceful shutdown. River's group signal only arrives when river exits.
//      A window manager that stops on its own leaves its children running and
//      its group unclean, and the next window manager then finds the old
//      programs still holding the session. On the way out yarfwm therefore
//      asks each child to stop with SIGTERM, reaps it, and escalates to
//      SIGKILL only for a child that ignores SIGTERM, so the group is left
//      with no zombie and every program can be started again. SIGTERM is the
//      signal labwc uses for the processes it starts (src/common/nag.c:59).
//
// The children are discovered from /proc rather than recorded at each fork
// site, because the inherited ones were created by a different process and
// there is no fork site in yarfwm to hook.
//
// A blocked signal is inherited across fork() and, unlike a disposition, is
// preserved across execve() (verified: a child that exec'd /bin/cat showed
// SigBlk 0x10000, the SIGCHLD bit). Every child must therefore call
// prepare_child_for_execution() before it execs, or it starts with its own
// child exits silently blocked. Do not rely on a shell to clean this up: bash
// clears the mask for a simple command but keeps it for a pipeline and for
// `cmd; true`. labwc solves this the same way, in reset_signals_and_limits()
// (src/common/spawn.c:15).
class ChildProcesses
{
      public:
	ChildProcesses();
	~ChildProcesses();

	// Block the signals and create the descriptor that reports them.
	// Returns false when the kernel refuses either call; reaping and
	// shutdown still work, only the prompt collection of exits is lost,
	// so this never fails startup.
	bool initialize();
	void terminate();

	// Descriptor to poll for readability, or -1 when unavailable.
	int file_descriptor() const;

	// Collect every child that has exited, so none is left a zombie.
	// Called when the descriptor becomes readable, and again from the
	// shutdown path.
	void reap_exited_children();

	// Ask every remaining child to terminate, then reap it. Bounded: a
	// child that ignores SIGTERM delays this by one second, never longer.
	void terminate_children();

	// True once SIGTERM or SIGINT has been received, so the main loop can
	// leave cleanly instead of dying between two instructions.
	bool shutdown_requested() const;

	// Restore the signal state a freshly exec'd program must start with.
	// Call this in the child, after fork() and before exec*().
	static void prepare_child_for_execution();

      private:
	// Read the exit and shutdown signals reported by the descriptor.
	void consume_signal_events();

	// Send one signal to every child, then reap what it killed. Returns
	// the number of children that were still alive when it ran.
	int signal_children(int signal_number);

	int signal_descriptor;

	// Whether this object is the one that blocked the signals. The mask
	// is process-wide state, so restoring it on teardown is only correct
	// if we changed it.
	bool signal_mask_blocked;

	bool shutdown_flag;
};

#endif // CHILD_PROCESSES_HPP
