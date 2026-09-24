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

	if (!config.load("config.json")) {
		return false;
	}

	// B13: connect to the compositor before anything that needs the
	// registry.
	if (!server.initialize()) {
		return false;
	}

	// Registry globals (river_window_manager_v1, river_layer_shell_v1)
	// arrive during the roundtrip, so it must complete before anything
	// inspects them.
	if (!display.initialize(&server, config)) {
		server.terminate();
		return false;
	}

	if (!display.roundtrip(&server)) {
		std::fprintf(stderr,
			     "Yarfwm: failed to complete registry roundtrip\n");
		display.terminate();
		server.terminate();
		return false;
	}

	if (!display.window_manager) {
		std::fprintf(stderr, "Yarfwm: compositor does not offer "
				     "river_window_manager_v1\n");
		display.terminate();
		server.terminate();
		return false;
	}

	if (!seat.initialize(&server, &display, config)) {
		display.terminate();
		server.terminate();
		return false;
	}

	if (!view.initialize(&server, &seat, &display, config)) {
		seat.terminate();
		display.terminate();
		server.terminate();
		return false;
	}

	if (!keybind.initialize(&server, &display, &seat, &view, config)) {
		view.terminate();
		seat.terminate();
		display.terminate();
		server.terminate();
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

		// Wait on the Wayland connection and, when key repeat is
		// available, its timer as well.
		struct pollfd descriptors[2];
		descriptors[0].fd = wl_display_get_fd(server.display);
		descriptors[0].events = POLLIN;
		descriptors[0].revents = 0;
		descriptors[1].fd = keybind.repeat_timer_file_descriptor();
		descriptors[1].events = POLLIN;
		descriptors[1].revents = 0;

		const int descriptor_count = descriptors[1].fd >= 0 ? 2 : 1;
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
		if (descriptor_count == 2 &&
		    (descriptors[1].revents & POLLIN)) {
			keybind.handle_repeat_timer();
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
	keybind.terminate();
	view.terminate();
	seat.terminate();
	display.terminate();
	server.terminate();
}
