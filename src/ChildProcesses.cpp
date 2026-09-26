#include "ChildProcesses.hpp"
#include "SpawnedProcesses.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

// How long a child is given to honour SIGTERM before SIGKILL is used. This
// only bounds the worst case; a child that exits promptly costs nothing.
static const int graceful_shutdown_milliseconds = 1000;

// Poll interval while waiting for the children to go away.
static const int shutdown_poll_milliseconds = 50;

ChildProcesses::ChildProcesses()
    : signal_descriptor(-1), signal_mask_blocked(false), shutdown_flag(false)
{
}

ChildProcesses::~ChildProcesses() { terminate(); }

bool ChildProcesses::initialize()
{
	if (signal_descriptor >= 0) {
		return true;
	}

	// SIGCHLD so child exits can be collected, SIGTERM and SIGINT so a
	// request to stop is handled where it is safe to handle it. All three
	// are blocked and delivered through the descriptor: a signal handler
	// would run between arbitrary instructions, including inside
	// libwayland's dispatch, which is exactly what must not happen in an
	// event loop that owns protocol state.
	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals, SIGCHLD);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGINT);

	if (sigprocmask(SIG_BLOCK, &signals, nullptr) != 0) {
		std::fprintf(stderr,
			     "Yarfwm: failed to block the child signals: %s\n",
			     std::strerror(errno));
		return false;
	}
	signal_mask_blocked = true;

	signal_descriptor = signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
	if (signal_descriptor < 0) {
		std::fprintf(stderr,
			     "Yarfwm: failed to create the child signal "
			     "descriptor: %s\n",
			     std::strerror(errno));
		// The signals stay blocked. They are no longer delivered to a
		// default action, so a stray SIGTERM cannot kill the window
		// manager at an arbitrary point; unblock them again so the
		// process keeps its normal behaviour instead.
		sigprocmask(SIG_UNBLOCK, &signals, nullptr);
		signal_mask_blocked = false;
		return false;
	}

	// A child that exited before this ran is already a zombie, and the
	// SIGCHLD that announced it is pending. signalfd reports pending
	// signals as soon as it is read, so nothing needs to be done here
	// beyond the first reap the main loop performs.
	return true;
}

void ChildProcesses::terminate()
{
	if (signal_descriptor >= 0) {
		close(signal_descriptor);
		signal_descriptor = -1;
	}

	if (signal_mask_blocked) {
		sigset_t signals;
		sigemptyset(&signals);
		sigaddset(&signals, SIGCHLD);
		sigaddset(&signals, SIGTERM);
		sigaddset(&signals, SIGINT);
		sigprocmask(SIG_UNBLOCK, &signals, nullptr);
		signal_mask_blocked = false;
	}
}

int ChildProcesses::file_descriptor() const { return signal_descriptor; }

bool ChildProcesses::shutdown_requested() const { return shutdown_flag; }

void ChildProcesses::consume_signal_events()
{
	if (signal_descriptor < 0) {
		return;
	}

	// The descriptor stays readable while events remain, so read until it
	// would block. One read returns one signal.
	struct signalfd_siginfo information;
	while (read(signal_descriptor, &information, sizeof(information)) ==
	       static_cast<ssize_t>(sizeof(information))) {
		if (information.ssi_signo == SIGTERM ||
		    information.ssi_signo == SIGINT) {
			shutdown_flag = true;
		}
	}
}

void ChildProcesses::reap_exited_children()
{
	// CONSTRAINT for future changes: this collects with waitpid(-1, ...),
	// so it must never be called between a fork() and the blocking
	// waitpid(pid, ...) that reaps that same child. The two spawn sites
	// (KeybindActions::spawn_command,
	// ViewDesktops::announce_desktop_switch) fork and then wait for their
	// intermediate child as straight-line code with no event-loop turn in
	// between, which is what keeps this safe. Calling it from inside that
	// window would reap the intermediate child first and leave the blocking
	// waitpid with nothing to collect.
	//
	// This is also the only place the descriptor is drained, so the
	// shutdown request is picked up by the same call.
	consume_signal_events();

	int status = 0;
	while (waitpid(-1, &status, WNOHANG) > 0) {
		// Nothing to do with the status: this window manager does not
		// supervise what its children do, it only has to stop them
		// becoming zombies. WNOHANG keeps a second call from blocking
		// when a child is alive and simply has not exited.
	}
}

int ChildProcesses::signal_children(int signal_number)
{
	// Dynamic memory: one vector, reused by both callers through this
	// function, so no per-signal allocation happens in a shutdown path.
	// A pid is captured before the signal is sent: once a child is reaped
	// its pid is gone, and a signal sent to a reused pid would hit an
	// unrelated process.
	std::vector<pid_t> children;
	read_child_process_ids(&children);

	for (std::size_t index = 0; index < children.size(); index++) {
		if (kill(children[index], signal_number) != 0 &&
		    errno != ESRCH) {
			std::fprintf(stderr,
				     "Yarfwm: failed to signal child %d: %s\n",
				     static_cast<int>(children[index]),
				     std::strerror(errno));
		}
	}

	return static_cast<int>(children.size());
}

void ChildProcesses::terminate_children()
{
	std::vector<pid_t> children;

	int remaining = signal_children(SIGTERM);

	int waited_milliseconds = 0;
	while (remaining > 0 &&
	       waited_milliseconds < graceful_shutdown_milliseconds) {
		// Reaping here is what actually removes the zombie; the signal
		// only asks the child to stop.
		reap_exited_children();

		read_child_process_ids(&children);
		remaining = static_cast<int>(children.size());
		if (remaining == 0) {
			break;
		}

		struct timespec pause = {};
		pause.tv_sec = 0;
		pause.tv_nsec =
		    static_cast<long>(shutdown_poll_milliseconds) * 1000000L;
		nanosleep(&pause, nullptr);
		waited_milliseconds += shutdown_poll_milliseconds;
	}

	// Anything that ignored SIGTERM is forced, so shutdown cannot hang on a
	// program that refuses to leave.
	if (remaining > 0) {
		signal_children(SIGKILL);
		reap_exited_children();
	}
}

void ChildProcesses::prepare_child_for_execution()
{
	// The blocked signals are inherited across fork() and preserved across
	// execve(), so a child would start with its own child exits silently
	// blocked unless the mask is cleared here. labwc restores the mask the
	// same way before it execs (src/common/spawn.c
	// reset_signals_and_limits).
	sigset_t signals;
	sigemptyset(&signals);
	sigprocmask(SIG_SETMASK, &signals, nullptr);

	// SIGPIPE is not blocked by this object, but it is ignored by default
	// in some environments and would break the shell pipelines a spawned
	// program runs. Restoring it here keeps every fork site honest.
	signal(SIGPIPE, SIG_DFL);
}
