#include "RegistryInterfaces.hpp"
#include <cstring>

RegistryInterfaceKind classify_registry_interface(const char *interface_name)
{
	if (!interface_name) {
		return registry_interface_unknown;
	}
	if (std::strcmp(interface_name, "river_window_manager_v1") == 0) {
		return registry_interface_window_manager;
	}
	if (std::strcmp(interface_name, "river_layer_shell_v1") == 0) {
		return registry_interface_layer_shell;
	}
	if (std::strcmp(interface_name, "river_xkb_bindings_v1") == 0) {
		return registry_interface_xkb_bindings;
	}
	if (std::strcmp(interface_name, "wl_compositor") == 0) {
		return registry_interface_compositor;
	}
	if (std::strcmp(interface_name, "wl_shm") == 0) {
		return registry_interface_shared_memory;
	}
	if (std::strcmp(interface_name, "wl_seat") == 0) {
		return registry_interface_seat;
	}
	return registry_interface_unknown;
}
