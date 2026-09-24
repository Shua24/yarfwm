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
};

#endif // DISPLAY_HPP
