#include "Display.hpp"
#include "RegistryInterfaces.hpp"
#include "Seat.hpp"
#include "river-window-management-v1-client-protocol.h"
#include "river-xkb-bindings-v1.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <wayland-client.h>

Display::Display()
    : registry(nullptr), compositor(nullptr), shared_memory(nullptr),
      window_manager(nullptr), xkb_bindings(nullptr), seat_name_count(0)
{
	std::memset(seat_names, 0, sizeof(seat_names));
	std::memset(seats, 0, sizeof(seats));
}

struct wl_seat *Display::seat_by_registry_name(uint32_t name) const
{
	for (int i = 0; i < seat_name_count; i++) {
		if (seat_names[i] == name) {
			return seats[i];
		}
	}
	return nullptr;
}

void Display::remember_seat(uint32_t name, struct wl_seat *seat)
{
	if (!seat || seat_by_registry_name(name)) {
		return;
	}
	if (seat_name_count >= max_seat_names) {
		std::fprintf(stderr,
			     "Yarfwm: more than %d seats, ignoring the rest\n",
			     max_seat_names);
		wl_seat_destroy(seat);
		return;
	}
	seat_names[seat_name_count] = name;
	seats[seat_name_count] = seat;
	seat_pointer_capable[seat_name_count] = false;
	seat_name_count++;

	// The listener goes on immediately, at bind time: the capabilities
	// event is a property of the seat, and it can arrive before river has
	// named the seat, so waiting would miss it and make get_pointer a
	// protocol error later.
	static const struct wl_seat_listener seat_listener = [] {
		struct wl_seat_listener listener{};
		listener.capabilities = seat_capabilities;
		listener.name = seat_name;
		return listener;
	}();
	wl_seat_add_listener(seat, &seat_listener, this);
}

bool Display::seat_has_pointer(uint32_t name) const
{
	for (int i = 0; i < seat_name_count; i++) {
		if (seat_names[i] == name) {
			return seat_pointer_capable[i];
		}
	}
	return false;
}

void Display::seat_capabilities(void *data, struct wl_seat *wl_seat,
				uint32_t capabilities)
{
	Display *display = static_cast<Display *>(data);
	for (int i = 0; i < display->seat_name_count; i++) {
		if (display->seats[i] != wl_seat) {
			continue;
		}
		if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
			// Sticky on purpose: the capability is advertised at
			// least once, which is the condition the protocol
			// puts on get_pointer. Losing the device later does
			// not retract that.
			display->seat_pointer_capable[i] = true;
		}
	}
	// A seat that gains a pointer after river already named it still needs
	// its wl_pointer; a seat that loses one must drop it.
	if (display->seat_owner) {
		display->seat_owner->update_pointer_capability(
		    wl_seat, (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0);
	}
}

void Display::seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	// Only used for log lines; the registry name is what correlates the
	// two objects.
	(void)data;
	(void)wl_seat;
	(void)name;
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

	switch (classify_registry_interface(interface)) {
	case registry_interface_window_manager:
		display->window_manager =
		    static_cast<struct river_window_manager_v1 *>(
			wl_registry_bind(registry, name,
					 &river_window_manager_v1_interface,
					 std::min<uint32_t>(version, 6)));
		break;
	case registry_interface_layer_shell:
		// Binding this global is how the window manager tells river it
		// supports layer shell; without it river refuses to map layer
		// surfaces (wallpaper clients, bars) at all.
		display->layer_shell_state.bind_registry_global(registry, name,
								version);
		break;
	case registry_interface_xkb_bindings:
		// The keyboard binding factory. River only advertises it next
		// to the window management global; version 3 is what river
		// 0.4.8 speaks.
		display->xkb_bindings =
		    static_cast<struct river_xkb_bindings_v1 *>(
			wl_registry_bind(registry, name,
					 &river_xkb_bindings_v1_interface,
					 std::min<uint32_t>(version, 3)));
		break;
	case registry_interface_compositor:
		// wl_compositor: needed to create the decoration surfaces. The
		// window manager creates surfaces and never renders them
		// through the compositor's GL path; a wl_shm buffer is the only
		// backing store used, which is why version 4 (the minimum that
		// can create a surface and a region) is enough.
		display->compositor = static_cast<struct wl_compositor *>(
		    wl_registry_bind(registry, name, &wl_compositor_interface,
				     std::min<uint32_t>(version, 4)));
		break;
	case registry_interface_shared_memory:
		// wl_shm: the buffer backing store for the titlebar. Version 1
		// is the whole API surface used here (create_pool + format).
		display->shared_memory = static_cast<struct wl_shm *>(
		    wl_registry_bind(registry, name, &wl_shm_interface, 1));
		break;
	case registry_interface_seat:
		// wl_seat: bound for the titlebar click channel only. River
		// routes a press on a window to window_interaction, but a press
		// on a decoration surface has no scene node for river to
		// resolve and arrives as an ordinary wl_pointer event on our
		// own surface -- so the Seat needs its own pointer, and the
		// pointer comes from the seat.
		//
		// The version is taken from the advertisement: binding a
		// higher version than the compositor offers is a protocol
		// error, and the pointer's own version follows its parent.
		display->remember_seat(
		    name, static_cast<struct wl_seat *>(wl_registry_bind(
			      registry, name, &wl_seat_interface,
			      std::min<uint32_t>(version, 9))));
		break;
	case registry_interface_unknown:
	default:
		// Every other global is deliberately ignored.
		break;
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
