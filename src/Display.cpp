#include "Display.hpp"
#include "river-window-management-v1-client-protocol.h"
#include "river-xkb-bindings-v1.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

Display::Display()
    : registry(nullptr), compositor(nullptr), shared_memory(nullptr),
      window_manager(nullptr), xkb_bindings(nullptr)
{
}

Display::~Display() { terminate(); }

static void display_handle_destroy(struct wl_listener *listener, void *data)
{
	(void)listener;
	(void)data;
	// B11: the old handler cast the listener data to Display* and called
	// terminate() from inside a dispatch callback. Shutdown now travels
	// through View::shutdown_requested and the main loop, so this stays
	// unused.
}

static void registry_handle_global(void *data, struct wl_registry *registry,
				   uint32_t name, const char *interface,
				   uint32_t version)
{
	Display *display = static_cast<Display *>(data);

	if (std::strcmp(interface, "river_window_manager_v1") == 0) {
		display->window_manager =
		    static_cast<struct river_window_manager_v1 *>(
			wl_registry_bind(registry, name,
					 &river_window_manager_v1_interface,
					 std::min<uint32_t>(version, 6)));
	} else if (std::strcmp(interface, "river_layer_shell_v1") == 0) {
		// Binding this global is how the window manager tells river it
		// supports layer shell; without it river refuses to map layer
		// surfaces (wallpaper clients, bars) at all.
		display->layer_shell_state.bind_registry_global(registry, name,
								version);
	} else if (std::strcmp(interface, "river_xkb_bindings_v1") == 0) {
		// The keyboard binding factory. River only advertises it next
		// to the window management global; version 3 is what river
		// 0.4.8 speaks.
		display->xkb_bindings =
		    static_cast<struct river_xkb_bindings_v1 *>(
			wl_registry_bind(registry, name,
					 &river_xkb_bindings_v1_interface,
					 std::min<uint32_t>(version, 3)));
	}
}

static void registry_handle_global_remove(void *data,
					  struct wl_registry *registry,
					  uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

bool Display::initialize(Server *server, Config &config)
{
	static const struct wl_registry_listener registry_listener = {
	    registry_handle_global,
	    registry_handle_global_remove,
	};

	(void)config;

	if (!server->registry) {
		return false;
	}

	registry = server->registry;
	wl_registry_add_listener(registry, &registry_listener, this);
	return true;
}

bool Display::roundtrip(Server *server)
{
	if (!registry || !server || !server->display) {
		return false;
	}
	// C6: one bounded roundtrip so the registry globals arrive before we
	// decide what to bind. The old unbounded dispatch loop never returned.
	return wl_display_roundtrip(server->display) >= 0;
}

void Display::terminate()
{
	// Children first: the View and the Seat destroy their layer shell
	// output and seat state objects in their own terminate() before this
	// runs (see Yarfwm::terminate()), so the global is the last layer shell
	// object left.
	layer_shell_state.terminate();
	if (xkb_bindings) {
		river_xkb_bindings_v1_destroy(xkb_bindings);
		xkb_bindings = nullptr;
	}
	if (window_manager) {
		river_window_manager_v1_destroy(window_manager);
		window_manager = nullptr;
	}
	if (shared_memory) {
		wl_shm_destroy(shared_memory);
		shared_memory = nullptr;
	}
	if (compositor) {
		wl_compositor_destroy(compositor);
		compositor = nullptr;
	}
	// The registry is borrowed from Server, which owns the connection and
	// destroys it in Server::terminate(). Destroying it here as well would
	// double free the proxy (crash: SEGV inside wl_proxy_destroy).
	registry = nullptr;
}
