#ifndef DISPLAY_HPP
#define DISPLAY_HPP

#include "Config.hpp"
#include "LayerShell.hpp"
#include "Server.hpp"

struct river_xkb_bindings_v1;

// Represents the River client-side compositor state exposed to the window
// manager.
class Display
{
      public:
	Display();
	~Display();

	bool initialize(Server *server, Config &config);
	bool roundtrip(Server *server);
	void terminate();

	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shared_memory;

	struct river_window_manager_v1 *window_manager;

	// The river_xkb_bindings_v1 global: the keyboard binding factory. The
	// Keybind engine creates one river_xkb_binding_v1 per configured
	// binding from it.
	struct river_xkb_bindings_v1 *xkb_bindings;

	// The river_layer_shell_v1 global and its per-object state factories.
	// The View creates and owns one layer shell output state per output and
	// the Seat one layer shell seat state per seat.
	LayerShell layer_shell_state;

	// The wl_seat globals seen in the registry, by name.
	//
	// A river_seat_v1 event carries only the wl_seat's registry *name*
	// (river_seat_v1.wl_seat), so this is how the Seat turns that number
	// back into the object it must bind a wl_pointer from. The wl_seat is
	// bound here, in the registry callback, because that is the only place
	// the advertised version is known -- binding a higher version than the
	// compositor advertised is a protocol error. The wl_pointer is bound
	// later, in Seat::river_seat_wl_seat, because that is the first moment
	// the name and the river_seat_v1 object can be matched up.
	static const int max_seat_names = 16;
	uint32_t seat_names[max_seat_names];
	struct wl_seat *seats[max_seat_names];
	// Whether each seat has ever advertised the pointer capability. It is
	// tracked HERE, next to the wl_seat objects, because the capabilities
	// event can arrive before river has named the seat -- and
	// wl_seat.get_pointer is a protocol error until the capability has
	// been advertised at least once.
	bool seat_pointer_capable[max_seat_names];
	int seat_name_count;

	// The wl_seat with the given registry name, or null when it is not
	// one we saw.
	struct wl_seat *seat_by_registry_name(uint32_t name) const;

	// Whether that seat has advertised the pointer capability, so a
	// wl_pointer may be bound from it.
	bool seat_has_pointer(uint32_t name) const;

	// Record a wl_seat bound in the registry callback. A name seen twice
	// (a compositor re-advertising) is ignored rather than duplicated.
	void remember_seat(uint32_t name, struct wl_seat *seat);

	// The wl_seat listener. It lives here rather than on the Seat because
	// the capabilities event is a property of the seat object, and it can
	// arrive before the Seat knows which river_seat_v1 it belongs to.
	static void seat_capabilities(void *data, struct wl_seat *wl_seat,
				      uint32_t capabilities);
	static void seat_name(void *data, struct wl_seat *wl_seat,
			      const char *name);

	// The Seat that owns the wl_pointer bindings. Capabilities are a
	// property of the seat object and arrive here; the Seat is what turns
	// one into a wl_pointer, so the event is forwarded. Public because
	// Seat::initialize sets it and Seat::update_pointer_capability is
	// what consumes it.
	class Seat *seat_owner = nullptr;
};

#endif // DISPLAY_HPP
