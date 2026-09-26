#include "Yarfwm.hpp"
#include <cstdio>
#include <poll.h>

bool Yarfwm::initialize(int argc, char *argv[])
{
	// Dynamic memory: config is heap-allocated during parsing from
	// config.json, then copied into fixed Config members; raw config object
	// freed at end of load.
	(void)argc;
	(void)argv;

	// First, because the children already exist: river's init script
	// backgrounded them and then exec'd this process, so they are this
	// process's children from the first instruction. Blocking SIGCHLD and
	// opening the descriptor before anything else means no exit during
	// startup can be missed, and a child that died before this ran is still
	// reported, because a blocked signal stays pending.
	if (!child_processes.initialize()) {
		std::fprintf(stderr,
			     "Yarfwm: child process reaping is unavailable; "
			     "continuing without it\n");
	}

	if (!config.load("config.json")) {
		child_processes.terminate_children();
		return false;
	}

	// A separate file, a separate concern: decoration appearance is not
	// window-management behaviour, and a bad colour must never be able to
	// stop the window manager. WindowDecorations::load never fails -- on
	// any error the settings keep their documented defaults.
	window_decorations.load("window-decorations.json");

	// B13: connect to the compositor before anything that needs the
	// registry.
	if (!server.initialize()) {
		child_processes.terminate_children();
		return false;
	}

	// Registry globals (river_window_manager_v1, river_layer_shell_v1)
	// arrive during the roundtrip, so it must complete before anything
	// inspects them.
	if (!display.initialize(&server, config)) {
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	if (!display.roundtrip(&server)) {
		std::fprintf(stderr,
			     "Yarfwm: failed to complete registry roundtrip\n");
		display.terminate();
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	if (!display.window_manager) {
		std::fprintf(stderr, "Yarfwm: compositor does not offer "
				     "river_window_manager_v1\n");
		display.terminate();
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	if (!seat.initialize(&server, &display, config)) {
		display.terminate();
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	if (!view.initialize(&server, &seat, &display, config,
			     window_decorations.settings())) {
		seat.terminate();
		display.terminate();
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	if (!keybind.initialize(&server, &display, &seat, &view, config)) {
		view.terminate();
		seat.terminate();
		display.terminate();
		server.terminate();
		child_processes.terminate_children();
		return false;
	}

	running = true;
	return true;
}

int Yarfwm::run()
{
	if (!server.display || !server.registry) {
		return 1;
	}

	while (running) {
		// C8: the server asks for shutdown through a flag; the loop
		// exits and teardown happens outside any event callback.
		if (view.should_shutdown()) {
			break;
		}

		// A SIGTERM or SIGINT asks this process to stop. Leaving
		// through the same teardown as any other exit means the
		// children are shut down and reaped rather than orphaned by an
		// abrupt death.
		if (child_processes.shutdown_requested()) {
			break;
		}

		// Take the read lock before flushing so that no event can
		// arrive between the flush and the poll below.
		if (wl_display_prepare_read(server.display) != 0) {
			if (wl_display_dispatch_pending(server.display) == -1) {
				break;
			}
			continue;
		}

		if (wl_display_flush(server.display) < 0) {
			wl_display_cancel_read(server.display);
			break;
		}

		// Wait on the Wayland connection, the key repeat timer when it
		// is available, and the child signal descriptor. A negative
		// descriptor is ignored by poll, so all three slots are always
		// passed and their availability needs no bookkeeping.
		struct pollfd descriptors[3];
		descriptors[0].fd = wl_display_get_fd(server.display);
		descriptors[0].events = POLLIN;
		descriptors[0].revents = 0;
		descriptors[1].fd = keybind.repeat_timer_file_descriptor();
		descriptors[1].events = POLLIN;
		descriptors[1].revents = 0;
		descriptors[2].fd = child_processes.file_descriptor();
		descriptors[2].events = POLLIN;
		descriptors[2].revents = 0;

		const int descriptor_count = 3;
		const int ready = poll(descriptors, descriptor_count, -1);
		if (ready < 0) {
			wl_display_cancel_read(server.display);
			break;
		}

		if (descriptors[0].revents & POLLIN) {
			if (wl_display_read_events(server.display) == -1) {
				break;
			}
		} else {
			wl_display_cancel_read(server.display);
		}

		// A held key binding: perform its action again.
		if (descriptors[1].revents & POLLIN) {
			keybind.handle_repeat_timer();
		}

		// One or more children exited: collect them, and pick up a
		// shutdown request that arrived with them.
		if (descriptors[2].revents & POLLIN) {
			child_processes.reap_exited_children();
		}

		if (wl_display_dispatch_pending(server.display) == -1) {
			break;
		}
	}

	terminate();
	return 0;
}

void Yarfwm::terminate()
{
	running = false;

	// The children go first, while the Wayland connection is still open:
	// they are clients of this session and can only exit cleanly while the
	// compositor is still answering them. SIGTERM first, reaped after, so
	// the group is left with no zombie behind. River sends the same signal
	// to the whole process group when it exits (river(1), CONFIGURATION);
	// this covers the case where the window manager stops first.
	child_processes.terminate_children();
	child_processes.terminate();

	keybind.terminate();
	view.terminate();
	seat.terminate();
	display.terminate();
	server.terminate();
}
