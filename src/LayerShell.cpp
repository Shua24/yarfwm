#include "LayerShell.hpp"
#include "river-layer-shell-v1-client-protocol.h"
#include <algorithm>
#include <cstdio>

LayerShell::LayerShell() : layer_shell(nullptr) {}

LayerShell::~LayerShell() { terminate(); }

void LayerShell::bind_registry_global(struct wl_registry *registry,
				      uint32_t name, uint32_t version)
{
	if (layer_shell) {
		return;
	}

	layer_shell = static_cast<struct river_layer_shell_v1 *>(
	    wl_registry_bind(registry, name, &river_layer_shell_v1_interface,
			     std::min<uint32_t>(version, 1)));
	if (!layer_shell) {
		std::fprintf(stderr,
			     "Yarfwm: failed to bind river_layer_shell_v1\n");
	}
}

bool LayerShell::available() const { return layer_shell != nullptr; }

struct river_layer_shell_output_v1 *
LayerShell::create_output_state(struct river_output_v1 *output)
{
	if (!layer_shell) {
		return nullptr;
	}
	return river_layer_shell_v1_get_output(layer_shell, output);
}

struct river_layer_shell_seat_v1 *
LayerShell::create_seat_state(struct river_seat_v1 *seat)
{
	if (!layer_shell) {
		return nullptr;
	}
	return river_layer_shell_v1_get_seat(layer_shell, seat);
}

void LayerShell::terminate()
{
	if (layer_shell) {
		river_layer_shell_v1_destroy(layer_shell);
		layer_shell = nullptr;
	}
}
